//! In-browser document sessions. Same command names and reply shapes as the Tauri
//! backend, minus everything that needs a filesystem, a compiler, or a network:
//! files live in a map fed by JS, saves mark the document clean, build/run/agent refuse.

use std::cell::RefCell;
use std::collections::HashMap;

use cler_graph::{
    extract_specs, first_fault, unified, ActionQueue, ApplyError, BlockSpec, Command,
    DocumentSession, FileModel, ParseFault, PatchDirection, SourcePatch, Transaction,
    SCHEMA_VERSION,
};
use serde::{Deserialize, Serialize};
use serde_json::{json, Map, Value};
use sha2::{Digest, Sha256};

const NEW_DOCUMENT_TEMPLATE: &str = r#"#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"

#include <chrono>
#include <thread>

int main() {
    auto flowgraph = cler::make_desktop_flowgraph();

    cler::FlowGraphConfig config;
    config.scheduler = cler::SchedulerType::ThreadPerBlock;
    flowgraph.run(config);
    while (!flowgraph.is_stopped()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return 0;
}
"#;

const DESKTOP_ONLY: &str = "needs the desktop app";

struct Document {
    session: DocumentSession,
    saved: String,
    history: ActionQueue<Action>,
    ui: Value,
}

#[derive(Clone)]
enum Action {
    Source(SourcePatch),
    MoveNodes { view: String, moves: Vec<NodeMove> },
}

#[derive(Clone, Debug, Deserialize, PartialEq)]
#[serde(rename_all = "camelCase")]
struct Point {
    x: f64,
    y: f64,
}

#[derive(Clone, Debug, Deserialize, PartialEq)]
#[serde(rename_all = "camelCase")]
struct NodeMove {
    node: String,
    from: Point,
    to: Point,
}

#[derive(Serialize)]
struct DocumentModel {
    sha256: String,
    #[serde(rename = "hasErrors")]
    has_errors: bool,
    #[serde(flatten)]
    model: FileModel,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct DocumentState {
    path: String,
    revision: u64,
    model: DocumentModel,
    source: String,
    can_undo: bool,
    can_redo: bool,
    dirty: bool,
    external_change: bool,
    cache: Value,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct EditOutcome {
    state: DocumentState,
    unparsed: bool,
    fault: Option<ParseFault>,
}

#[derive(Default)]
struct World {
    files: HashMap<String, String>,
    palette: Vec<BlockSpec>,
    docs: HashMap<String, Document>,
}

thread_local! {
    static WORLD: RefCell<World> = RefCell::new(World::default());
}

// ---- ABI: JS allocates a UTF-8 buffer, calls cler_invoke, reads a NUL-terminated reply, frees both.

#[no_mangle]
pub extern "C" fn cler_alloc(len: usize) -> *mut u8 {
    let mut buffer = Vec::<u8>::with_capacity(len.max(1));
    let ptr = buffer.as_mut_ptr();
    std::mem::forget(buffer);
    ptr
}

#[no_mangle]
pub unsafe extern "C" fn cler_free(ptr: *mut u8, len: usize) {
    drop(Vec::from_raw_parts(ptr, 0, len.max(1)));
}

#[no_mangle]
pub unsafe extern "C" fn cler_invoke(ptr: *const u8, len: usize) -> *mut u8 {
    let request = std::str::from_utf8_unchecked(std::slice::from_raw_parts(ptr, len));
    let reply = handle(request);
    let mut bytes = reply.into_bytes();
    bytes.push(0);
    let ptr = bytes.as_mut_ptr();
    std::mem::forget(bytes);
    ptr
}

fn handle(request: &str) -> String {
    let parsed: Value = match serde_json::from_str(request) {
        Ok(value) => value,
        Err(cause) => return json!({ "loud": format!("bad invoke body: {cause}") }).to_string(),
    };
    let cmd = parsed.get("cmd").and_then(Value::as_str).unwrap_or("");
    let args = parsed.get("args").cloned().unwrap_or(Value::Null);
    let reply = WORLD.with(|world| dispatch(&mut world.borrow_mut(), cmd, &args));
    match reply {
        Ok(value) => json!({ "ok": value }),
        Err(message) => json!({ "err": message }),
    }
    .to_string()
}

fn dispatch(world: &mut World, cmd: &str, args: &Value) -> Result<Value, String> {
    match cmd {
        // JS-only setup commands
        "put_file" => {
            let path = text(args, "path")?;
            let content = text(args, "text")?;
            world.files.insert(path.clone(), content.clone());
            // a header is also a palette source; the browser has no directory to scan
            if path.ends_with(".hpp") || path.ends_with(".h") {
                world.palette.retain(|spec| spec.origin != path);
                world.palette.extend(extract_specs(&content, &path));
            }
            Ok(Value::Null)
        }
        "app_settings" | "set_app_settings" => Ok(json!({
            "clerRoot": null, "blockLibraries": [], "aiAgentModel": null,
            "aiAgentProvider": null, "aiAgentBaseUrl": null
        })),
        "ai_agent_status" => Ok(json!({
            "available": false, "provider": "anthropic", "model": "",
            "reason": format!("the AI agent {DESKTOP_ONLY}"), "method": null
        })),
        "ai_agent_stop" => Ok(Value::Null),
        "ai_agent_models" => Ok(json!([])),
        "resolved_cler_root" => Ok(Value::Null),
        "open_in_editor" => Err(format!("opening an editor {DESKTOP_ONLY}")),
        _ => document_command(world, cmd, args),
    }
}

fn document_command(world: &mut World, cmd: &str, args: &Value) -> Result<Value, String> {
    let path = text(args, "path")?;
    match cmd {
        "open_document" => {
            if !world.docs.contains_key(&path) {
                let source = world
                    .files
                    .get(&path)
                    .cloned()
                    .ok_or_else(|| format!("cannot resolve {path}: not in the browser bundle"))?;
                let doc = fresh(source)?;
                world.docs.insert(path.clone(), doc);
            }
            state(world, &path)
        }
        "new_document" => {
            if world.files.contains_key(&path) {
                return Err(format!("{path} already exists"));
            }
            world.files.insert(path.clone(), NEW_DOCUMENT_TEMPLATE.to_string());
            world.docs.insert(path.clone(), fresh(NEW_DOCUMENT_TEMPLATE.to_string())?);
            state(world, &path)
        }
        "close_document" => {
            world.docs.remove(&path);
            Ok(Value::Null)
        }
        "reload_document" => {
            let saved = world.files.get(&path).cloned().unwrap_or_default();
            let doc = doc_mut(world, &path)?;
            if saved != doc.session.source() {
                doc.session.reload(saved.clone()).map_err(|cause| cause.to_string())?;
                doc.history.clear();
            }
            doc.saved = saved;
            state(world, &path)
        }
        "save_document" => {
            let doc = doc_mut(world, &path)?;
            doc.saved = doc.session.source().to_string();
            let saved = doc.saved.clone();
            world.files.insert(path.clone(), saved);
            state(world, &path)
        }
        "save_document_as" => {
            let new_path = text(args, "newPath")?;
            let source = doc_mut(world, &path)?.session.source().to_string();
            world.files.insert(new_path.clone(), source.clone());
            world.docs.insert(new_path.clone(), fresh(source)?);
            state(world, &new_path)
        }
        "save_cache" => {
            doc_mut(world, &path)?.ui = args.get("ui").cloned().unwrap_or(Value::Null);
            Ok(Value::Null)
        }
        "palette" => {
            let doc = doc_mut(world, &path)?;
            let mut specs = extract_specs(doc.session.source(), &path);
            let local: Vec<String> = specs.iter().map(|spec| spec.name.clone()).collect();
            specs.extend(
                world
                    .palette
                    .iter()
                    .filter(|spec| !local.contains(&spec.name))
                    .cloned(),
            );
            serde_json::to_value(specs).map_err(|cause| cause.to_string())
        }
        "apply_commands" | "preview_commands" => {
            let base = revision(args)?;
            let commands: Vec<Command> = serde_json::from_value(
                args.get("commands").cloned().unwrap_or(Value::Array(Vec::new())),
            )
            .map_err(|cause| cause.to_string())?;
            let includes = add_block_origins(&commands, &world.palette);
            let doc = doc_mut(world, &path)?;
            let pending = doc
                .session
                .preview_with_includes(
                    Transaction {
                        version: SCHEMA_VERSION.to_string(),
                        base_revision: base,
                        commands,
                    },
                    &includes,
                )
                .map_err(|cause| cause.to_string())?;
            if cmd == "preview_commands" {
                return Ok(json!({
                    "diff": unified(doc.session.source(), pending.source(), pending.splices()),
                    "summary": { "splices": pending.splices().len() }
                }));
            }
            if pending.changes() {
                let patch = pending
                    .patch()
                    .cloned()
                    .ok_or_else(|| ApplyError::HistoryMismatch.to_string())?;
                doc.session.commit(pending);
                doc.history.push(Action::Source(patch));
            } else {
                doc.session.commit(pending);
            }
            state(world, &path)
        }
        "edit_source" => {
            let base = revision(args)?;
            let source = text(args, "source")?;
            let doc = doc_mut(world, &path)?;
            if base != doc.session.revision() {
                return Err(ApplyError::RevisionMismatch {
                    base_revision: base,
                    current_revision: doc.session.revision(),
                }
                .to_string());
            }
            let pending = match doc.session.preview_text(source.clone()) {
                Ok(pending) => pending,
                Err(ApplyError::ProducesParseError) => {
                    let fault = first_fault(&source);
                    return serde_json::to_value(EditOutcome {
                        unparsed: true,
                        fault,
                        state: snapshot(&path, doc),
                    })
                    .map_err(|cause| cause.to_string());
                }
                Err(cause) => return Err(cause.to_string()),
            };
            if pending.changes() {
                let patch = pending
                    .patch()
                    .cloned()
                    .ok_or_else(|| ApplyError::HistoryMismatch.to_string())?;
                doc.session.commit(pending);
                doc.history.push(Action::Source(patch));
            } else {
                doc.session.commit(pending);
            }
            serde_json::to_value(EditOutcome {
                unparsed: false,
                fault: None,
                state: snapshot(&path, doc),
            })
            .map_err(|cause| cause.to_string())
        }
        "move_nodes" => {
            let view = text(args, "view")?;
            let mut moves: Vec<NodeMove> =
                serde_json::from_value(args.get("moves").cloned().unwrap_or(Value::Null))
                    .map_err(|cause| cause.to_string())?;
            moves.retain(|movement| movement.from != movement.to);
            if view.is_empty() {
                return Err("node movement requires a view".to_string());
            }
            let doc = doc_mut(world, &path)?;
            if !moves.is_empty() {
                moves.sort_by(|left, right| left.node.cmp(&right.node));
                apply_node_moves(&mut doc.ui, &view, &moves, PatchDirection::Forward);
                doc.history.push(Action::MoveNodes { view, moves });
            }
            state(world, &path)
        }
        "undo" | "redo" => {
            let doc = doc_mut(world, &path)?;
            let undo = cmd == "undo";
            let action = if undo { doc.history.undo() } else { doc.history.redo() }.cloned();
            let Some(action) = action else {
                return Err(if undo {
                    ApplyError::NothingToUndo.to_string()
                } else {
                    ApplyError::NothingToRedo.to_string()
                });
            };
            let direction = if undo { PatchDirection::Reverse } else { PatchDirection::Forward };
            match action {
                Action::Source(patch) => {
                    let source = patch
                        .apply(doc.session.source(), direction)
                        .map_err(|cause| cause.to_string())?;
                    let pending = doc
                        .session
                        .preview_source(source)
                        .map_err(|cause| cause.to_string())?;
                    doc.session.commit(pending);
                }
                Action::MoveNodes { view, moves } => {
                    apply_node_moves(&mut doc.ui, &view, &moves, direction);
                }
            }
            if undo {
                doc.history.commit_undo();
            } else {
                doc.history.commit_redo();
            }
            state(world, &path)
        }
        _ => Err(format!("unknown command: {cmd}")),
    }
}

fn text(args: &Value, key: &str) -> Result<String, String> {
    args.get(key)
        .and_then(Value::as_str)
        .map(str::to_string)
        .ok_or_else(|| format!("missing {key} argument"))
}

fn revision(args: &Value) -> Result<u64, String> {
    args.get("base_revision")
        .or_else(|| args.get("baseRevision"))
        .and_then(Value::as_u64)
        .ok_or_else(|| "missing baseRevision".to_string())
}

fn fresh(source: String) -> Result<Document, String> {
    let session = DocumentSession::load(source.clone()).map_err(|cause| cause.to_string())?;
    Ok(Document {
        session,
        saved: source,
        history: ActionQueue::default(),
        ui: Value::Object(Map::new()),
    })
}

fn doc_mut<'a>(world: &'a mut World, path: &str) -> Result<&'a mut Document, String> {
    world
        .docs
        .get_mut(path)
        .ok_or_else(|| format!("no open document for {path}"))
}

fn state(world: &mut World, path: &str) -> Result<Value, String> {
    let palette = std::mem::take(&mut world.palette);
    let doc = doc_mut(world, path);
    let result = doc.map(|doc| snapshot_with(path, doc, &palette));
    world.palette = palette;
    let state = result?;
    serde_json::to_value(state).map_err(|cause| cause.to_string())
}

fn snapshot(path: &str, doc: &Document) -> DocumentState {
    snapshot_with(path, doc, &[])
}

fn snapshot_with(path: &str, doc: &Document, palette: &[BlockSpec]) -> DocumentState {
    let model = doc.session.parse_with_palette(palette);
    DocumentState {
        path: path.to_string(),
        revision: doc.session.revision(),
        model: DocumentModel {
            sha256: format!("{:x}", Sha256::digest(doc.session.source().as_bytes())),
            has_errors: model.has_errors,
            model,
        },
        source: doc.session.source().to_string(),
        can_undo: doc.history.can_undo(),
        can_redo: doc.history.can_redo(),
        dirty: doc.session.source() != doc.saved,
        external_change: false,
        cache: doc.ui.clone(),
    }
}

// Palette origins are repo-relative ("desktop_blocks/…") because JS registers them that
// way, so the include path is the origin itself.
fn add_block_origins(commands: &[Command], palette: &[BlockSpec]) -> Vec<String> {
    commands
        .iter()
        .filter_map(|command| match command {
            Command::AddBlock { type_name, .. } => palette
                .iter()
                .find(|spec| {
                    spec.name == *type_name
                        || spec.synonyms.iter().any(|synonym| synonym.name == *type_name)
                })
                .map(|spec| spec.origin.clone()),
            _ => None,
        })
        .collect()
}

fn apply_node_moves(ui: &mut Value, view: &str, moves: &[NodeMove], direction: PatchDirection) {
    let views = object_field(object(ui), "views");
    let cached_view = object_field(views, view);
    let positions = object_field(cached_view, "positions");
    for movement in moves {
        let point = match direction {
            PatchDirection::Reverse => &movement.from,
            PatchDirection::Forward => &movement.to,
        };
        positions.insert(movement.node.clone(), json!({ "x": point.x, "y": point.y }));
    }
}

fn object(value: &mut Value) -> &mut Map<String, Value> {
    if !value.is_object() {
        *value = Value::Object(Map::new());
    }
    value.as_object_mut().expect("object was just initialized")
}

fn object_field<'a>(parent: &'a mut Map<String, Value>, key: &str) -> &'a mut Map<String, Value> {
    object(
        parent
            .entry(key.to_string())
            .or_insert_with(|| Value::Object(Map::new())),
    )
}

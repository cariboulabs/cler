//! In-browser document sessions. Same command names and reply shapes as the Tauri
//! backend, minus everything that needs a filesystem, a compiler, or a network:
//! files live in a map fed by JS, saves mark the document clean, build/run/agent refuse.

use std::cell::RefCell;
use std::collections::HashMap;

use cler_graph::session::{self, Document, EditOutcome, Edited, NodeMove};
use cler_graph::{extract_specs, BlockSpec, Command, PatchDirection};
use serde_json::{json, Value};

const DESKTOP_ONLY: &str = "needs the desktop app";

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
                let doc = Document::load(source).map_err(|cause| cause.to_string())?;
                world.docs.insert(path.clone(), doc);
            }
            state(world, &path)
        }
        "new_document" => {
            if world.files.contains_key(&path) {
                return Err(format!("{path} already exists"));
            }
            let source = session::NEW_DOCUMENT_TEMPLATE.to_string();
            world.files.insert(path.clone(), source.clone());
            world.docs.insert(
                path.clone(),
                Document::load(source).map_err(|cause| cause.to_string())?,
            );
            state(world, &path)
        }
        "close_document" => {
            world.docs.remove(&path);
            Ok(Value::Null)
        }
        "reload_document" => {
            let saved = world.files.get(&path).cloned().unwrap_or_default();
            doc_mut(&mut world.docs, &path)?.reload(saved)?;
            state(world, &path)
        }
        "save_document" => {
            let doc = doc_mut(&mut world.docs, &path)?;
            doc.saved = doc.session.source().to_string();
            let saved = doc.saved.clone();
            world.files.insert(path.clone(), saved);
            state(world, &path)
        }
        "save_document_as" => {
            let new_path = text(args, "newPath")?;
            let source = doc_mut(&mut world.docs, &path)?.session.source().to_string();
            world.files.insert(new_path.clone(), source.clone());
            world.docs.insert(
                new_path.clone(),
                Document::load(source).map_err(|cause| cause.to_string())?,
            );
            state(world, &new_path)
        }
        "save_cache" => {
            doc_mut(&mut world.docs, &path)?.ui = args.get("ui").cloned().unwrap_or(Value::Null);
            Ok(Value::Null)
        }
        "palette" => {
            let specs = session::palette(doc_mut(&mut world.docs, &path)?, &path, &world.palette);
            serde_json::to_value(specs).map_err(|cause| cause.to_string())
        }
        "apply_commands" | "preview_commands" => {
            let base = revision(args)?;
            let commands: Vec<Command> = serde_json::from_value(
                args.get("commands").cloned().unwrap_or(Value::Array(Vec::new())),
            )
            .map_err(|cause| cause.to_string())?;
            // JS registers palette origins repo-relative, so an origin is already an include path.
            let includes = session::add_block_origins(&commands, &world.palette);
            let doc = doc_mut(&mut world.docs, &path)?;
            if cmd == "preview_commands" {
                let preview = session::preview(doc, base, commands, &includes)?;
                return serde_json::to_value(preview).map_err(|cause| cause.to_string());
            }
            session::apply(doc, base, commands, &includes)?;
            state(world, &path)
        }
        "edit_source" => {
            let base = revision(args)?;
            let source = text(args, "source")?;
            let doc = doc_mut(&mut world.docs, &path)?;
            let (unparsed, fault) = match session::edit(doc, base, &source)? {
                Edited::Applied => (false, None),
                Edited::Unparsed(fault) => (true, fault),
            };
            serde_json::to_value(EditOutcome {
                unparsed,
                fault,
                state: session::snapshot(&path, doc, &[]),
            })
            .map_err(|cause| cause.to_string())
        }
        "move_nodes" => {
            let view = text(args, "view")?;
            let moves: Vec<NodeMove> =
                serde_json::from_value(args.get("moves").cloned().unwrap_or(Value::Null))
                    .map_err(|cause| cause.to_string())?;
            session::move_nodes(doc_mut(&mut world.docs, &path)?, &view, moves)?;
            state(world, &path)
        }
        "undo" | "redo" => {
            let direction = if cmd == "undo" {
                PatchDirection::Reverse
            } else {
                PatchDirection::Forward
            };
            session::step(doc_mut(&mut world.docs, &path)?, direction)?;
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

fn doc_mut<'a>(docs: &'a mut HashMap<String, Document>, path: &str) -> Result<&'a mut Document, String> {
    docs.get_mut(path)
        .ok_or_else(|| format!("no open document for {path}"))
}

fn state(world: &mut World, path: &str) -> Result<Value, String> {
    let doc = doc_mut(&mut world.docs, path)?;
    serde_json::to_value(session::snapshot(path, doc, &world.palette))
        .map_err(|cause| cause.to_string())
}

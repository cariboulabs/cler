//! The document session every edition shares: source, undo history and the ui cache,
//! with no filesystem underneath. Desktop and browser adapters own where the bytes live.

use serde::{Deserialize, Serialize};
use serde_json::{json, Map, Value};
use sha2::{Digest, Sha256};

use crate::{
    extract_specs, first_fault, unified, ActionQueue, ApplyError, BlockSpec, Command,
    DocumentSession, FileModel, ParseFault, PatchDirection, SourcePatch, Transaction,
    SCHEMA_VERSION,
};

pub const NEW_DOCUMENT_TEMPLATE: &str = r#"#include "cler.hpp"
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

pub struct Document {
    pub session: DocumentSession,
    pub saved: String,
    pub ui: Value,
    pub external_change: bool,
    history: ActionQueue<Action>,
}

#[derive(Clone)]
enum Action {
    Source(SourcePatch),
    MoveNodes { view: String, moves: Vec<NodeMove> },
}

#[derive(Clone, Debug, Deserialize, PartialEq)]
#[serde(rename_all = "camelCase")]
pub struct Point {
    pub x: f64,
    pub y: f64,
}

#[derive(Clone, Debug, Deserialize, PartialEq)]
#[serde(rename_all = "camelCase")]
pub struct NodeMove {
    pub node: String,
    pub from: Point,
    pub to: Point,
}

#[derive(Debug, Serialize)]
pub struct DocumentModel {
    pub sha256: String,
    #[serde(rename = "hasErrors")]
    pub has_errors: bool,
    #[serde(flatten)]
    pub model: FileModel,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DocumentState {
    pub path: String,
    pub revision: u64,
    pub model: DocumentModel,
    pub source: String,
    pub can_undo: bool,
    pub can_redo: bool,
    pub dirty: bool,
    pub external_change: bool,
    pub cache: Value,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct EditOutcome {
    pub state: DocumentState,
    pub unparsed: bool,
    pub fault: Option<ParseFault>,
}

#[derive(Debug, Serialize)]
pub struct Summary {
    pub splices: usize,
}

#[derive(Debug, Serialize)]
pub struct Preview {
    pub diff: String,
    pub summary: Summary,
}

#[derive(Debug)]
pub enum Edited {
    Applied,
    Unparsed(Option<ParseFault>),
}

impl Document {
    pub fn new(session: DocumentSession, saved: String, ui: Value) -> Self {
        Document {
            session,
            saved,
            ui,
            external_change: false,
            history: ActionQueue::default(),
        }
    }

    pub fn load(source: String) -> Result<Self, crate::Error> {
        let session = DocumentSession::load(source.clone())?;
        Ok(Document::new(session, source, Value::Object(Map::new())))
    }

    pub fn reload(&mut self, text: String) -> Result<(), String> {
        if text != self.session.source() {
            self.session
                .reload(text.clone())
                .map_err(|cause| cause.to_string())?;
            self.history.clear();
        }
        self.saved = text;
        Ok(())
    }
}

pub fn apply(
    doc: &mut Document,
    base_revision: u64,
    commands: Vec<Command>,
    includes: &[String],
) -> Result<(), String> {
    let pending = doc
        .session
        .preview_with_includes(
            Transaction {
                version: SCHEMA_VERSION.to_string(),
                base_revision,
                commands,
            },
            includes,
        )
        .map_err(|cause| cause.to_string())?;
    let patch = pending
        .changes()
        .then(|| {
            pending
                .patch()
                .cloned()
                .ok_or_else(|| ApplyError::HistoryMismatch.to_string())
        })
        .transpose()?;
    doc.session.commit(pending);
    if let Some(patch) = patch {
        doc.history.push(Action::Source(patch));
    }
    Ok(())
}

pub fn preview(
    doc: &Document,
    base_revision: u64,
    commands: Vec<Command>,
    includes: &[String],
) -> Result<Preview, String> {
    let pending = doc
        .session
        .preview_with_includes(
            Transaction {
                version: SCHEMA_VERSION.to_string(),
                base_revision,
                commands,
            },
            includes,
        )
        .map_err(|cause| cause.to_string())?;
    Ok(Preview {
        diff: unified(doc.session.source(), pending.source(), pending.splices()),
        summary: Summary {
            splices: pending.splices().len(),
        },
    })
}

pub fn edit(doc: &mut Document, base_revision: u64, source: &str) -> Result<Edited, String> {
    if base_revision != doc.session.revision() {
        return Err(ApplyError::RevisionMismatch {
            base_revision,
            current_revision: doc.session.revision(),
        }
        .to_string());
    }
    let pending = match doc.session.preview_text(source.to_string()) {
        Ok(pending) => pending,
        Err(ApplyError::ProducesParseError) => return Ok(Edited::Unparsed(first_fault(source))),
        Err(cause) => return Err(cause.to_string()),
    };
    let patch = pending
        .changes()
        .then(|| {
            pending
                .patch()
                .cloned()
                .ok_or_else(|| ApplyError::HistoryMismatch.to_string())
        })
        .transpose()?;
    doc.session.commit(pending);
    if let Some(patch) = patch {
        doc.history.push(Action::Source(patch));
    }
    Ok(Edited::Applied)
}

pub fn move_nodes(doc: &mut Document, view: &str, moves: Vec<NodeMove>) -> Result<(), String> {
    if view.is_empty() {
        return Err("node movement requires a view".to_string());
    }
    let mut moves: Vec<NodeMove> = moves
        .into_iter()
        .filter(|movement| movement.from != movement.to)
        .collect();
    let mut nodes = std::collections::HashSet::new();
    for movement in &moves {
        if movement.node.is_empty() {
            return Err("node movement requires a node".to_string());
        }
        if !movement.from.x.is_finite()
            || !movement.from.y.is_finite()
            || !movement.to.x.is_finite()
            || !movement.to.y.is_finite()
        {
            return Err("node movement requires finite coordinates".to_string());
        }
        if !nodes.insert(movement.node.clone()) {
            return Err(format!("node movement repeats {}", movement.node));
        }
    }
    if moves.is_empty() {
        return Ok(());
    }
    moves.sort_by(|left, right| left.node.cmp(&right.node));
    apply_node_moves(&mut doc.ui, view, &moves, PatchDirection::Forward);
    doc.history.push(Action::MoveNodes {
        view: view.to_string(),
        moves,
    });
    Ok(())
}

/// Whether the next history entry in this direction rewrites the source — the desktop
/// gates those on the file not having drifted, and lets position moves through.
/// `None` when there is nothing left to walk.
pub fn pending_action_edits_source(doc: &Document, direction: PatchDirection) -> Option<bool> {
    let action = match direction {
        PatchDirection::Reverse => doc.history.undo(),
        PatchDirection::Forward => doc.history.redo(),
    }?;
    Some(matches!(action, Action::Source(_)))
}

pub fn step(doc: &mut Document, direction: PatchDirection) -> Result<(), String> {
    let action = match direction {
        PatchDirection::Reverse => doc.history.undo(),
        PatchDirection::Forward => doc.history.redo(),
    }
    .cloned();
    let Some(action) = action else {
        return Err(match direction {
            PatchDirection::Reverse => ApplyError::NothingToUndo.to_string(),
            PatchDirection::Forward => ApplyError::NothingToRedo.to_string(),
        });
    };
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
        Action::MoveNodes { view, moves } => apply_node_moves(&mut doc.ui, &view, &moves, direction),
    }
    match direction {
        PatchDirection::Reverse => doc.history.commit_undo(),
        PatchDirection::Forward => doc.history.commit_redo(),
    }
    Ok(())
}

pub fn model(doc: &Document, palette: &[BlockSpec]) -> DocumentModel {
    let model = doc.session.parse_with_palette(palette);
    DocumentModel {
        sha256: format!("{:x}", Sha256::digest(doc.session.source().as_bytes())),
        has_errors: model.has_errors,
        model,
    }
}

pub fn snapshot(path: &str, doc: &Document, palette: &[BlockSpec]) -> DocumentState {
    DocumentState {
        path: path.to_string(),
        revision: doc.session.revision(),
        model: model(doc, palette),
        source: doc.session.source().to_string(),
        can_undo: doc.history.can_undo(),
        can_redo: doc.history.can_redo(),
        dirty: doc.session.source() != doc.saved,
        external_change: doc.external_change,
        cache: doc.ui.clone(),
    }
}

/// The blocks declared in this document shadow same-named palette entries.
pub fn palette(doc: &Document, origin: &str, palette: &[BlockSpec]) -> Vec<BlockSpec> {
    let mut specs = extract_specs(doc.session.source(), origin);
    let local: Vec<String> = specs.iter().map(|spec| spec.name.clone()).collect();
    specs.extend(
        palette
            .iter()
            .filter(|spec| !local.contains(&spec.name))
            .cloned(),
    );
    specs
}

/// The palette origins the `add_block` commands in this transaction need included.
/// Where those origins become include paths is the caller's edition: the browser
/// registers them repo-relative already, the desktop resolves them against its roots.
pub fn add_block_origins(commands: &[Command], palette: &[BlockSpec]) -> Vec<String> {
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

/// The document commands, with their JSON arguments already read. Both editions parse
/// requests here; what they do with one differs (the desktop has files to keep in step).
pub enum Request {
    Apply { base_revision: u64, commands: Vec<Value> },
    Preview { base_revision: u64, commands: Vec<Value> },
    Edit { base_revision: u64, source: String },
    MoveNodes { view: String, moves: Vec<NodeMove> },
    Undo,
    Redo,
    Palette,
    SaveCache { ui: Value },
    SaveAs { new_path: String },
}

/// Arguments the caller got wrong — a bug in the frontend, not a refusal of the edit.
pub struct Malformed(pub String);

impl Request {
    pub fn parse(cmd: &str, args: &Value) -> Result<Option<Request>, Malformed> {
        let request = match cmd {
            "apply_commands" => Request::Apply {
                base_revision: base_revision(cmd, args)?,
                commands: commands(cmd, args)?,
            },
            "preview_commands" => Request::Preview {
                base_revision: base_revision(cmd, args)?,
                commands: commands(cmd, args)?,
            },
            "edit_source" => Request::Edit {
                base_revision: base_revision(cmd, args)?,
                source: args
                    .get("source")
                    .and_then(Value::as_str)
                    .ok_or_else(|| Malformed("edit_source needs baseRevision and source".to_string()))?
                    .to_string(),
            },
            "move_nodes" => Request::MoveNodes {
                view: args
                    .get("view")
                    .and_then(Value::as_str)
                    .ok_or_else(|| Malformed("move_nodes needs view and moves".to_string()))?
                    .to_string(),
                moves: serde_json::from_value(args.get("moves").cloned().unwrap_or(Value::Null))
                    .map_err(|_| Malformed("move_nodes needs view and moves".to_string()))?,
            },
            "undo" => Request::Undo,
            "redo" => Request::Redo,
            "palette" => Request::Palette,
            "save_cache" => Request::SaveCache {
                ui: args.get("ui").cloned().unwrap_or(Value::Null),
            },
            "save_document_as" => Request::SaveAs {
                new_path: args
                    .get("newPath")
                    .and_then(Value::as_str)
                    .ok_or_else(|| {
                        Malformed("save_document_as needs a newPath argument".to_string())
                    })?
                    .to_string(),
            },
            _ => return Ok(None),
        };
        Ok(Some(request))
    }
}

fn base_revision(cmd: &str, args: &Value) -> Result<u64, Malformed> {
    args.get("base_revision")
        .or_else(|| args.get("baseRevision"))
        .and_then(Value::as_u64)
        .ok_or_else(|| Malformed(format!("{cmd} needs baseRevision and commands")))
}

fn commands(cmd: &str, args: &Value) -> Result<Vec<Value>, Malformed> {
    args.get("commands")
        .and_then(Value::as_array)
        .cloned()
        .ok_or_else(|| Malformed(format!("{cmd} needs baseRevision and commands")))
}

/// Runs a request against the session alone — the browser edition end to end.
pub fn command(
    doc: &mut Document,
    specs: &[BlockSpec],
    path: &str,
    request: Request,
) -> Result<Value, String> {
    match request {
        Request::Apply {
            base_revision,
            commands,
        } => {
            let commands: Vec<Command> = serde_json::from_value(Value::Array(commands))
                .map_err(|cause| cause.to_string())?;
            let includes = add_block_origins(&commands, specs);
            apply(doc, base_revision, commands, &includes)?;
            serde_json::to_value(snapshot(path, doc, specs)).map_err(|cause| cause.to_string())
        }
        Request::Preview {
            base_revision,
            commands,
        } => {
            let commands: Vec<Command> = serde_json::from_value(Value::Array(commands))
                .map_err(|cause| cause.to_string())?;
            let includes = add_block_origins(&commands, specs);
            serde_json::to_value(preview(doc, base_revision, commands, &includes)?)
                .map_err(|cause| cause.to_string())
        }
        Request::Edit {
            base_revision,
            source,
        } => {
            let (unparsed, fault) = match edit(doc, base_revision, &source)? {
                Edited::Applied => (false, None),
                Edited::Unparsed(fault) => (true, fault),
            };
            serde_json::to_value(EditOutcome {
                unparsed,
                fault,
                state: snapshot(path, doc, &[]),
            })
            .map_err(|cause| cause.to_string())
        }
        Request::MoveNodes { view, moves } => {
            move_nodes(doc, &view, moves)?;
            serde_json::to_value(snapshot(path, doc, specs)).map_err(|cause| cause.to_string())
        }
        Request::Undo => {
            step(doc, PatchDirection::Reverse)?;
            serde_json::to_value(snapshot(path, doc, specs)).map_err(|cause| cause.to_string())
        }
        Request::Redo => {
            step(doc, PatchDirection::Forward)?;
            serde_json::to_value(snapshot(path, doc, specs)).map_err(|cause| cause.to_string())
        }
        Request::Palette => {
            serde_json::to_value(palette(doc, path, specs)).map_err(|cause| cause.to_string())
        }
        Request::SaveCache { ui } => {
            doc.ui = ui;
            Ok(Value::Null)
        }
        Request::SaveAs { .. } => Err("saving under a new name needs a filesystem".to_string()),
    }
}

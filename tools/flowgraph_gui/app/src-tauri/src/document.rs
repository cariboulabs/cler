use std::collections::hash_map::Entry;
use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::sync::{Mutex, MutexGuard};

use cler_graph::{Command, DocumentSession, FileModel, Transaction, SCHEMA_VERSION};
use serde::Serialize;
use serde_json::Value;
use sha2::{Digest, Sha256};

pub type Documents = Mutex<HashMap<PathBuf, Document>>;

pub struct Document {
    session: DocumentSession,
    written: String,
    external_change: bool,
    undo_depth: usize,
    redo_depth: usize,
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
    pub can_undo: bool,
    pub can_redo: bool,
    pub external_change: bool,
}

enum Step {
    Undo,
    Redo,
}

pub fn canonical(path: &str) -> Result<PathBuf, String> {
    Path::new(path)
        .canonicalize()
        .map_err(|cause| format!("cannot resolve {path}: {cause}"))
}

pub fn open(docs: &Documents, path: &str) -> Result<DocumentState, String> {
    let target = canonical(path)?;
    let mut map = lock(docs)?;
    let doc = match map.entry(target.clone()) {
        Entry::Occupied(existing) => existing.into_mut(),
        Entry::Vacant(slot) => slot.insert(fresh(&target)?),
    };
    Ok(snapshot(&target, doc))
}

pub fn close(docs: &Documents, path: &str) -> Result<(), String> {
    let target = canonical(path)?;
    lock(docs)?.remove(&target);
    Ok(())
}

pub fn apply(docs: &Documents, path: &str, commands: Vec<Value>) -> Result<DocumentState, String> {
    let target = canonical(path)?;
    let commands: Vec<Command> =
        serde_json::from_value(Value::Array(commands)).map_err(|cause| cause.to_string())?;
    let mut map = lock(docs)?;
    let doc = document(&mut map, &target)?;
    refuse_external(doc, &target)?;

    let base_revision = doc.session.revision();
    let transaction = Transaction {
        version: SCHEMA_VERSION.to_string(),
        base_revision,
        commands,
    };
    let outcome = doc
        .session
        .apply(transaction)
        .map_err(|cause| cause.to_string())?;
    if outcome.revision != base_revision {
        doc.undo_depth += 1;
        doc.redo_depth = 0;
        write(doc, &target)?;
    }
    Ok(snapshot(&target, doc))
}

pub fn undo(docs: &Documents, path: &str) -> Result<DocumentState, String> {
    step(docs, path, Step::Undo)
}

pub fn redo(docs: &Documents, path: &str) -> Result<DocumentState, String> {
    step(docs, path, Step::Redo)
}

pub fn reload(docs: &Documents, path: &str) -> Result<DocumentState, String> {
    let target = canonical(path)?;
    let text = read(&target)?;
    let mut map = lock(docs)?;
    let doc = document(&mut map, &target)?;
    doc.session
        .reload(text.clone())
        .map_err(|cause| cause.to_string())?;
    doc.written = text;
    doc.external_change = false;
    doc.undo_depth = 0;
    doc.redo_depth = 0;
    Ok(snapshot(&target, doc))
}

pub fn parse_file(path: &str) -> Result<String, String> {
    let target = canonical(path)?;
    let session = DocumentSession::open(&target).map_err(|cause| cause.to_string())?;
    serde_json::to_string(&model_of(&session)).map_err(|cause| cause.to_string())
}

pub fn note_disk_event(docs: &Documents, path: &Path) -> bool {
    let Ok(mut map) = docs.lock() else {
        return false;
    };
    let Some(doc) = map.get_mut(path) else {
        return false;
    };
    let Ok(bytes) = std::fs::read(path) else {
        return false;
    };
    if bytes == doc.written.as_bytes() {
        return false;
    }
    let first_notice = !doc.external_change;
    doc.external_change = true;
    first_notice
}

fn step(docs: &Documents, path: &str, step: Step) -> Result<DocumentState, String> {
    let target = canonical(path)?;
    let mut map = lock(docs)?;
    let doc = document(&mut map, &target)?;
    refuse_external(doc, &target)?;
    match step {
        Step::Undo => {
            doc.session.undo().map_err(|cause| cause.to_string())?;
            doc.undo_depth = doc.undo_depth.saturating_sub(1);
            doc.redo_depth += 1;
        }
        Step::Redo => {
            doc.session.redo().map_err(|cause| cause.to_string())?;
            doc.redo_depth = doc.redo_depth.saturating_sub(1);
            doc.undo_depth += 1;
        }
    }
    write(doc, &target)?;
    Ok(snapshot(&target, doc))
}

fn fresh(target: &Path) -> Result<Document, String> {
    let session = DocumentSession::open(target).map_err(|cause| cause.to_string())?;
    let written = session.source().to_string();
    Ok(Document {
        session,
        written,
        external_change: false,
        undo_depth: 0,
        redo_depth: 0,
    })
}

fn lock(docs: &Documents) -> Result<MutexGuard<'_, HashMap<PathBuf, Document>>, String> {
    docs.lock()
        .map_err(|_| "the document store lock is poisoned".to_string())
}

fn document<'a>(
    map: &'a mut HashMap<PathBuf, Document>,
    target: &Path,
) -> Result<&'a mut Document, String> {
    map.get_mut(target)
        .ok_or_else(|| format!("no open document for {}", target.display()))
}

fn snapshot(target: &Path, doc: &Document) -> DocumentState {
    DocumentState {
        path: target.display().to_string(),
        revision: doc.session.revision(),
        model: model_of(&doc.session),
        can_undo: doc.undo_depth > 0,
        can_redo: doc.redo_depth > 0,
        external_change: doc.external_change,
    }
}

fn model_of(session: &DocumentSession) -> DocumentModel {
    let model = session.parse();
    DocumentModel {
        sha256: format!("{:x}", Sha256::digest(session.source().as_bytes())),
        has_errors: model.has_errors,
        model,
    }
}

fn refuse_external(doc: &mut Document, target: &Path) -> Result<(), String> {
    let bytes = std::fs::read(target)
        .map_err(|cause| format!("cannot read {}: {cause}", target.display()))?;
    if bytes == doc.written.as_bytes() {
        return Ok(());
    }
    doc.external_change = true;
    Err(format!(
        "{} changed on disk since the last write; reload before editing",
        target.display()
    ))
}

fn write(doc: &mut Document, target: &Path) -> Result<(), String> {
    write_atomic(target, doc.session.source())
        .map_err(|cause| format!("cannot write {}: {cause}", target.display()))?;
    doc.written = doc.session.source().to_string();
    doc.external_change = false;
    Ok(())
}

fn read(target: &Path) -> Result<String, String> {
    std::fs::read_to_string(target)
        .map_err(|cause| format!("cannot read {}: {cause}", target.display()))
}

fn write_atomic(target: &Path, contents: &str) -> std::io::Result<()> {
    std::fs::OpenOptions::new().write(true).open(target)?;
    let original = std::fs::metadata(target)?;
    let name = target
        .file_name()
        .map(|name| name.to_string_lossy().to_string())
        .unwrap_or_else(|| "document".to_string());
    let temp = target.with_file_name(format!(".{name}.cler-gui.tmp"));
    std::fs::write(&temp, contents)?;
    std::fs::set_permissions(&temp, original.permissions())?;
    inherit_owner(&original, &temp);
    std::fs::rename(&temp, target)
}

#[cfg(unix)]
fn inherit_owner(original: &std::fs::Metadata, temp: &Path) {
    use std::os::unix::fs::MetadataExt;
    std::os::unix::fs::chown(temp, Some(original.uid()), Some(original.gid())).ok();
}

#[cfg(not(unix))]
fn inherit_owner(_original: &std::fs::Metadata, _temp: &Path) {}

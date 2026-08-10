use std::collections::hash_map::RandomState;
use std::collections::HashMap;
use std::hash::{BuildHasher, Hasher};
use std::io::Write;
use std::path::{Path, PathBuf};
use std::sync::{Mutex, MutexGuard, PoisonError};

use cler_graph::{
    palette_specs, BlockSpec, Command, DocumentSession, FileModel, Transaction, SCHEMA_VERSION,
};
use serde::Serialize;
use serde_json::Value;
use sha2::{Digest, Sha256};

pub type Documents = Mutex<HashMap<PathBuf, Document>>;

const PALETTE_DIR: &str = "desktop_blocks";

pub struct Document {
    session: DocumentSession,
    spelling: String,
    written: String,
    external_change: bool,
    palette: Vec<BlockSpec>,
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

enum WriteFailure {
    Drift,
    Io(std::io::Error),
}

pub fn canonical(path: &str) -> Result<PathBuf, String> {
    Path::new(path)
        .canonicalize()
        .map_err(|cause| format!("cannot resolve {path}: {cause}"))
}

pub fn open(docs: &Documents, path: &str) -> Result<DocumentState, String> {
    let mut map = lock(docs);
    if let Ok((key, doc)) = document(&mut map, path) {
        revalidate(doc, &key);
        return Ok(snapshot(&key, doc));
    }
    let target = canonical(path)?;
    let doc = fresh(&target, path)?;
    let state = snapshot(&target, &doc);
    map.insert(target, doc);
    Ok(state)
}

pub fn close(docs: &Documents, path: &str) -> Option<PathBuf> {
    let mut map = lock(docs);
    let key = resolve(&map, path)?;
    map.remove(&key);
    let dir = key.parent()?.to_path_buf();
    map.keys()
        .all(|other| other.parent() != Some(dir.as_path()))
        .then_some(dir)
}

pub fn apply(
    docs: &Documents,
    path: &str,
    base_revision: u64,
    commands: Vec<Value>,
) -> Result<DocumentState, String> {
    let commands: Vec<Command> =
        serde_json::from_value(Value::Array(commands)).map_err(|cause| cause.to_string())?;
    let mut map = lock(docs);
    let (target, doc) = document(&mut map, path)?;
    refuse_external(doc, &target)?;

    let pending = doc
        .session
        .preview(Transaction {
            version: SCHEMA_VERSION.to_string(),
            base_revision,
            commands,
        })
        .map_err(|cause| cause.to_string())?;
    if pending.changes() {
        persist(doc, &target, pending.source())?;
    }
    doc.session.commit(pending);
    Ok(snapshot(&target, doc))
}

pub fn undo(docs: &Documents, path: &str) -> Result<DocumentState, String> {
    step(docs, path, Step::Undo)
}

pub fn redo(docs: &Documents, path: &str) -> Result<DocumentState, String> {
    step(docs, path, Step::Redo)
}

pub fn reload(docs: &Documents, path: &str) -> Result<DocumentState, String> {
    let mut map = lock(docs);
    let (target, doc) = document(&mut map, path)?;
    let text = read(&target)?;
    if text != doc.session.source() {
        doc.session
            .reload(text.clone())
            .map_err(|cause| cause.to_string())?;
    }
    doc.written = text;
    doc.external_change = false;
    Ok(snapshot(&target, doc))
}

pub fn parse_file(docs: &Documents, path: &str) -> Result<String, String> {
    let map = lock(docs);
    if let Some(doc) = resolve(&map, path).and_then(|key| map.get(&key)) {
        return serde_json::to_string(&model_of(&doc.session, &doc.palette))
            .map_err(|cause| cause.to_string());
    }
    drop(map);
    let target = canonical(path)?;
    let session = DocumentSession::open(&target).map_err(|cause| cause.to_string())?;
    serde_json::to_string(&model_of(&session, &nearby_palette(&target)))
        .map_err(|cause| cause.to_string())
}

pub fn note_disk_event(docs: &Documents, path: &Path) -> bool {
    let mut map = lock(docs);
    let Some(doc) = map.get_mut(path) else {
        return false;
    };
    let quiet = !doc.external_change;
    revalidate(doc, path);
    doc.external_change && quiet
}

fn step(docs: &Documents, path: &str, step: Step) -> Result<DocumentState, String> {
    let mut map = lock(docs);
    let (target, doc) = document(&mut map, path)?;
    refuse_external(doc, &target)?;
    let pending = match step {
        Step::Undo => doc.session.preview_undo(),
        Step::Redo => doc.session.preview_redo(),
    }
    .map_err(|cause| cause.to_string())?;
    persist(doc, &target, pending.source())?;
    doc.session.commit(pending);
    Ok(snapshot(&target, doc))
}

fn fresh(target: &Path, spelling: &str) -> Result<Document, String> {
    let session = DocumentSession::open(target).map_err(|cause| cause.to_string())?;
    let written = session.source().to_string();
    Ok(Document {
        session,
        spelling: spelling.to_string(),
        written,
        external_change: false,
        palette: nearby_palette(target),
    })
}

fn nearby_palette(target: &Path) -> Vec<BlockSpec> {
    let Some(root) = target
        .ancestors()
        .map(|dir| dir.join(PALETTE_DIR))
        .find(|dir| dir.is_dir())
    else {
        return Vec::new();
    };
    palette_specs(&[root]).unwrap_or_default()
}

fn lock(docs: &Documents) -> MutexGuard<'_, HashMap<PathBuf, Document>> {
    docs.lock().unwrap_or_else(PoisonError::into_inner)
}

fn resolve(map: &HashMap<PathBuf, Document>, path: &str) -> Option<PathBuf> {
    if let Ok(target) = canonical(path) {
        if map.contains_key(&target) {
            return Some(target);
        }
    }
    let given = Path::new(path);
    map.iter()
        .find(|(key, doc)| key.as_path() == given || doc.spelling == path)
        .map(|(key, _)| key.clone())
}

fn document<'a>(
    map: &'a mut HashMap<PathBuf, Document>,
    path: &str,
) -> Result<(PathBuf, &'a mut Document), String> {
    let missing = || format!("no open document for {path}");
    let key = resolve(map, path).ok_or_else(missing)?;
    let doc = map.get_mut(&key).ok_or_else(missing)?;
    Ok((key, doc))
}

fn snapshot(target: &Path, doc: &Document) -> DocumentState {
    DocumentState {
        path: target.display().to_string(),
        revision: doc.session.revision(),
        model: model_of(&doc.session, &doc.palette),
        can_undo: doc.session.can_undo(),
        can_redo: doc.session.can_redo(),
        external_change: doc.external_change,
    }
}

fn model_of(session: &DocumentSession, palette: &[BlockSpec]) -> DocumentModel {
    let model = session.parse_with_palette(palette);
    DocumentModel {
        sha256: format!("{:x}", Sha256::digest(session.source().as_bytes())),
        has_errors: model.has_errors,
        model,
    }
}

fn revalidate(doc: &mut Document, target: &Path) {
    doc.external_change =
        !matches!(std::fs::read(target), Ok(bytes) if bytes == doc.written.as_bytes());
}

fn refuse_external(doc: &mut Document, target: &Path) -> Result<(), String> {
    let bytes = std::fs::read(target)
        .map_err(|cause| format!("cannot read {}: {cause}", target.display()))?;
    if bytes == doc.written.as_bytes() {
        return Ok(());
    }
    doc.external_change = true;
    Err(drifted(target))
}

fn drifted(target: &Path) -> String {
    format!(
        "{} changed on disk since the last write; reload before editing",
        target.display()
    )
}

fn persist(doc: &mut Document, target: &Path, contents: &str) -> Result<(), String> {
    match write_atomic(target, contents, &doc.written) {
        Ok(()) => {
            doc.written = contents.to_string();
            doc.external_change = false;
            Ok(())
        }
        Err(WriteFailure::Drift) => {
            doc.external_change = true;
            Err(drifted(target))
        }
        Err(WriteFailure::Io(cause)) => Err(format!("cannot write {}: {cause}", target.display())),
    }
}

fn read(target: &Path) -> Result<String, String> {
    std::fs::read_to_string(target)
        .map_err(|cause| format!("cannot read {}: {cause}", target.display()))
}

fn write_atomic(target: &Path, contents: &str, expected: &str) -> Result<(), WriteFailure> {
    let original = std::fs::metadata(target).map_err(WriteFailure::Io)?;
    std::fs::OpenOptions::new()
        .write(true)
        .open(target)
        .map_err(WriteFailure::Io)?;
    let temp = temp_sibling(target);
    let file = std::fs::OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(&temp)
        .map_err(WriteFailure::Io)?;
    let staged = stage(file, &original, contents, target, expected);
    if staged.is_err() {
        std::fs::remove_file(&temp).ok();
        return staged;
    }
    std::fs::rename(&temp, target).map_err(WriteFailure::Io)?;
    sync_parent(target);
    Ok(())
}

fn stage(
    mut file: std::fs::File,
    original: &std::fs::Metadata,
    contents: &str,
    target: &Path,
    expected: &str,
) -> Result<(), WriteFailure> {
    file.write_all(contents.as_bytes())
        .map_err(WriteFailure::Io)?;
    file.sync_all().map_err(WriteFailure::Io)?;
    file.set_permissions(original.permissions())
        .map_err(WriteFailure::Io)?;
    inherit_owner(original, &file);
    if std::fs::read(target).map_err(WriteFailure::Io)? != expected.as_bytes() {
        return Err(WriteFailure::Drift);
    }
    Ok(())
}

fn temp_sibling(target: &Path) -> PathBuf {
    let name = target
        .file_name()
        .map(|name| name.to_string_lossy().to_string())
        .unwrap_or_else(|| "document".to_string());
    let token = RandomState::new().build_hasher().finish();
    target.with_file_name(format!(".{name}.{token:016x}.cler-gui.tmp"))
}

fn sync_parent(target: &Path) {
    if let Some(dir) = target.parent() {
        if let Ok(handle) = std::fs::File::open(dir) {
            handle.sync_all().ok();
        }
    }
}

#[cfg(unix)]
fn inherit_owner(original: &std::fs::Metadata, temp: &std::fs::File) {
    use std::os::unix::fs::MetadataExt;
    use std::os::unix::io::AsFd;
    std::os::unix::fs::fchown(temp.as_fd(), Some(original.uid()), Some(original.gid())).ok();
}

#[cfg(not(unix))]
fn inherit_owner(_original: &std::fs::Metadata, _temp: &std::fs::File) {}

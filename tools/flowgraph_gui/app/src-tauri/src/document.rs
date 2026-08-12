use std::collections::hash_map::RandomState;
use std::collections::{HashMap, HashSet};
use std::hash::{BuildHasher, Hasher};
use std::io::Write;
use std::path::{Path, PathBuf};
use std::sync::{Mutex, MutexGuard, PoisonError};
use std::time::{Duration, SystemTime};

use cler_graph::{
    block_requirements, extract_specs, palette_specs, ActionQueue, ApplyError, BlockSpec, Command,
    DocumentSession, FileModel, PatchDirection, SourcePatch, Splice, Transaction, SCHEMA_VERSION,
};
use serde::{Deserialize, Serialize};
use serde_json::{json, Map, Value};
use sha2::{Digest, Sha256};

use crate::provenance::{ArtifactCatalog, ArtifactRecord};

pub type Documents = Mutex<HashMap<PathBuf, Document>>;

const PALETTE_DIR: &str = "desktop_blocks";
const REPOSITORY_MARKER: &str = "include/cler.hpp";
const DIFF_CONTEXT: usize = 3;
const CACHE_FORMAT: &str = "cler-flowgraph-cache";
const CACHE_VERSION: u32 = 1;
const SNAPSHOT_LIMIT: usize = 16;
const SNAPSHOT_MIN_AGE: Duration = Duration::from_secs(60 * 60);

#[derive(Clone, Debug)]
pub struct DraftState {
    pub path: PathBuf,
    pub workspace: PathBuf,
    pub sha256: String,
    pub requirements: BuildRequirements,
    pub artifacts: ArtifactCatalog,
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct BuildRequirements {
    pub origins: Vec<String>,
    pub exact: bool,
}

pub struct Document {
    session: DocumentSession,
    spelling: String,
    saved: String,
    working: PathBuf,
    cache_path: PathBuf,
    cache: CacheFile,
    external_change: bool,
    palette: Vec<BlockSpec>,
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

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
struct DocumentCache {
    source_path: String,
    saved_sha256: String,
    #[serde(flatten)]
    extra: Map<String, Value>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
struct CacheFile {
    format: String,
    version: u32,
    #[serde(default)]
    document: DocumentCache,
    #[serde(default)]
    ui: Value,
    #[serde(default)]
    build: Value,
    #[serde(flatten)]
    extra: Map<String, Value>,
}

impl Default for CacheFile {
    fn default() -> Self {
        Self {
            format: CACHE_FORMAT.to_string(),
            version: CACHE_VERSION,
            document: DocumentCache::default(),
            ui: Value::Object(Map::new()),
            build: Value::Object(Map::new()),
            extra: Map::new(),
        }
    }
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
pub struct Summary {
    pub splices: usize,
}

#[derive(Debug, Serialize)]
pub struct Preview {
    pub diff: String,
    pub summary: Summary,
}

struct Hunk {
    first: usize,
    last: usize,
    shift: isize,
    delta: isize,
}

#[derive(Clone, Copy)]
enum HistoryDirection {
    Undo,
    Redo,
}

enum WriteFailure {
    Drift,
    Io(std::io::Error),
}

pub fn canonical(path: &str) -> Result<PathBuf, String> {
    let given = Path::new(path);
    given
        .canonicalize()
        .or_else(|cause| {
            if given.is_absolute() {
                return Err(cause);
            }
            repository_root().join(given).canonicalize()
        })
        .map_err(|cause| format!("cannot resolve {path}: {cause}"))
}

fn repository_root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .ancestors()
        .find(|dir| dir.join(REPOSITORY_MARKER).is_file())
        .map(Path::to_path_buf)
        .unwrap_or_else(|| PathBuf::from(env!("CARGO_MANIFEST_DIR")))
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

    let origins = add_block_origins(&commands, &doc.palette, &target);
    let pending = doc
        .session
        .preview_with_includes(Transaction {
            version: SCHEMA_VERSION.to_string(),
            base_revision,
            commands,
        }, &origins)
        .map_err(|cause| cause.to_string())?;
    if pending.changes() {
        let patch = pending
            .patch()
            .cloned()
            .ok_or_else(|| ApplyError::HistoryMismatch.to_string())?;
        write_working(doc, pending.source())?;
        doc.session.commit(pending);
        doc.history.push(Action::Source(patch));
    } else {
        doc.session.commit(pending);
    }
    Ok(snapshot(&target, doc))
}

pub fn preview(
    docs: &Documents,
    path: &str,
    base_revision: u64,
    commands: Vec<Value>,
) -> Result<Preview, String> {
    let commands: Vec<Command> =
        serde_json::from_value(Value::Array(commands)).map_err(|cause| cause.to_string())?;
    let mut map = lock(docs);
    let (target, doc) = document(&mut map, path)?;
    refuse_external(doc, &target)?;

    let origins = add_block_origins(&commands, &doc.palette, &target);
    let pending = doc
        .session
        .preview_with_includes(Transaction {
            version: SCHEMA_VERSION.to_string(),
            base_revision,
            commands,
        }, &origins)
        .map_err(|cause| cause.to_string())?;
    Ok(Preview {
        diff: unified(doc.session.source(), pending.source(), pending.splices()),
        summary: Summary {
            splices: pending.splices().len(),
        },
    })
}

fn add_block_origins(commands: &[Command], palette: &[BlockSpec], target: &Path) -> Vec<String> {
    let roots = include_roots(target);
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
        .filter_map(|origin| include_path(&origin, &roots))
        .collect()
}

fn include_roots(target: &Path) -> Vec<PathBuf> {
    let repo_blocks = crate::build::repo_root(target).map(|root| root.join(PALETTE_DIR));
    repo_blocks
        .into_iter()
        .chain(crate::settings::block_library_dirs())
        .collect()
}

fn include_path(origin: &str, roots: &[PathBuf]) -> Option<String> {
    let origin = Path::new(origin);
    for root in roots {
        let Some(parent) = root.parent() else {
            continue;
        };
        if let Ok(relative) = origin.strip_prefix(parent) {
            let mut segments = Vec::new();
            for part in relative.components() {
                segments.push(part.as_os_str().to_string_lossy().into_owned());
            }
            return Some(segments.join("/"));
        }
    }
    None
}

pub fn move_nodes(
    docs: &Documents,
    path: &str,
    view: String,
    moves: Vec<NodeMove>,
) -> Result<DocumentState, String> {
    if view.is_empty() {
        return Err("node movement requires a view".to_string());
    }
    let mut nodes = HashSet::new();
    let mut moves = moves
        .into_iter()
        .filter(|movement| movement.from != movement.to)
        .collect::<Vec<_>>();
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
        let mut map = lock(docs);
        let (target, doc) = document(&mut map, path)?;
        return Ok(snapshot(&target, doc));
    }
    moves.sort_by(|left, right| left.node.cmp(&right.node));
    let mut map = lock(docs);
    let (target, doc) = document(&mut map, path)?;
    let mut cache = doc.cache.clone();
    apply_node_moves(&mut cache.ui, &view, &moves, HistoryDirection::Redo);
    write_cache(&doc.cache_path, &cache)?;
    doc.cache = cache;
    doc.history.push(Action::MoveNodes { view, moves });
    Ok(snapshot(&target, doc))
}

fn line_starts(text: &str) -> Vec<usize> {
    let mut starts = vec![0];
    starts.extend(text.match_indices('\n').map(|(at, _)| at + 1));
    starts
}

fn line_of(starts: &[usize], offset: usize) -> usize {
    starts
        .partition_point(|start| *start <= offset)
        .saturating_sub(1)
}

fn hunks(starts: &[usize], splices: &[Splice]) -> Vec<Hunk> {
    let mut found: Vec<Hunk> = Vec::new();
    let mut shift = 0isize;
    for splice in splices {
        let first = line_of(starts, splice.start);
        let last = line_of(starts, splice.end);
        let delta = splice.text.len() as isize - (splice.end - splice.start) as isize;
        match found.last_mut() {
            Some(previous) if first <= previous.last + DIFF_CONTEXT * 2 => {
                previous.last = previous.last.max(last);
                previous.delta += delta;
            }
            _ => found.push(Hunk {
                first,
                last,
                shift,
                delta,
            }),
        }
        shift += delta;
    }
    found
}

fn unified(before: &str, after: &str, splices: &[Splice]) -> String {
    if splices.is_empty() || before == after {
        return String::new();
    }
    let starts = line_starts(before);
    let old: Vec<&str> = before.split('\n').collect();
    let mut out = String::new();
    for hunk in hunks(&starts, splices) {
        let from = hunk.first.saturating_sub(DIFF_CONTEXT);
        let to = (hunk.last + DIFF_CONTEXT).min(old.len() - 1);
        let opened = (starts[from] as isize + hunk.shift) as usize;
        let closed = (starts[to] as isize + old[to].len() as isize + hunk.shift + hunk.delta) as usize;
        let Some(window) = after.get(opened..closed) else {
            continue;
        };
        let fresh: Vec<&str> = window.split('\n').collect();
        let lead = hunk.first - from;
        let trail = to - hunk.last;
        let dropped = &old[hunk.first..=hunk.last];
        let gained = &fresh[lead..fresh.len() - trail];
        let head = shared(dropped.iter().zip(gained.iter()));
        let tail = shared(dropped[head..].iter().rev().zip(gained[head..].iter().rev()));

        out.push_str(&format!(
            "@@ -{},{} +{},{} @@\n",
            from + 1,
            to - from + 1,
            after[..opened].matches('\n').count() + 1,
            fresh.len()
        ));
        for line in old[from..hunk.first].iter().chain(&dropped[..head]) {
            out.push_str(&format!(" {line}\n"));
        }
        for line in &dropped[head..dropped.len() - tail] {
            out.push_str(&format!("-{line}\n"));
        }
        for line in &gained[head..gained.len() - tail] {
            out.push_str(&format!("+{line}\n"));
        }
        for line in dropped[dropped.len() - tail..]
            .iter()
            .chain(&old[hunk.last + 1..=to])
        {
            out.push_str(&format!(" {line}\n"));
        }
    }
    out
}

fn shared<'a>(pairs: impl Iterator<Item = (&'a &'a str, &'a &'a str)>) -> usize {
    pairs.take_while(|(left, right)| left == right).count()
}

pub fn undo(docs: &Documents, path: &str) -> Result<DocumentState, String> {
    step(docs, path, HistoryDirection::Undo)
}

pub fn redo(docs: &Documents, path: &str) -> Result<DocumentState, String> {
    step(docs, path, HistoryDirection::Redo)
}

pub fn save(docs: &Documents, path: &str) -> Result<DocumentState, String> {
    let mut map = lock(docs);
    let (target, doc) = document(&mut map, path)?;
    refuse_external(doc, &target)?;
    if doc.session.source() != doc.saved {
        let source = doc.session.source().to_string();
        persist(doc, &target, &source)?;
        update_cache_document(doc, &target);
    }
    Ok(snapshot(&target, doc))
}

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

pub fn create(docs: &Documents, path: &str) -> Result<DocumentState, String> {
    if Path::new(path).exists() {
        return Err(format!("{path} already exists"));
    }
    std::fs::write(path, NEW_DOCUMENT_TEMPLATE)
        .map_err(|cause| format!("cannot write {path}: {cause}"))?;
    open(docs, path)
}

pub fn save_as(docs: &Documents, path: &str, new_path: &str) -> Result<DocumentState, String> {
    let source = {
        let mut map = lock(docs);
        let (_, doc) = document(&mut map, path)?;
        doc.session.source().to_string()
    };
    std::fs::write(new_path, &source)
        .map_err(|cause| format!("cannot write {new_path}: {cause}"))?;
    open(docs, new_path)
}

pub fn reload(docs: &Documents, path: &str) -> Result<DocumentState, String> {
    let mut map = lock(docs);
    let (target, doc) = document(&mut map, path)?;
    let text = read(&target)?;
    if text != doc.session.source() {
        write_working(doc, &text)?;
        doc.session
            .reload(text.clone())
            .map_err(|cause| cause.to_string())?;
        doc.history.clear();
    }
    doc.saved = text;
    update_cache_document(doc, &target);
    doc.external_change = false;
    Ok(snapshot(&target, doc))
}

pub fn working_path(docs: &Documents, path: &str) -> Result<PathBuf, String> {
    let map = lock(docs);
    let key = resolve(&map, path).ok_or_else(|| format!("no open document for {path}"))?;
    map.get(&key)
        .map(|doc| doc.working.clone())
        .ok_or_else(|| format!("no open document for {path}"))
}

pub fn draft_state(docs: &Documents, path: &str) -> Result<DraftState, String> {
    let map = lock(docs);
    let key = resolve(&map, path).ok_or_else(|| format!("no open document for {path}"))?;
    let doc = map
        .get(&key)
        .ok_or_else(|| format!("no open document for {path}"))?;
    let workspace = doc
        .working
        .parent()
        .ok_or_else(|| format!("{} has no working directory", doc.working.display()))?
        .to_path_buf();
    Ok(DraftState {
        path: doc.working.clone(),
        workspace,
        sha256: digest(doc.session.source()),
        requirements: build_requirements(doc.session.source(), &doc.palette),
        artifacts: ArtifactCatalog::read(&doc.cache.build),
    })
}

pub fn snapshot_draft(docs: &Documents, path: &str) -> Result<DraftState, String> {
    let map = lock(docs);
    let key = resolve(&map, path).ok_or_else(|| format!("no open document for {path}"))?;
    let doc = map
        .get(&key)
        .ok_or_else(|| format!("no open document for {path}"))?;
    let source = doc.session.source();
    let sha256 = digest(source);
    let snapshots = doc
        .working
        .parent()
        .ok_or_else(|| format!("{} has no working directory", doc.working.display()))?
        .join("snapshots");
    private_dir(&snapshots)?;
    let extension = doc
        .working
        .extension()
        .and_then(|value| value.to_str())
        .unwrap_or("cpp");
    let snapshot = snapshots.join(format!("{sha256}.{extension}"));
    let current = matches!(std::fs::read_to_string(&snapshot), Ok(ref text) if text == source);
    if !current {
        let staged = snapshots.join(format!("{sha256}.{extension}.next"));
        std::fs::write(&staged, source)
            .map_err(|cause| format!("cannot write {}: {cause}", staged.display()))?;
        std::fs::rename(&staged, &snapshot)
            .map_err(|cause| format!("cannot replace {}: {cause}", snapshot.display()))?;
    }
    prune_snapshots(&snapshots, &snapshot);
    Ok(DraftState {
        path: snapshot,
        workspace: snapshots
            .parent()
            .ok_or_else(|| format!("{} has no workspace", snapshots.display()))?
            .to_path_buf(),
        sha256,
        requirements: build_requirements(source, &doc.palette),
        artifacts: ArtifactCatalog::read(&doc.cache.build),
    })
}

fn build_requirements(source: &str, palette: &[BlockSpec]) -> BuildRequirements {
    let requirements = block_requirements(source, palette);
    let exact = !requirements.needs_fallback();
    BuildRequirements {
        origins: requirements.origins,
        exact,
    }
}

pub fn record_artifact(
    docs: &Documents,
    path: &str,
    name: String,
    record: ArtifactRecord,
) -> Result<(), String> {
    let mut map = lock(docs);
    let (_, doc) = document(&mut map, path)?;
    let mut catalog = ArtifactCatalog::read(&doc.cache.build);
    catalog.prune_missing();
    catalog.put(name, record)?;
    doc.cache.build = serde_json::to_value(catalog).map_err(|cause| cause.to_string())?;
    write_cache(&doc.cache_path, &doc.cache)
}

pub fn store_cache(docs: &Documents, path: &str, ui: Value) -> Result<(), String> {
    let mut map = lock(docs);
    let (_, doc) = document(&mut map, path)?;
    doc.cache.ui = ui;
    write_cache(&doc.cache_path, &doc.cache)
}

fn apply_node_moves(
    ui: &mut Value,
    view: &str,
    moves: &[NodeMove],
    direction: HistoryDirection,
) {
    let views = object_field(object(ui), "views");
    let cached_view = object_field(views, view);
    let positions = object_field(cached_view, "positions");
    for movement in moves {
        let point = match direction {
            HistoryDirection::Undo => &movement.from,
            HistoryDirection::Redo => &movement.to,
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

pub fn palette(docs: &Documents, path: &str) -> Result<Vec<BlockSpec>, String> {
    let map = lock(docs);
    let key = resolve(&map, path).ok_or_else(|| format!("no open document for {path}"))?;
    let doc = map
        .get(&key)
        .ok_or_else(|| format!("no open document for {path}"))?;
    let mut specs = extract_specs(doc.session.source(), &key.display().to_string());
    let local: Vec<&str> = specs.iter().map(|spec| spec.name.as_str()).collect();
    let shipped: Vec<BlockSpec> = doc
        .palette
        .iter()
        .filter(|spec| !local.contains(&spec.name.as_str()))
        .cloned()
        .collect();
    specs.extend(shipped);
    Ok(specs)
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

fn step(
    docs: &Documents,
    path: &str,
    direction: HistoryDirection,
) -> Result<DocumentState, String> {
    let mut map = lock(docs);
    let (target, doc) = document(&mut map, path)?;
    let action = match direction {
        HistoryDirection::Undo => doc.history.undo(),
        HistoryDirection::Redo => doc.history.redo(),
    }
    .cloned();
    let Some(action) = action else {
        refuse_external(doc, &target)?;
        return Err(match direction {
            HistoryDirection::Undo => ApplyError::NothingToUndo.to_string(),
            HistoryDirection::Redo => ApplyError::NothingToRedo.to_string(),
        });
    };
    match action {
        Action::Source(patch) => {
            refuse_external(doc, &target)?;
            let patch_direction = match direction {
                HistoryDirection::Undo => PatchDirection::Reverse,
                HistoryDirection::Redo => PatchDirection::Forward,
            };
            let source = patch
                .apply(doc.session.source(), patch_direction)
                .map_err(|cause| cause.to_string())?;
            let pending = doc
                .session
                .preview_source(source)
                .map_err(|cause| cause.to_string())?;
            write_working(doc, pending.source())?;
            doc.session.commit(pending);
        }
        Action::MoveNodes { view, moves } => {
            let mut cache = doc.cache.clone();
            apply_node_moves(&mut cache.ui, &view, &moves, direction);
            write_cache(&doc.cache_path, &cache)?;
            doc.cache = cache;
        }
    }
    match direction {
        HistoryDirection::Undo => doc.history.commit_undo(),
        HistoryDirection::Redo => doc.history.commit_redo(),
    }
    Ok(snapshot(&target, doc))
}

fn fresh(target: &Path, spelling: &str) -> Result<Document, String> {
    let mut session = DocumentSession::open(target).map_err(|cause| cause.to_string())?;
    let saved = session.source().to_string();
    let (working, cache_path, mut cache, draft) = create_working(target, &saved)?;
    if draft != saved && session.reload(draft).is_err() {
        std::fs::write(&working, &saved)
            .map_err(|cause| format!("cannot write {}: {cause}", working.display()))?;
    }
    cache.document.source_path = target.display().to_string();
    cache.document.saved_sha256 = digest(&saved);
    let mut artifacts = ArtifactCatalog::read(&cache.build);
    if artifacts.prune_missing() {
        cache.build = serde_json::to_value(artifacts).map_err(|cause| cause.to_string())?;
    }
    write_cache(&cache_path, &cache)?;
    Ok(Document {
        session,
        spelling: spelling.to_string(),
        saved,
        working,
        cache_path,
        cache,
        external_change: false,
        palette: nearby_palette(target),
        history: ActionQueue::default(),
    })
}

fn nearby_palette(target: &Path) -> Vec<BlockSpec> {
    let repo_blocks = target
        .ancestors()
        .map(|dir| dir.join(PALETTE_DIR))
        .find(|dir| dir.is_dir())
        .or_else(|| {
            crate::settings::cler_root_fallback()
                .map(|root| root.join(PALETTE_DIR))
                .filter(|dir| dir.is_dir())
        });
    let mut roots: Vec<PathBuf> = repo_blocks.into_iter().collect();
    roots.extend(crate::settings::block_library_dirs());
    if roots.is_empty() {
        return Vec::new();
    }
    palette_specs(&roots).unwrap_or_default()
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
        source: doc.session.source().to_string(),
        can_undo: doc.history.can_undo(),
        can_redo: doc.history.can_redo(),
        dirty: doc.session.source() != doc.saved,
        external_change: doc.external_change,
        cache: doc.cache.ui.clone(),
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
        !matches!(std::fs::read(target), Ok(bytes) if bytes == doc.saved.as_bytes());
}

fn refuse_external(doc: &mut Document, target: &Path) -> Result<(), String> {
    let bytes = std::fs::read(target)
        .map_err(|cause| format!("cannot read {}: {cause}", target.display()))?;
    if bytes == doc.saved.as_bytes() {
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
    match write_atomic(target, contents, &doc.saved) {
        Ok(()) => {
            doc.saved = contents.to_string();
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

fn create_working(
    target: &Path,
    contents: &str,
) -> Result<(PathBuf, PathBuf, CacheFile, String), String> {
    let root = std::env::temp_dir().join(format!("cler-flowgraph-gui-{}", user_scope()));
    private_dir(&root)?;
    let dir = root.join(digest(&target.display().to_string()));
    private_dir(&dir)?;
    let name = target
        .file_name()
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("document.cpp"));
    let working = dir.join(name);
    let cache_name = target
        .file_stem()
        .and_then(|name| name.to_str())
        .map(|name| format!("{name}.cfgc"))
        .unwrap_or_else(|| "document.cfgc".to_string());
    let cache_path = dir.join(cache_name);
    let mut cache = read_cache(&cache_path).unwrap_or_default();
    if cache.format != CACHE_FORMAT {
        cache = CacheFile::default();
    }
    let reusable = cache.version >= 1
        && cache.document.source_path == target.display().to_string()
        && cache.document.saved_sha256 == digest(contents);
    let draft = reusable
        .then(|| read(&working).ok())
        .flatten()
        .unwrap_or_else(|| contents.to_string());
    std::fs::write(&working, &draft)
        .map_err(|cause| format!("cannot write {}: {cause}", working.display()))?;
    cache.format = CACHE_FORMAT.to_string();
    cache.version = cache.version.max(CACHE_VERSION);
    Ok((working, cache_path, cache, draft))
}

fn write_working(doc: &Document, contents: &str) -> Result<(), String> {
    let staged = doc.working.with_extension("cpp.next");
    std::fs::write(&staged, contents)
        .map_err(|cause| format!("cannot write {}: {cause}", staged.display()))?;
    std::fs::rename(&staged, &doc.working)
        .map_err(|cause| format!("cannot replace {}: {cause}", doc.working.display()))
}

fn update_cache_document(doc: &mut Document, target: &Path) {
    doc.cache.document.source_path = target.display().to_string();
    doc.cache.document.saved_sha256 = digest(&doc.saved);
    write_cache(&doc.cache_path, &doc.cache).ok();
}

fn read_cache(path: &Path) -> Result<CacheFile, String> {
    let text = read(path)?;
    serde_json::from_str(&text).map_err(|cause| format!("cannot parse {}: {cause}", path.display()))
}

fn write_cache(path: &Path, cache: &CacheFile) -> Result<(), String> {
    let text = serde_json::to_string_pretty(cache).map_err(|cause| cause.to_string())?;
    let staged = path.with_extension("cfgc.next");
    std::fs::write(&staged, format!("{text}\n"))
        .map_err(|cause| format!("cannot write {}: {cause}", staged.display()))?;
    std::fs::rename(&staged, path)
        .map_err(|cause| format!("cannot replace {}: {cause}", path.display()))
}

fn digest(text: &str) -> String {
    format!("{:x}", Sha256::digest(text.as_bytes()))
}

fn private_dir(path: &Path) -> Result<(), String> {
    std::fs::create_dir_all(path)
        .map_err(|cause| format!("cannot create {}: {cause}", path.display()))?;
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        std::fs::set_permissions(path, std::fs::Permissions::from_mode(0o700))
            .map_err(|cause| format!("cannot protect {}: {cause}", path.display()))?;
    }
    Ok(())
}

fn prune_snapshots(dir: &Path, current: &Path) {
    let Ok(entries) = std::fs::read_dir(dir) else {
        return;
    };
    let snapshots: Vec<(SystemTime, PathBuf)> = entries
        .flatten()
        .filter_map(|entry| {
            let path = entry.path();
            let name = path.file_name()?.to_str()?;
            let digest = name.split('.').next()?;
            if digest.len() != 64 || !digest.bytes().all(|byte| byte.is_ascii_hexdigit()) {
                return None;
            }
            let modified = entry.metadata().ok()?.modified().ok()?;
            Some((modified, path))
        })
        .collect();
    let now = SystemTime::now();
    for path in expired_snapshots(snapshots, current, now) {
        std::fs::remove_file(path).ok();
    }
}

fn expired_snapshots(
    mut snapshots: Vec<(SystemTime, PathBuf)>,
    current: &Path,
    now: SystemTime,
) -> Vec<PathBuf> {
    snapshots.sort_by_key(|(modified, _)| std::cmp::Reverse(*modified));
    snapshots
        .into_iter()
        .skip(SNAPSHOT_LIMIT)
        .filter(|(modified, path)| {
            path != current
                && now.duration_since(*modified).unwrap_or_default() >= SNAPSHOT_MIN_AGE
        })
        .map(|(_, path)| path)
        .collect()
}

#[cfg(unix)]
fn user_scope() -> u32 {
    unsafe { libc::geteuid() }
}

#[cfg(not(unix))]
fn user_scope() -> u32 {
    std::process::id()
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn snapshot_cleanup_keeps_recent_and_current_inputs() {
        let now = SystemTime::UNIX_EPOCH + Duration::from_secs(10_000);
        let old = now - SNAPSHOT_MIN_AGE - Duration::from_secs(1);
        let recent = now - Duration::from_secs(1);
        let current = PathBuf::from("17.cpp");
        let mut snapshots = (0..SNAPSHOT_LIMIT)
            .map(|index| (recent, PathBuf::from(format!("{index}.cpp"))))
            .collect::<Vec<_>>();
        snapshots.push((old, current.clone()));
        snapshots.push((old, PathBuf::from("18.cpp")));
        snapshots.push((recent, PathBuf::from("19.cpp")));

        let removed = expired_snapshots(snapshots, &current, now);

        assert_eq!(removed, vec![PathBuf::from("18.cpp")]);
    }
}

#[cfg(test)]
mod include_tests {
    use super::include_path;
    use std::path::PathBuf;

    #[test]
    fn include_paths_are_library_root_relative() {
        let roots = vec![
            PathBuf::from("/repo/desktop_blocks"),
            PathBuf::from("/home/user/my_blocks"),
        ];
        assert_eq!(
            include_path("/repo/desktop_blocks/filters/kaiser_lpf.hpp", &roots).as_deref(),
            Some("desktop_blocks/filters/kaiser_lpf.hpp")
        );
        assert_eq!(
            include_path("/home/user/my_blocks/agc.hpp", &roots).as_deref(),
            Some("my_blocks/agc.hpp")
        );
        assert_eq!(include_path("/elsewhere/foo.hpp", &roots), None);
    }
}

use std::collections::hash_map::RandomState;
use std::collections::{HashMap, HashSet};
use std::hash::{BuildHasher, Hasher};
use std::io::Write;
use std::path::{Path, PathBuf};
use std::sync::{Mutex, MutexGuard, PoisonError};
use std::time::{Duration, SystemTime};

use cler_graph::session::{self, Edited};
use cler_graph::{block_requirements, palette_specs, BlockSpec, Command, DocumentSession, PatchDirection};
use serde::{Deserialize, Serialize};
use serde_json::{Map, Value};

pub use cler_graph::session::{DocumentState, EditOutcome, NodeMove, Point, Preview};
use sha2::{Digest, Sha256};

use crate::provenance::{ArtifactCatalog, ArtifactRecord};

pub type Documents = Mutex<HashMap<PathBuf, Document>>;

const PALETTE_DIR: &str = "desktop_blocks";
const REPOSITORY_MARKER: &str = "include/cler.hpp";
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
    core: session::Document,
    spelling: String,
    working: PathBuf,
    cache_path: PathBuf,
    cache: CacheFile,
    palette: Vec<BlockSpec>,
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

    let includes = includes_for(&commands, doc, &target);
    session::apply(&mut doc.core, base_revision, commands, &includes, &mut |source| {
        write_working(&doc.working, source)
    })?;
    Ok(snapshot(&target, doc))
}

pub fn edit(
    docs: &Documents,
    path: &str,
    base_revision: u64,
    source: String,
) -> Result<EditOutcome, String> {
    let mut map = lock(docs);
    let (target, doc) = document(&mut map, path)?;
    refuse_external(doc, &target)?;
    let edited = session::edit(&mut doc.core, base_revision, &source, &mut |next| {
        write_working(&doc.working, next)
    })?;
    if let Edited::Unparsed(fault) = edited {
        // text the parser rejects belongs in the working copy, not in the session
        write_working(&doc.working, &source)?;
        return Ok(EditOutcome {
            unparsed: true,
            fault,
            state: snapshot(&target, doc),
        });
    }
    Ok(EditOutcome {
        unparsed: false,
        fault: None,
        state: snapshot(&target, doc),
    })
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

    let includes = includes_for(&commands, doc, &target);
    session::preview(&doc.core, base_revision, commands, &includes)
}

fn includes_for(commands: &[Command], doc: &Document, target: &Path) -> Vec<String> {
    let roots = include_roots(target);
    session::add_block_origins(commands, &doc.palette)
        .iter()
        .filter_map(|origin| include_path(origin, &roots))
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
    let mut map = lock(docs);
    let (target, doc) = document(&mut map, path)?;
    session::move_nodes(&mut doc.core, &view, moves)?;
    store_ui(doc)?;
    Ok(snapshot(&target, doc))
}

pub fn undo(docs: &Documents, path: &str) -> Result<DocumentState, String> {
    step(docs, path, PatchDirection::Reverse)
}

pub fn redo(docs: &Documents, path: &str) -> Result<DocumentState, String> {
    step(docs, path, PatchDirection::Forward)
}

pub fn save(docs: &Documents, path: &str) -> Result<DocumentState, String> {
    let mut map = lock(docs);
    let (target, doc) = document(&mut map, path)?;
    refuse_external(doc, &target)?;
    if doc.core.session.source() != doc.core.saved {
        let source = doc.core.session.source().to_string();
        persist(doc, &target, &source)?;
        update_cache_document(doc, &target);
    }
    Ok(snapshot(&target, doc))
}

pub fn create(docs: &Documents, path: &str) -> Result<DocumentState, String> {
    if Path::new(path).exists() {
        return Err(format!("{path} already exists"));
    }
    std::fs::write(path, session::NEW_DOCUMENT_TEMPLATE)
        .map_err(|cause| format!("cannot write {path}: {cause}"))?;
    open(docs, path)
}

pub fn save_as(docs: &Documents, path: &str, new_path: &str) -> Result<DocumentState, String> {
    let source = {
        let mut map = lock(docs);
        let (_, doc) = document(&mut map, path)?;
        doc.core.session.source().to_string()
    };
    std::fs::write(new_path, &source)
        .map_err(|cause| format!("cannot write {new_path}: {cause}"))?;
    open(docs, new_path)
}

pub fn reload(docs: &Documents, path: &str) -> Result<DocumentState, String> {
    let mut map = lock(docs);
    let (target, doc) = document(&mut map, path)?;
    let text = read(&target)?;
    doc.core.reload(text)?;
    write_working(&doc.working, doc.core.session.source())?;
    update_cache_document(doc, &target);
    doc.core.external_change = false;
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
        sha256: digest(doc.core.session.source()),
        requirements: build_requirements(doc.core.session.source(), &doc.palette),
        artifacts: ArtifactCatalog::read(&doc.cache.build),
    })
}

pub fn snapshot_draft(docs: &Documents, path: &str) -> Result<DraftState, String> {
    let map = lock(docs);
    let key = resolve(&map, path).ok_or_else(|| format!("no open document for {path}"))?;
    let doc = map
        .get(&key)
        .ok_or_else(|| format!("no open document for {path}"))?;
    let source = doc.core.session.source();
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
    doc.core.ui = ui;
    store_ui(doc)
}

fn store_ui(doc: &mut Document) -> Result<(), String> {
    if doc.cache.ui == doc.core.ui {
        return Ok(());
    }
    doc.cache.ui = doc.core.ui.clone();
    write_cache(&doc.cache_path, &doc.cache)
}

pub fn parse_file(docs: &Documents, path: &str) -> Result<String, String> {
    let map = lock(docs);
    if let Some(doc) = resolve(&map, path).and_then(|key| map.get(&key)) {
        return serde_json::to_string(&session::model(&doc.core, &doc.palette))
            .map_err(|cause| cause.to_string());
    }
    drop(map);
    let target = canonical(path)?;
    let opened = session::Document::new(
        DocumentSession::open(&target).map_err(|cause| cause.to_string())?,
        String::new(),
        Value::Null,
    );
    serde_json::to_string(&session::model(&opened, &nearby_palette(&target)))
        .map_err(|cause| cause.to_string())
}

pub fn palette(docs: &Documents, path: &str) -> Result<Vec<BlockSpec>, String> {
    let map = lock(docs);
    let key = resolve(&map, path).ok_or_else(|| format!("no open document for {path}"))?;
    let doc = map
        .get(&key)
        .ok_or_else(|| format!("no open document for {path}"))?;
    Ok(session::palette(
        &doc.core,
        &key.display().to_string(),
        &doc.palette,
    ))
}

pub fn note_disk_event(docs: &Documents, path: &Path) -> bool {
    let mut map = lock(docs);
    let Some(doc) = map.get_mut(path) else {
        return false;
    };
    let quiet = !doc.core.external_change;
    revalidate(doc, path);
    doc.core.external_change && quiet
}

fn step(docs: &Documents, path: &str, direction: PatchDirection) -> Result<DocumentState, String> {
    let mut map = lock(docs);
    let (target, doc) = document(&mut map, path)?;
    // a position action carries no source: disk drift does not block it and the
    // working copy — which may hold unparsed text — must be left alone
    if session::pending_action_edits_source(&doc.core, direction) != Some(false) {
        refuse_external(doc, &target)?;
    }
    session::step(&mut doc.core, direction, &mut |source| {
        write_working(&doc.working, source)
    })?;
    store_ui(doc)?;
    Ok(snapshot(&target, doc))
}

fn adopts_draft(session: &mut DocumentSession, draft: String) -> bool {
    session.reload(draft).is_ok() && !session.has_errors()
}

fn fresh(target: &Path, spelling: &str) -> Result<Document, String> {
    let mut session = DocumentSession::open(target).map_err(|cause| cause.to_string())?;
    let saved = session.source().to_string();
    let (working, cache_path, mut cache, draft) = create_working(target, &saved)?;
    // The working copy also holds text that was typed but never parsed, so a draft
    // is adopted only when it still parses; otherwise the saved file stands.
    if draft != saved && !adopts_draft(&mut session, draft) {
        session
            .reload(saved.clone())
            .map_err(|cause| cause.to_string())?;
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
        core: session::Document::new(session, saved, cache.ui.clone()),
        spelling: spelling.to_string(),
        working,
        cache_path,
        cache,
        palette: nearby_palette(target),
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
    let mut roots: Vec<PathBuf> = repo_blocks
        .into_iter()
        .filter_map(|dir| dir.canonicalize().ok())
        .collect();
    roots.extend(crate::settings::block_library_dirs());
    crate::settings::prune_nested(&mut roots);
    if roots.is_empty() {
        return Vec::new();
    }
    let mut specs = palette_specs(&roots).unwrap_or_default();
    let mut seen = HashSet::new();
    specs.retain(|spec| seen.insert((spec.origin.clone(), spec.name.clone())));
    specs
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
    session::snapshot(&target.display().to_string(), &doc.core, &doc.palette)
}

fn revalidate(doc: &mut Document, target: &Path) {
    doc.core.external_change =
        !matches!(std::fs::read(target), Ok(bytes) if bytes == doc.core.saved.as_bytes());
}

fn refuse_external(doc: &mut Document, target: &Path) -> Result<(), String> {
    let bytes = std::fs::read(target)
        .map_err(|cause| format!("cannot read {}: {cause}", target.display()))?;
    if bytes == doc.core.saved.as_bytes() {
        return Ok(());
    }
    doc.core.external_change = true;
    Err(drifted(target))
}

fn drifted(target: &Path) -> String {
    format!(
        "{} changed on disk since the last write; reload before editing",
        target.display()
    )
}

fn persist(doc: &mut Document, target: &Path, contents: &str) -> Result<(), String> {
    match write_atomic(target, contents, &doc.core.saved) {
        Ok(()) => {
            doc.core.saved = contents.to_string();
            doc.core.external_change = false;
            Ok(())
        }
        Err(WriteFailure::Drift) => {
            doc.core.external_change = true;
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

fn write_working(working: &Path, contents: &str) -> Result<(), String> {
    let staged = working.with_extension("cpp.next");
    std::fs::write(&staged, contents)
        .map_err(|cause| format!("cannot write {}: {cause}", staged.display()))?;
    std::fs::rename(&staged, working)
        .map_err(|cause| format!("cannot replace {}: {cause}", working.display()))
}

fn update_cache_document(doc: &mut Document, target: &Path) {
    doc.cache.document.source_path = target.display().to_string();
    doc.cache.document.saved_sha256 = digest(&doc.core.saved);
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

    #[test]
    fn a_draft_that_no_longer_parses_is_not_adopted() {
        let saved = "int main() { return 0; }\n";
        let mut session = DocumentSession::load(saved).expect("loads");

        assert!(
            adopts_draft(&mut session, "int main() { return 1; }\n".to_string()),
            "a parseable draft is adopted"
        );
        assert!(
            !adopts_draft(&mut session, "int main() { return 1;\n".to_string()),
            "typed-but-unfinished text must not become the session"
        );
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

#[cfg(test)]
mod palette_tests {
    #[test]
    fn a_library_inside_the_repo_palette_does_not_duplicate_specs() {
        let _guard = crate::settings::test_guard();
        let file = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("../../../../desktop_examples/hello_world.cpp");
        let specs = super::nearby_palette(&file.canonicalize().expect("example exists"));
        let mut seen = std::collections::HashSet::new();
        for spec in &specs {
            assert!(
                seen.insert((spec.origin.clone(), spec.name.clone())),
                "duplicate spec {} from {}",
                spec.name,
                spec.origin
            );
        }
        assert!(specs.iter().any(|spec| spec.name == "SinkNullBlock"));
    }

    #[test]
    fn wiring_an_empty_external_flowgraph_seeds_a_runner() {
        let _guard = crate::settings::test_guard();
        let dir = std::env::temp_dir().join(format!("cler-user-wire-{}", std::process::id()));
        std::fs::remove_dir_all(&dir).ok();
        std::fs::create_dir_all(&dir).expect("temp dir");
        let file = dir.join("flowgraph.cpp");
        let original = concat!(
            "#include \"cler.hpp\"\n",
            "#include \"task_policies/cler_desktop_tpolicy.hpp\"\n",
            "#include \"desktop_blocks/sources/source_chirp.hpp\"\n",
            "#include \"desktop_blocks/plots/plot_timeseries.hpp\"\n",
            "\n",
            "int main() {\n",
            "    SourceChirpBlock<float> source_chirp(\"source_chirp\", 2, 10, 20, 4000000, 5);\n",
            "    PlotTimeSeriesBlock plot_time_series(\"plot_time_series\", {\"in\"}, 4000000, 10);\n",
            "    auto flowgraph = cler::make_desktop_flowgraph();\n",
            "\n",
            "    cler::FlowGraphConfig config;\n",
            "    flowgraph.run(config);\n",
            "    return 0;\n",
            "}\n"
        );
        std::fs::write(&file, original).expect("seed file");

        let docs = super::Documents::default();
        let path = file.display().to_string();
        super::open(&docs, &path).expect("opens");
        let command = serde_json::json!({
            "command": "connect",
            "site": 0,
            "from": "source_chirp",
            "to": "plot_time_series",
            "port": "in",
            "port_index": 0
        });
        let state = super::apply(&docs, &path, 0, vec![command]).expect("connect applies");
        assert!(state
            .source
            .contains("cler::BlockRunner(&source_chirp, &plot_time_series.in[0])"));
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn an_external_file_still_gets_the_builtin_palette() {
        let _guard = crate::settings::test_guard();
        let dir = std::env::temp_dir().join(format!("cler-ext-palette-{}", std::process::id()));
        std::fs::create_dir_all(&dir).expect("temp dir");
        let file = dir.join("flowgraph.cpp");
        std::fs::write(&file, "int main() { return 0; }\n").expect("external file");
        let specs = super::nearby_palette(&file);
        assert!(
            specs.iter().any(|spec| spec.name == "SourceCWBlock"),
            "external files resolve the palette via the builtin cler root"
        );
        std::fs::remove_dir_all(&dir).ok();
    }
}

#[cfg(test)]
mod external_add_tests {
    use super::*;

    #[test]
    fn adding_a_block_to_a_fresh_external_document_works() {
        let _guard = crate::settings::test_guard();
        let dir = std::env::temp_dir().join(format!("cler-ext-add-{}", std::process::id()));
        std::fs::remove_dir_all(&dir).ok();
        std::fs::create_dir_all(&dir).expect("temp dir");
        let file = dir.join("flowgraph.cpp");
        let docs = Documents::default();
        let path = file.display().to_string();
        create(&docs, &path).expect("new document");
        let command = serde_json::json!({
            "command": "add_block",
            "site": 0,
            "type": "SourceCWBlock",
            "template_args": ["float"],
            "ctor_args": ["\"source\"", "1.0f", "1.0f", "1000"],
            "var_name": "source"
        });
        let state = apply(&docs, &path, 0, vec![command]).expect("add_block applies");
        assert!(state.source.contains("SourceCWBlock<float> source"));
        assert!(state
            .source
            .contains("#include \"desktop_blocks/sources/source_cw.hpp\""));
        std::fs::remove_dir_all(&dir).ok();
    }
}

#[cfg(test)]
mod external_build_tests {
    #[test]
    fn an_external_draft_records_its_artifact() {
        let _guard = crate::settings::test_guard();
        let dir = std::env::temp_dir().join(format!("cler-ext-build-{}", std::process::id()));
        std::fs::remove_dir_all(&dir).ok();
        std::fs::create_dir_all(&dir).expect("temp dir");
        let file = dir.join("flowgraph.cpp");
        std::fs::write(
            &file,
            concat!(
                "#include \"cler.hpp\"\n",
                "#include \"task_policies/cler_desktop_tpolicy.hpp\"\n",
                "#include \"desktop_blocks/sources/source_cw.hpp\"\n",
                "#include \"desktop_blocks/sinks/sink_null.hpp\"\n",
                "int main() {\n",
                "    SourceCWBlock<float> source(\"Source\", 1.0f, 1.0f, 1000);\n",
                "    SinkNullBlock<float> sink(\"Null\");\n",
                "    auto flowgraph = cler::make_desktop_flowgraph(\n",
                "        cler::BlockRunner(&source, &sink.in),\n",
                "        cler::BlockRunner(&sink)\n",
                "    );\n",
                "    flowgraph.run();\n",
                "    return 0;\n",
                "}\n"
            ),
        )
        .expect("seed");

        let docs = super::Documents::default();
        let path = file.display().to_string();
        super::open(&docs, &path).expect("opens");
        let draft = super::snapshot_draft(&docs, &path).expect("draft");
        let found = crate::build::find_target(&path).expect("target");
        assert!(found.available, "{:?}", found.reason);
        assert!(found.name.starts_with("cler_draft_"));
        assert!(crate::build::build_input_for_test(&file, &found, &draft).is_ok());

        let binary = crate::build::draft_binary_for_test(&file, &draft.workspace)
            .expect("draft binary path");
        assert_eq!(
            binary.file_name().and_then(|name| name.to_str()),
            Some(found.name.as_str()),
            "Run looks for the binary cmake was told to build"
        );
        std::fs::remove_dir_all(&dir).ok();
    }
}

#[cfg(test)]
mod render_tests {
    #[test]
    fn materializing_a_loop_titles_the_window_after_the_document() {
        let _guard = crate::settings::test_guard();
        let dir = std::env::temp_dir().join(format!("cler-render-{}", std::process::id()));
        std::fs::remove_dir_all(&dir).ok();
        std::fs::create_dir_all(&dir).expect("temp dir");
        let file = dir.join("my_receiver.cpp");
        std::fs::write(
            &file,
            concat!(
                "#include \"cler.hpp\"\n",
                "#include \"task_policies/cler_desktop_tpolicy.hpp\"\n",
                "#include \"desktop_blocks/plots/plot_timeseries.hpp\"\n",
                "int main() {\n",
                "    PlotTimeSeriesBlock plot(\"Plot\", {\"in\"}, 1000, 10.0f);\n",
                "    auto flowgraph = cler::make_desktop_flowgraph(\n",
                "        cler::BlockRunner(&plot)\n",
                "    );\n",
                "    cler::FlowGraphConfig config;\n",
                "    flowgraph.run(config);\n",
                "    return 0;\n",
                "}\n"
            ),
        )
        .expect("seed");

        let docs = super::Documents::default();
        let path = file.display().to_string();
        super::open(&docs, &path).expect("opens");
        let command = serde_json::json!({
            "command": "materialize_gui",
            "site": 0
        });
        let state = super::apply(&docs, &path, 0, vec![command]).expect("materialize_gui applies");
        assert!(
            state.source.contains("cler::GuiManager gui(800, 400, \"my_receiver\")"),
            "the window carries the document name: {}",
            state.source
        );
        assert!(state.source.contains("gui.render("));
        assert!(!state.source.contains("plot.render();"));
        std::fs::remove_dir_all(&dir).ok();
    }
}

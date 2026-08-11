use std::collections::{BTreeMap, HashMap};
use std::ffi::OsStr;
use std::io::{BufRead, BufReader, Read};
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex, MutexGuard, PoisonError};
use std::thread::{self, JoinHandle};
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use serde::Serialize;
use serde_json::{json, Value};
use sha2::{Digest, Sha256};

use crate::document::{canonical, DraftState};
use crate::provenance::{ArtifactRecord, ArtifactStatus, InputKey};

pub type Emit = Arc<dyn Fn(&str, Value) + Send + Sync>;
pub type RecordArtifact = Arc<dyn Fn(String, ArtifactRecord) -> Result<(), String> + Send + Sync>;
pub type FinalizeArtifact =
    Arc<dyn Fn(ArtifactRecord) -> Result<ArtifactRecord, String> + Send + Sync>;

struct Running {
    id: u64,
    child: Child,
}

#[derive(Default)]
struct JobBook {
    running: HashMap<String, Running>,
}

#[derive(Default, Clone)]
pub struct Jobs(Arc<Mutex<JobBook>>);

struct Completion {
    name: String,
    artifact: ArtifactRecord,
    finalize: FinalizeArtifact,
    record: RecordArtifact,
}

const MARKER: &str = "include/cler.hpp";
const EXAMPLES: &str = "desktop_examples";
const STANDARD: &str = "-std=c++17";
const GRACE: Duration = Duration::from_secs(3);
const PRODUCER: &str = "cmake";
const DRAFT_INPUT: &str = "draft";
const CMAKE_INPUT: &str = "cmake";
const REPO_DEPENDENCY: &str = "dependency:repo:";
const EXTERNAL_DEPENDENCY: &str = "dependency:absolute:";
static NEXT_JOB: AtomicU64 = AtomicU64::new(1);

#[derive(Clone, Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Target {
    pub available: bool,
    pub reason: Option<String>,
    pub name: String,
    pub build_dir: Option<String>,
    pub binary: Option<String>,
    pub artifact: ArtifactStatus,
}

#[derive(Clone, Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Started {
    pub job_id: u64,
    pub input_key: InputKey,
}

pub fn repo_root(target: &Path) -> Option<PathBuf> {
    target
        .ancestors()
        .find(|dir| dir.join(MARKER).is_file())
        .map(Path::to_path_buf)
}

pub fn check(jobs: &Jobs, path: &str, emit: Emit) -> Result<Started, String> {
    let source = canonical(path)?;
    check_draft(jobs, path, &source, emit)
}

pub fn check_draft(jobs: &Jobs, path: &str, draft: &Path, emit: Emit) -> Result<Started, String> {
    let target = canonical(path)?;
    let draft = draft
        .canonicalize()
        .map_err(|cause| format!("cannot resolve {}: {cause}", draft.display()))?;
    let root = repo_root(&target).ok_or_else(|| outside(&target))?;
    let mut command = Command::new("g++");
    command
        .arg("-fsyntax-only")
        .arg(STANDARD)
        .arg("-fdiagnostics-color=never");
    for dir in includes(&root) {
        command.arg(format!("-I{}", dir.display()));
    }
    command.arg(&draft).current_dir(&root);
    let input_key = check_input(&draft)?;
    spawn_mapped(
        jobs,
        "check",
        &target,
        command,
        emit,
        Some((draft.display().to_string(), target.display().to_string())),
        input_key,
        None,
    )
}

pub fn find_target(path: &str) -> Result<Target, String> {
    let file = canonical(path)?;
    let name = file
        .file_stem()
        .and_then(OsStr::to_str)
        .unwrap_or_default()
        .to_string();
    let root = repo_root(&file).ok_or_else(|| outside(&file))?;
    let Some(relative) = file.parent().and_then(|dir| dir.strip_prefix(&root).ok()) else {
        return Ok(refused(name, outside(&file)));
    };
    if !relative.starts_with(EXAMPLES) {
        return Ok(refused(
            name,
            format!("only files under {EXAMPLES}/ have a cmake target"),
        ));
    }
    let Some(dir) = choose_build(&root, relative) else {
        return Ok(refused(
            name,
            format!(
                "configure a build directory first: cmake -B {}/build -S {}",
                root.display(),
                root.display()
            ),
        ));
    };
    let binary = dir.join(relative).join(&name);
    Ok(Target {
        available: true,
        reason: None,
        name,
        build_dir: Some(dir.display().to_string()),
        binary: Some(binary.display().to_string()),
        artifact: ArtifactStatus::NeedsBuild {
            reason: "build the current draft before running".to_string(),
        },
    })
}

pub fn find_draft_target(jobs: &Jobs, path: &str, draft: &DraftState) -> Result<Target, String> {
    let target = canonical(path)?;
    let mut found = find_target(path)?;
    if !found.available {
        return Ok(found);
    }
    let (_, _, binary) = draft_tree(&target, &draft.workspace)?;
    let name = artifact_name(&target, &found)?;
    let input_key = build_input(&target, &found, &draft.sha256)?;
    let reason = "build the current draft before running".to_string();
    found.binary = Some(binary.display().to_string());
    if let Some(job_id) = running_job(jobs, "build", &target) {
        found.artifact = ArtifactStatus::Building { job_id };
        return Ok(found);
    }
    found.artifact = match draft.artifacts.get(&name) {
        Some(record)
            if input_matches(&target, &input_key, &record.input_key)
                && record.artifact_path == binary.display().to_string() =>
        {
            if binary.is_file() {
                ArtifactStatus::Ready {
                    artifact_path: binary.display().to_string(),
                }
            } else {
                ArtifactStatus::NeedsBuild {
                    reason: "the built artifact is missing; build the current draft again"
                        .to_string(),
                }
            }
        }
        _ => ArtifactStatus::NeedsBuild { reason },
    };
    Ok(found)
}

pub fn build(jobs: &Jobs, path: &str, emit: Emit) -> Result<Started, String> {
    let target = canonical(path)?;
    refuse_parallel_build(jobs, &target)?;
    let found = find_target(path)?;
    let dir = usable(&found)?;
    let mut command = Command::new("cmake");
    command.args(["--build", dir, "--target", &found.name, "--parallel"]);
    command.arg(cores().to_string());
    let input_key = InputKey {
        inputs: BTreeMap::new(),
        recipe_sha256: hash_text(&format!("source-build:{}:{}", dir, found.name)),
    };
    spawn(
        jobs,
        "build",
        &target,
        command,
        emit,
        input_key,
        None,
    )
}

pub fn build_draft(
    jobs: &Jobs,
    path: &str,
    draft: &DraftState,
    record: RecordArtifact,
    emit: Emit,
) -> Result<Started, String> {
    let target = canonical(path)?;
    refuse_parallel_build(jobs, &target)?;
    let found = find_target(path)?;
    usable(&found)?;
    let (source_dir, build_dir, binary) = draft_tree(&target, &draft.workspace)?;
    prepare_overlay(&target, &draft.path, &source_dir)?;
    let script = draft_runner(&found, &source_dir, &build_dir)?;
    let mut command = Command::new("cmake");
    command.arg("-P").arg(script);
    let name = artifact_name(&target, &found)?;
    let input_key = build_input(&target, &found, &draft.sha256)?;
    let started = SystemTime::now();
    let completed = ArtifactRecord {
        input_key: input_key.clone(),
        producer: PRODUCER.to_string(),
        artifact_path: binary.display().to_string(),
        completed_unix_ms: 0,
        extra: Default::default(),
    };
    let dependency_target = target.clone();
    let dependency_source = source_dir.clone();
    let dependency_build = build_dir.clone();
    let finalize: FinalizeArtifact = Arc::new(move |artifact| {
        finalize_build_artifact(
            artifact,
            &dependency_target,
            &dependency_source,
            &dependency_build,
            started,
        )
    });
    spawn(
        jobs,
        "build",
        &target,
        command,
        emit,
        input_key,
        Some(Completion {
            name,
            artifact: completed,
            finalize,
            record,
        }),
    )
}

pub fn start(jobs: &Jobs, path: &str, emit: Emit) -> Result<Started, String> {
    let found = find_target(path)?;
    usable(&found)?;
    let binary = PathBuf::from(found.binary.unwrap_or_default());
    if !binary.is_file() {
        return Err(format!("{} is not built yet", binary.display()));
    }
    let mut command = Command::new(&binary);
    if let Some(dir) = binary.parent() {
        command.current_dir(dir);
    }
    let input_key = InputKey {
        inputs: BTreeMap::new(),
        recipe_sha256: hash_text(&binary.display().to_string()),
    };
    spawn(
        jobs,
        "run",
        &canonical(path)?,
        command,
        emit,
        input_key,
        None,
    )
}

pub fn start_draft(
    jobs: &Jobs,
    path: &str,
    draft: &DraftState,
    emit: Emit,
) -> Result<Started, String> {
    let target = canonical(path)?;
    let found = find_draft_target(jobs, path, draft)?;
    usable(&found)?;
    let ArtifactStatus::Ready { artifact_path } = &found.artifact else {
        let reason = match found.artifact {
            ArtifactStatus::Unavailable { ref reason }
            | ArtifactStatus::NeedsBuild { ref reason } => reason.clone(),
            ArtifactStatus::Building { .. } => {
                "a build is already running for this document".to_string()
            }
            ArtifactStatus::Ready { .. } => unreachable!(),
        };
        return Err(reason);
    };
    let binary = PathBuf::from(artifact_path);
    let mut command = Command::new(&binary);
    if let Some(dir) = binary.parent() {
        command.current_dir(dir);
    }
    let input_key = build_input(&target, &found, &draft.sha256)?;
    spawn(jobs, "run", &target, command, emit, input_key, None)
}

pub fn stop(jobs: &Jobs, path: &str) -> Result<(), String> {
    let target = canonical(path)?;
    let key = key("run", &target);
    let Some((id, pid)) = held(jobs)
        .running
        .get(&key)
        .map(|running| (running.id, running.child.id()))
    else {
        return Err(format!("nothing is running for {}", target.display()));
    };
    interrupt(pid);
    let jobs = jobs.clone();
    thread::spawn(move || {
        thread::sleep(GRACE);
        discard_job(&jobs, &key, id);
    });
    Ok(())
}

fn usable(found: &Target) -> Result<&str, String> {
    match (found.available, found.build_dir.as_deref()) {
        (true, Some(dir)) => Ok(dir),
        _ => Err(found
            .reason
            .clone()
            .unwrap_or_else(|| format!("{} has no cmake target", found.name))),
    }
}

fn refused(name: String, reason: String) -> Target {
    Target {
        available: false,
        reason: Some(reason.clone()),
        name,
        build_dir: None,
        binary: None,
        artifact: ArtifactStatus::Unavailable { reason },
    }
}

fn artifact_name(target: &Path, found: &Target) -> Result<String, String> {
    let root = repo_root(target).ok_or_else(|| outside(target))?;
    let relative = target.strip_prefix(root).map_err(|_| outside(target))?;
    Ok(format!("{PRODUCER}:{}:{}", relative.display(), found.name))
}

fn build_input(target: &Path, found: &Target, draft_sha256: &str) -> Result<InputKey, String> {
    let root = repo_root(target).ok_or_else(|| outside(target))?;
    let relative = target.strip_prefix(&root).map_err(|_| outside(target))?;
    let configured = found.build_dir.as_deref().unwrap_or_default();
    let cache = Path::new(configured).join("CMakeCache.txt");
    let recipe = json!({
        "producer": PRODUCER,
        "target": found.name,
        "source": relative.display().to_string(),
        "configuredBuild": configured,
        "buildType": cache_value(&cache, "CMAKE_BUILD_TYPE"),
        "compiler": cache_value(&cache, "CMAKE_CXX_COMPILER"),
        "generator": cache_value(&cache, "CMAKE_GENERATOR"),
        "os": std::env::consts::OS,
        "arch": std::env::consts::ARCH,
    });
    let mut inputs = BTreeMap::new();
    inputs.insert(DRAFT_INPUT.to_string(), draft_sha256.to_string());
    inputs.insert(CMAKE_INPUT.to_string(), project_config_digest(&root)?);
    Ok(InputKey {
        inputs,
        recipe_sha256: hash_text(&recipe.to_string()),
    })
}

fn input_matches(target: &Path, current: &InputKey, recorded: &InputKey) -> bool {
    if current.recipe_sha256 != recorded.recipe_sha256
        || current
            .inputs
            .iter()
            .any(|(name, expected)| recorded.inputs.get(name) != Some(expected))
    {
        return false;
    }
    recorded.inputs.iter().all(|(name, expected)| {
        let actual = match name.as_str() {
            DRAFT_INPUT | CMAKE_INPUT => current.inputs.get(name).cloned(),
            _ => dependency_path(target, name)
                .and_then(|path| std::fs::read(path).ok())
                .map(|bytes| hash_bytes(&bytes)),
        };
        actual.as_deref() == Some(expected)
    })
}

fn dependency_path(target: &Path, name: &str) -> Option<PathBuf> {
    if let Some(relative) = name.strip_prefix(REPO_DEPENDENCY) {
        return repo_root(target).map(|root| root.join(relative));
    }
    name.strip_prefix(EXTERNAL_DEPENDENCY).map(PathBuf::from)
}

fn project_config_digest(root: &Path) -> Result<String, String> {
    let mut files = Vec::new();
    collect_project_configs(root, root, &mut files)?;
    files.sort();
    let mut digest = Sha256::new();
    for file in files {
        let relative = file.strip_prefix(root).unwrap_or(&file);
        let bytes = std::fs::read(&file)
            .map_err(|cause| format!("cannot read {}: {cause}", file.display()))?;
        digest.update(relative.as_os_str().as_encoded_bytes());
        digest.update([0]);
        digest.update(&bytes);
        digest.update([0]);
    }
    Ok(format!("{:x}", digest.finalize()))
}

fn collect_project_configs(
    root: &Path,
    dir: &Path,
    found: &mut Vec<PathBuf>,
) -> Result<(), String> {
    let entries = std::fs::read_dir(dir)
        .map_err(|cause| format!("cannot read {}: {cause}", dir.display()))?;
    for entry in entries.flatten() {
        let path = entry.path();
        let file_type = entry
            .file_type()
            .map_err(|cause| format!("cannot inspect {}: {cause}", path.display()))?;
        if file_type.is_symlink() {
            continue;
        }
        if file_type.is_dir() {
            if !skip_config_dir(root, &path) {
                collect_project_configs(root, &path, found)?;
            }
            continue;
        }
        let name = path.file_name().and_then(OsStr::to_str).unwrap_or_default();
        if name == "CMakeLists.txt" || path.extension() == Some(OsStr::new("cmake")) {
            found.push(path);
        }
    }
    Ok(())
}

fn skip_config_dir(root: &Path, path: &Path) -> bool {
    let name = path.file_name().and_then(OsStr::to_str).unwrap_or_default();
    matches!(name, ".git" | "node_modules" | "target" | "dist" | ".cache")
        || (path != root && path.join("CMakeCache.txt").is_file())
}

fn finalize_build_artifact(
    mut artifact: ArtifactRecord,
    target: &Path,
    source_dir: &Path,
    build_dir: &Path,
    started: SystemTime,
) -> Result<ArtifactRecord, String> {
    let root = repo_root(target).ok_or_else(|| outside(target))?;
    let expected_config = artifact
        .input_key
        .inputs
        .get(CMAKE_INPUT)
        .ok_or_else(|| "build input is missing its CMake fingerprint".to_string())?;
    if &project_config_digest(&root)? != expected_config {
        return Err(
            "CMake inputs changed while the artifact was building; build again".to_string(),
        );
    }
    let dependencies = dependency_inputs(target, source_dir, build_dir, started)?;
    artifact.input_key.inputs.extend(dependencies);
    Ok(artifact)
}

fn dependency_inputs(
    target: &Path,
    source_dir: &Path,
    build_dir: &Path,
    started: SystemTime,
) -> Result<BTreeMap<String, String>, String> {
    let root = repo_root(target).ok_or_else(|| outside(target))?;
    let relative_target = target.strip_prefix(&root).map_err(|_| outside(target))?;
    let mut depfiles = Vec::new();
    collect_depfiles(build_dir, &mut depfiles)?;
    if depfiles.is_empty() {
        return Err("the build produced no compiler dependency files; artifact freshness cannot be verified".to_string());
    }
    let mut inputs = BTreeMap::new();
    for depfile in depfiles {
        let text = std::fs::read_to_string(&depfile)
            .map_err(|cause| format!("cannot read {}: {cause}", depfile.display()))?;
        for dependency in depfile_dependencies(&text) {
            let Some(path) = locate_dependency(&dependency, &depfile, source_dir, build_dir, &root)
            else {
                continue;
            };
            let Some((name, actual)) = dependency_name(&path, source_dir, build_dir, &root) else {
                continue;
            };
            if name == format!("{REPO_DEPENDENCY}{}", relative_target.display()) {
                continue;
            }
            if !path.starts_with(build_dir)
                && std::fs::metadata(&actual)
                    .and_then(|metadata| metadata.modified())
                    .is_ok_and(|modified| modified > started)
            {
                return Err(format!(
                    "{} changed while the artifact was building; build again",
                    actual.display()
                ));
            }
            let bytes = std::fs::read(&actual)
                .map_err(|cause| format!("cannot read {}: {cause}", actual.display()))?;
            inputs.insert(name, hash_bytes(&bytes));
        }
    }
    Ok(inputs)
}

fn collect_depfiles(dir: &Path, found: &mut Vec<PathBuf>) -> Result<(), String> {
    let entries = std::fs::read_dir(dir)
        .map_err(|cause| format!("cannot read {}: {cause}", dir.display()))?;
    for entry in entries.flatten() {
        let path = entry.path();
        let file_type = entry
            .file_type()
            .map_err(|cause| format!("cannot inspect {}: {cause}", path.display()))?;
        if file_type.is_dir() {
            collect_depfiles(&path, found)?;
        } else if file_type.is_file() && path.extension() == Some(OsStr::new("d")) {
            found.push(path);
        }
    }
    Ok(())
}

fn depfile_dependencies(text: &str) -> Vec<String> {
    let flattened = text.replace("\\\r\n", " ").replace("\\\n", " ");
    let Some(split) = unescaped_colon(&flattened) else {
        return Vec::new();
    };
    make_words(&flattened[split + 1..])
}

fn unescaped_colon(text: &str) -> Option<usize> {
    let mut escaped = false;
    for (index, byte) in text.bytes().enumerate() {
        if byte == b':' && !escaped {
            return Some(index);
        }
        escaped = byte == b'\\' && !escaped;
        if byte != b'\\' {
            escaped = false;
        }
    }
    None
}

fn make_words(text: &str) -> Vec<String> {
    let mut words = Vec::new();
    let mut word = String::new();
    let mut escaped = false;
    for character in text.chars() {
        if escaped {
            word.push(character);
            escaped = false;
        } else if character == '\\' {
            escaped = true;
        } else if character.is_whitespace() {
            if !word.is_empty() {
                words.push(std::mem::take(&mut word));
            }
        } else {
            word.push(character);
        }
    }
    if escaped {
        word.push('\\');
    }
    if !word.is_empty() {
        words.push(word);
    }
    words
}

fn locate_dependency(
    dependency: &str,
    depfile: &Path,
    source_dir: &Path,
    build_dir: &Path,
    root: &Path,
) -> Option<PathBuf> {
    let path = PathBuf::from(dependency);
    if path.is_absolute() && path.exists() {
        return Some(path);
    }
    let parent = depfile.parent().unwrap_or(build_dir);
    [
        parent.join(&path),
        build_dir.join(&path),
        source_dir.join(&path),
        root.join(&path),
    ]
    .into_iter()
    .find(|candidate| candidate.exists())
}

fn dependency_name(
    path: &Path,
    source_dir: &Path,
    build_dir: &Path,
    root: &Path,
) -> Option<(String, PathBuf)> {
    if let Ok(relative) = path.strip_prefix(source_dir) {
        let actual = root.join(relative);
        return Some((
            format!("{REPO_DEPENDENCY}{}", relative.display()),
            actual,
        ));
    }
    let actual = path.canonicalize().ok()?;
    if let Ok(relative) = actual.strip_prefix(root) {
        return Some((
            format!("{REPO_DEPENDENCY}{}", relative.display()),
            actual,
        ));
    }
    if actual.starts_with(build_dir) || actual.components().any(|part| part.as_os_str() == "_deps")
    {
        return Some((
            format!("{EXTERNAL_DEPENDENCY}{}", actual.display()),
            actual,
        ));
    }
    None
}

fn check_input(draft: &Path) -> Result<InputKey, String> {
    let bytes = std::fs::read(draft)
        .map_err(|cause| format!("cannot read {}: {cause}", draft.display()))?;
    let mut inputs = BTreeMap::new();
    inputs.insert(DRAFT_INPUT.to_string(), hash_bytes(&bytes));
    Ok(InputKey {
        inputs,
        recipe_sha256: hash_text(&format!(
            "g++:{STANDARD}:{}:{}",
            std::env::consts::OS,
            std::env::consts::ARCH
        )),
    })
}

fn hash_text(text: &str) -> String {
    hash_bytes(text.as_bytes())
}

fn hash_bytes(bytes: &[u8]) -> String {
    format!("{:x}", Sha256::digest(bytes))
}

fn completed_unix_ms() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_millis()
        .try_into()
        .unwrap_or(u64::MAX)
}

fn outside(target: &Path) -> String {
    format!(
        "{} is not inside a cler repository (no {MARKER} above it)",
        target.display()
    )
}

fn cores() -> usize {
    thread::available_parallelism().map_or(2, |count| count.get().saturating_sub(1).clamp(1, 8))
}

fn draft_tree(target: &Path, workspace: &Path) -> Result<(PathBuf, PathBuf, PathBuf), String> {
    let root = repo_root(target).ok_or_else(|| outside(target))?;
    let relative = target.strip_prefix(&root).map_err(|_| outside(target))?;
    let source_dir = workspace.join("source");
    let build_dir = workspace.join("build");
    let name = target
        .file_stem()
        .and_then(OsStr::to_str)
        .unwrap_or_default();
    let binary = build_dir
        .join(relative.parent().unwrap_or_else(|| Path::new("")))
        .join(name);
    Ok((source_dir, build_dir, binary))
}

fn prepare_overlay(target: &Path, draft: &Path, source_dir: &Path) -> Result<(), String> {
    let root = repo_root(target).ok_or_else(|| outside(target))?;
    let relative = target.strip_prefix(&root).map_err(|_| outside(target))?;
    mirror_layer(&root, source_dir, relative, draft)
}

fn mirror_layer(real: &Path, overlay: &Path, relative: &Path, draft: &Path) -> Result<(), String> {
    std::fs::create_dir_all(overlay)
        .map_err(|cause| format!("cannot create {}: {cause}", overlay.display()))?;
    let mut parts = relative.components();
    let wanted = parts
        .next()
        .ok_or_else(|| format!("{} is not a source file", relative.display()))?
        .as_os_str()
        .to_os_string();
    let tail: PathBuf = parts.collect();
    let entries = std::fs::read_dir(real)
        .map_err(|cause| format!("cannot read {}: {cause}", real.display()))?;
    for entry in entries.flatten() {
        let name = entry.file_name();
        let destination = overlay.join(&name);
        if name == wanted {
            if tail.as_os_str().is_empty() {
                replace_link(draft, &destination)?;
            } else {
                mirror_layer(&entry.path(), &destination, &tail, draft)?;
            }
        } else {
            ensure_link(&entry.path(), &destination)?;
        }
    }
    Ok(())
}

#[cfg(unix)]
fn ensure_link(target: &Path, link: &Path) -> Result<(), String> {
    if std::fs::symlink_metadata(link).is_ok() {
        return Ok(());
    }
    std::os::unix::fs::symlink(target, link)
        .map_err(|cause| format!("cannot link {}: {cause}", link.display()))
}

#[cfg(unix)]
fn replace_link(target: &Path, link: &Path) -> Result<(), String> {
    if std::fs::symlink_metadata(link).is_ok() {
        std::fs::remove_file(link)
            .map_err(|cause| format!("cannot replace {}: {cause}", link.display()))?;
    }
    std::os::unix::fs::symlink(target, link)
        .map_err(|cause| format!("cannot link {}: {cause}", link.display()))
}

#[cfg(not(unix))]
fn ensure_link(_target: &Path, _link: &Path) -> Result<(), String> {
    Err("temporary draft builds require a Unix-compatible filesystem".to_string())
}

#[cfg(not(unix))]
fn replace_link(_target: &Path, _link: &Path) -> Result<(), String> {
    Err("temporary draft builds require a Unix-compatible filesystem".to_string())
}

fn draft_runner(found: &Target, source_dir: &Path, build_dir: &Path) -> Result<PathBuf, String> {
    std::fs::create_dir_all(build_dir)
        .map_err(|cause| format!("cannot create {}: {cause}", build_dir.display()))?;
    let mut configure = vec![
        "-S".to_string(),
        source_dir.display().to_string(),
        "-B".to_string(),
        build_dir.display().to_string(),
        "-DCLER_BUILD_TESTS=OFF".to_string(),
        "-DCLER_BUILD_PERFORMANCE=OFF".to_string(),
    ];
    if let Some(existing) = found.build_dir.as_deref() {
        let cache = Path::new(existing).join("CMakeCache.txt");
        for key in ["CMAKE_BUILD_TYPE", "CMAKE_C_COMPILER", "CMAKE_CXX_COMPILER"] {
            if let Some(value) = cache_value(&cache, key) {
                configure.push(format!("-D{key}={value}"));
            }
        }
        for source in subdirs(&Path::new(existing).join("_deps")) {
            let Some(name) = source.file_name().and_then(OsStr::to_str) else {
                continue;
            };
            let Some(package) = name.strip_suffix("-src") else {
                continue;
            };
            let variable = package.replace('-', "_").to_uppercase();
            configure.push(format!(
                "-DFETCHCONTENT_SOURCE_DIR_{variable}={}",
                source.display()
            ));
        }
    }
    let args = configure
        .iter()
        .map(|arg| cmake_quote(arg))
        .collect::<Vec<_>>()
        .join(" ");
    let script = build_dir
        .parent()
        .unwrap_or(build_dir)
        .join("build-draft.cmake");
    let text = format!(
        "execute_process(COMMAND \"${{CMAKE_COMMAND}}\" {args} RESULT_VARIABLE configured)\n\
         if(NOT configured EQUAL 0)\n\
           message(FATAL_ERROR \"draft configure failed\")\n\
         endif()\n\
         execute_process(COMMAND \"${{CMAKE_COMMAND}}\" --build {} --target {} --parallel {} RESULT_VARIABLE built)\n\
         if(NOT built EQUAL 0)\n\
           message(FATAL_ERROR \"draft build failed\")\n\
         endif()\n",
        cmake_quote(&build_dir.display().to_string()),
        found.name,
        cores()
    );
    std::fs::write(&script, text)
        .map_err(|cause| format!("cannot write {}: {cause}", script.display()))?;
    Ok(script)
}

fn cmake_quote(text: &str) -> String {
    format!("[==[{text}]==]")
}

fn cache_value(cache: &Path, key: &str) -> Option<String> {
    let text = std::fs::read_to_string(cache).ok()?;
    text.lines().find_map(|line| {
        let (name, value) = line.split_once('=')?;
        name.split_once(':')
            .is_some_and(|(found, _)| found == key)
            .then(|| value.to_string())
    })
}

fn subdirs(dir: &Path) -> Vec<PathBuf> {
    let Ok(entries) = std::fs::read_dir(dir) else {
        return Vec::new();
    };
    let mut found: Vec<PathBuf> = entries
        .flatten()
        .map(|entry| entry.path())
        .filter(|path| path.is_dir())
        .collect();
    found.sort();
    found
}

fn includes(root: &Path) -> Vec<PathBuf> {
    let mut dirs = vec![root.to_path_buf(), root.join("include")];
    dirs.extend(subdirs(&root.join("desktop_blocks")));
    for build in build_dirs(root) {
        for dep in subdirs(&build.1.join("_deps")) {
            dirs.push(dep.join("include"));
            dirs.push(dep.join("backends"));
            dirs.push(dep);
        }
    }
    dirs.retain(|dir| dir.is_dir());
    dirs
}

fn build_dirs(root: &Path) -> Vec<(SystemTime, PathBuf)> {
    let mut found: Vec<(SystemTime, PathBuf)> = subdirs(root)
        .into_iter()
        .filter(|dir| {
            dir.file_name()
                .and_then(OsStr::to_str)
                .is_some_and(|name| name.starts_with("build"))
        })
        .filter_map(|dir| {
            let stamp = std::fs::metadata(dir.join("CMakeCache.txt"))
                .and_then(|meta| meta.modified())
                .ok()?;
            Some((stamp, dir))
        })
        .collect();
    found.sort();
    found
}

fn choose_build(root: &Path, relative: &Path) -> Option<PathBuf> {
    let dirs = build_dirs(root);
    dirs.iter()
        .rev()
        .map(|(_, dir)| dir)
        .find(|dir| dir.join(relative).is_dir())
        .or_else(|| dirs.last().map(|(_, dir)| dir))
        .cloned()
}

fn key(kind: &str, target: &Path) -> String {
    format!("{kind}:{}", target.display())
}

fn held(jobs: &Jobs) -> MutexGuard<'_, JobBook> {
    jobs.0.lock().unwrap_or_else(PoisonError::into_inner)
}

fn running_job(jobs: &Jobs, kind: &str, target: &Path) -> Option<u64> {
    held(jobs)
        .running
        .get(&key(kind, target))
        .map(|running| running.id)
}

fn refuse_parallel_build(jobs: &Jobs, target: &Path) -> Result<(), String> {
    let existing = held(jobs)
        .running
        .get(&key("build", target))
        .map(|running| running.id);
    match existing {
        Some(id) => Err(format!(
            "build job {id} is already running for {}",
            target.display()
        )),
        None => Ok(()),
    }
}

fn discard(jobs: &Jobs, key: &str) {
    let taken = held(jobs).running.remove(key);
    if let Some(mut running) = taken {
        running.child.kill().ok();
        running.child.wait().ok();
    }
}

fn discard_job(jobs: &Jobs, key: &str, id: u64) {
    let taken = {
        let mut book = held(jobs);
        book.running
            .get(key)
            .is_some_and(|running| running.id == id)
            .then(|| book.running.remove(key))
            .flatten()
    };
    if let Some(mut running) = taken {
        running.child.kill().ok();
        running.child.wait().ok();
    }
}

fn pump<R: Read + Send + 'static>(
    source: R,
    emit: Emit,
    event: String,
    path: String,
    rewrite: Option<(String, String)>,
    job_id: u64,
    input_key: InputKey,
) -> JoinHandle<()> {
    thread::spawn(move || {
        for line in BufReader::new(source).lines().map_while(Result::ok) {
            let line = rewrite
                .as_ref()
                .map(|(from, to)| line.replace(from, to))
                .unwrap_or(line);
            emit(
                &event,
                json!({
                    "path": path,
                    "line": line,
                    "jobId": job_id,
                    "inputKey": input_key,
                }),
            );
        }
    })
}

fn spawn(
    jobs: &Jobs,
    kind: &str,
    target: &Path,
    command: Command,
    emit: Emit,
    input_key: InputKey,
    completion: Option<Completion>,
) -> Result<Started, String> {
    spawn_mapped(
        jobs, kind, target, command, emit, None, input_key, completion,
    )
}

fn spawn_mapped(
    jobs: &Jobs,
    kind: &str,
    target: &Path,
    mut command: Command,
    emit: Emit,
    rewrite: Option<(String, String)>,
    input_key: InputKey,
    completion: Option<Completion>,
) -> Result<Started, String> {
    let job_id = NEXT_JOB.fetch_add(1, Ordering::Relaxed);
    let mut child = command
        .stdin(Stdio::null())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .map_err(|cause| format!("cannot start {kind}: {cause}"))?;
    let out = child.stdout.take();
    let err = child.stderr.take();
    let key = key(kind, target);
    let path = target.display().to_string();
    let event = format!("{kind}-output");
    let is_build = kind == "build";

    if is_build {
        let mut book = held(jobs);
        if let Some(existing) = book.running.get(&key) {
            let existing_id = existing.id;
            drop(book);
            child.kill().ok();
            child.wait().ok();
            return Err(format!("build job {existing_id} is already running for {path}"));
        }
        book.running.insert(
            key.clone(),
            Running {
                id: job_id,
                child,
            },
        );
    } else {
        discard(jobs, &key);
        held(jobs).running.insert(
            key.clone(),
            Running {
                id: job_id,
                child,
            },
        );
    }
    if is_build {
        emit("artifact-status-changed", json!({ "path": path }));
    }

    let pumps = vec![
        out.map(|pipe| {
            pump(
                pipe,
                emit.clone(),
                event.clone(),
                path.clone(),
                rewrite.clone(),
                job_id,
                input_key.clone(),
            )
        }),
        err.map(|pipe| {
            pump(
                pipe,
                emit.clone(),
                event.clone(),
                path.clone(),
                rewrite,
                job_id,
                input_key.clone(),
            )
        }),
    ];
    let jobs = jobs.clone();
    let finished = format!("{kind}-finished");
    let started = Started {
        job_id,
        input_key: input_key.clone(),
    };
    thread::spawn(move || {
        for handle in pumps.into_iter().flatten() {
            handle.join().ok();
        }
        let taken = {
            let mut book = held(&jobs);
            book.running
                .get(&key)
                .is_some_and(|running| running.id == job_id)
                .then(|| book.running.remove(&key))
                .flatten()
        };
        let mut code = taken
            .and_then(|mut running| running.child.wait().ok())
            .and_then(|status| status.code());
        if code == Some(0) {
            if let Some(completion) = completion {
                let mut artifact = completion.artifact;
                artifact.completed_unix_ms = completed_unix_ms();
                let recorded = (completion.finalize)(artifact)
                    .and_then(|artifact| (completion.record)(completion.name, artifact));
                if let Err(cause) = recorded {
                    emit(
                        &event,
                        json!({
                            "path": path,
                            "line": format!("cannot record built artifact: {cause}"),
                            "jobId": job_id,
                            "inputKey": input_key,
                        }),
                    );
                    code = Some(1);
                }
            }
        }
        if is_build {
            emit("artifact-status-changed", json!({ "path": path }));
        }
        emit(
            &finished,
            json!({
                "path": path,
                "code": code,
                "jobId": job_id,
                "inputKey": input_key,
            }),
        );
    });
    Ok(started)
}

#[cfg(unix)]
fn interrupt(pid: u32) {
    unsafe {
        libc::kill(pid as i32, libc::SIGINT);
    }
}

#[cfg(not(unix))]
fn interrupt(_pid: u32) {}

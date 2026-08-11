use std::collections::HashMap;
use std::ffi::OsStr;
use std::io::{BufRead, BufReader, Read};
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::sync::{Arc, Mutex, MutexGuard, PoisonError};
use std::thread::{self, JoinHandle};
use std::time::{Duration, SystemTime};

use serde::Serialize;
use serde_json::{json, Value};

use crate::document::canonical;

pub type Emit = Arc<dyn Fn(&str, Value) + Send + Sync>;

#[derive(Default, Clone)]
pub struct Jobs(Arc<Mutex<HashMap<String, Child>>>);

const MARKER: &str = "include/cler.hpp";
const EXAMPLES: &str = "desktop_examples";
const STANDARD: &str = "-std=c++17";
const GRACE: Duration = Duration::from_secs(3);

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Target {
    pub available: bool,
    pub reason: Option<String>,
    pub name: String,
    pub build_dir: Option<String>,
    pub binary: Option<String>,
}

pub fn repo_root(target: &Path) -> Option<PathBuf> {
    target
        .ancestors()
        .find(|dir| dir.join(MARKER).is_file())
        .map(Path::to_path_buf)
}

pub fn check(jobs: &Jobs, path: &str, emit: Emit) -> Result<(), String> {
    let source = canonical(path)?;
    check_draft(jobs, path, &source, emit)
}

pub fn check_draft(jobs: &Jobs, path: &str, draft: &Path, emit: Emit) -> Result<(), String> {
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
    spawn_mapped(
        jobs,
        "check",
        &target,
        command,
        emit,
        Some((draft.display().to_string(), target.display().to_string())),
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
    })
}

pub fn build(jobs: &Jobs, path: &str, emit: Emit) -> Result<(), String> {
    let found = find_target(path)?;
    let dir = usable(&found)?;
    let mut command = Command::new("cmake");
    command.args(["--build", dir, "--target", &found.name, "--parallel"]);
    command.arg(cores().to_string());
    spawn(jobs, "build", &canonical(path)?, command, emit)
}

pub fn build_draft(jobs: &Jobs, path: &str, draft: &Path, emit: Emit) -> Result<(), String> {
    let target = canonical(path)?;
    let found = find_target(path)?;
    usable(&found)?;
    let (source_dir, build_dir, _) = draft_tree(&target, draft)?;
    prepare_overlay(&target, draft, &source_dir)?;
    let script = draft_runner(&found, &source_dir, &build_dir)?;
    let mut command = Command::new("cmake");
    command.arg("-P").arg(script);
    spawn(jobs, "build", &target, command, emit)
}

pub fn start(jobs: &Jobs, path: &str, emit: Emit) -> Result<(), String> {
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
    spawn(jobs, "run", &canonical(path)?, command, emit)
}

pub fn start_draft(jobs: &Jobs, path: &str, draft: &Path, emit: Emit) -> Result<(), String> {
    let target = canonical(path)?;
    let found = find_target(path)?;
    usable(&found)?;
    let (_, _, binary) = draft_tree(&target, draft)?;
    if !binary.is_file() {
        return Err(format!(
            "{} is not built yet; build the temporary draft first",
            binary.display()
        ));
    }
    let mut command = Command::new(&binary);
    if let Some(dir) = binary.parent() {
        command.current_dir(dir);
    }
    spawn(jobs, "run", &target, command, emit)
}

pub fn stop(jobs: &Jobs, path: &str) -> Result<(), String> {
    let target = canonical(path)?;
    let key = key("run", &target);
    let Some(pid) = held(jobs).get(&key).map(Child::id) else {
        return Err(format!("nothing is running for {}", target.display()));
    };
    interrupt(pid);
    let jobs = jobs.clone();
    thread::spawn(move || {
        thread::sleep(GRACE);
        discard(&jobs, &key);
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
        reason: Some(reason),
        name,
        build_dir: None,
        binary: None,
    }
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

fn draft_tree(target: &Path, draft: &Path) -> Result<(PathBuf, PathBuf, PathBuf), String> {
    let root = repo_root(target).ok_or_else(|| outside(target))?;
    let relative = target.strip_prefix(&root).map_err(|_| outside(target))?;
    let workspace = draft
        .parent()
        .ok_or_else(|| format!("{} has no working directory", draft.display()))?;
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
                ensure_link(draft, &destination)?;
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

#[cfg(not(unix))]
fn ensure_link(_target: &Path, _link: &Path) -> Result<(), String> {
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

fn held(jobs: &Jobs) -> MutexGuard<'_, HashMap<String, Child>> {
    jobs.0.lock().unwrap_or_else(PoisonError::into_inner)
}

fn discard(jobs: &Jobs, key: &str) {
    let taken = held(jobs).remove(key);
    if let Some(mut child) = taken {
        child.kill().ok();
        child.wait().ok();
    }
}

fn pump<R: Read + Send + 'static>(
    source: R,
    emit: Emit,
    event: String,
    path: String,
    rewrite: Option<(String, String)>,
) -> JoinHandle<()> {
    thread::spawn(move || {
        for line in BufReader::new(source).lines().map_while(Result::ok) {
            let line = rewrite
                .as_ref()
                .map(|(from, to)| line.replace(from, to))
                .unwrap_or(line);
            emit(&event, json!({ "path": path, "line": line }));
        }
    })
}

fn spawn(
    jobs: &Jobs,
    kind: &str,
    target: &Path,
    command: Command,
    emit: Emit,
) -> Result<(), String> {
    spawn_mapped(jobs, kind, target, command, emit, None)
}

fn spawn_mapped(
    jobs: &Jobs,
    kind: &str,
    target: &Path,
    mut command: Command,
    emit: Emit,
    rewrite: Option<(String, String)>,
) -> Result<(), String> {
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

    discard(jobs, &key);
    held(jobs).insert(key.clone(), child);

    let pumps = vec![
        out.map(|pipe| {
            pump(
                pipe,
                emit.clone(),
                event.clone(),
                path.clone(),
                rewrite.clone(),
            )
        }),
        err.map(|pipe| pump(pipe, emit.clone(), event, path.clone(), rewrite)),
    ];
    let jobs = jobs.clone();
    let finished = format!("{kind}-finished");
    thread::spawn(move || {
        for handle in pumps.into_iter().flatten() {
            handle.join().ok();
        }
        let taken = held(&jobs).remove(&key);
        let code = taken
            .and_then(|mut child| child.wait().ok())
            .and_then(|status| status.code());
        emit(&finished, json!({ "path": path, "code": code }));
    });
    Ok(())
}

#[cfg(unix)]
fn interrupt(pid: u32) {
    unsafe {
        libc::kill(pid as i32, libc::SIGINT);
    }
}

#[cfg(not(unix))]
fn interrupt(_pid: u32) {}

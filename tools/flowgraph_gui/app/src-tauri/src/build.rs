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
    let target = canonical(path)?;
    let root = repo_root(&target).ok_or_else(|| outside(&target))?;
    let mut command = Command::new("g++");
    command
        .arg("-fsyntax-only")
        .arg(STANDARD)
        .arg("-fdiagnostics-color=never");
    for dir in includes(&root) {
        command.arg(format!("-I{}", dir.display()));
    }
    command.arg(&target).current_dir(&root);
    spawn(jobs, "check", &target, command, emit)
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
    thread::available_parallelism().map_or(2, |count| count.get())
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
) -> JoinHandle<()> {
    thread::spawn(move || {
        for line in BufReader::new(source).lines().map_while(Result::ok) {
            emit(&event, json!({ "path": path, "line": line }));
        }
    })
}

fn spawn(
    jobs: &Jobs,
    kind: &str,
    target: &Path,
    mut command: Command,
    emit: Emit,
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
        out.map(|pipe| pump(pipe, emit.clone(), event.clone(), path.clone())),
        err.map(|pipe| pump(pipe, emit.clone(), event, path.clone())),
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

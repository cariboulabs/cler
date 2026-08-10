pub mod document;

use std::ffi::OsStr;
use std::path::Path;
use std::process::{Command, Stdio};
use std::sync::{Mutex, MutexGuard, PoisonError};

use notify::{EventKind, RecommendedWatcher, RecursiveMode, Watcher};
use serde::Serialize;
use serde_json::Value;
use tauri::{AppHandle, Emitter, Manager, State};

use document::{DocumentState, Documents};

const EXTERNAL_CHANGE_EVENT: &str = "document-changed-externally";

const NEEDS_TERMINAL: [&str; 17] = [
    "vi", "vim", "nvim", "neovim", "nano", "pico", "emacs", "micro", "kak", "helix", "hx", "joe",
    "ed", "mg", "ne", "vis", "amp",
];

#[cfg(target_os = "macos")]
const OPENER: &str = "open";
#[cfg(not(target_os = "macos"))]
const OPENER: &str = "xdg-open";

type FileWatcher = Mutex<RecommendedWatcher>;

#[derive(Clone, Serialize)]
struct ExternalChange {
    path: String,
}

#[tauri::command]
fn open_document(
    path: String,
    docs: State<'_, Documents>,
    watcher: State<'_, FileWatcher>,
) -> Result<DocumentState, String> {
    let target = document::canonical(&path)?;
    watch_parent(&watcher, &target)?;
    document::open(&docs, &path)
}

#[tauri::command]
fn close_document(
    path: String,
    docs: State<'_, Documents>,
    watcher: State<'_, FileWatcher>,
) -> Result<(), String> {
    if let Some(dir) = document::close(&docs, &path) {
        held(watcher.inner()).unwatch(&dir).ok();
    }
    Ok(())
}

#[tauri::command]
fn apply_commands(
    path: String,
    base_revision: u64,
    commands: Vec<Value>,
    docs: State<'_, Documents>,
) -> Result<DocumentState, String> {
    document::apply(&docs, &path, base_revision, commands)
}

#[tauri::command]
fn undo(path: String, docs: State<'_, Documents>) -> Result<DocumentState, String> {
    document::undo(&docs, &path)
}

#[tauri::command]
fn redo(path: String, docs: State<'_, Documents>) -> Result<DocumentState, String> {
    document::redo(&docs, &path)
}

#[tauri::command]
fn reload_document(path: String, docs: State<'_, Documents>) -> Result<DocumentState, String> {
    document::reload(&docs, &path)
}

#[tauri::command]
fn parse_file(path: String, docs: State<'_, Documents>) -> Result<String, String> {
    document::parse_file(&docs, &path)
}

#[tauri::command]
fn open_in_editor(path: String, line: u32) -> Result<(), String> {
    let preference = std::env::var("VISUAL")
        .ok()
        .filter(|text| !text.trim().is_empty())
        .or_else(|| std::env::var("EDITOR").ok());
    match preference
        .as_deref()
        .and_then(|editor| editor_command(editor, &path, line))
    {
        Some((program, args)) => spawn_detached(&program, &args),
        None => spawn_detached(OPENER, std::slice::from_ref(&path)),
    }
}

fn line_arguments(editor: &str, path: &str, line: u32) -> Vec<String> {
    match editor {
        "code" | "code-insiders" | "codium" | "vscodium" | "cursor" | "windsurf" => {
            vec!["--goto".to_string(), format!("{path}:{line}")]
        }
        "subl" | "sublime_text" | "zed" | "zeditor" => vec![format!("{path}:{line}")],
        "gvim" | "mvim" | "gedit" | "xed" | "emacsclient" => {
            vec![format!("+{line}"), path.to_string()]
        }
        "kate" | "idea" | "clion" | "pycharm" | "webstorm" | "rustrover" | "goland"
        | "phpstorm" => vec!["--line".to_string(), line.to_string(), path.to_string()],
        _ => vec![path.to_string()],
    }
}

pub fn editor_command(editor: &str, path: &str, line: u32) -> Option<(String, Vec<String>)> {
    let mut words = editor.split_whitespace();
    let program = words.next()?;
    let name = Path::new(program).file_stem().and_then(OsStr::to_str)?;
    if NEEDS_TERMINAL.contains(&name) {
        return None;
    }
    let mut args: Vec<String> = words.map(str::to_string).collect();
    args.extend(line_arguments(name, path, line));
    Some((program.to_string(), args))
}

fn spawn_detached(program: &str, args: &[String]) -> Result<(), String> {
    Command::new(program)
        .args(args)
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .spawn()
        .map(drop)
        .map_err(|cause| format!("cannot launch {program}: {cause}"))
}

fn held(watcher: &FileWatcher) -> MutexGuard<'_, RecommendedWatcher> {
    watcher.lock().unwrap_or_else(PoisonError::into_inner)
}

fn watch_parent(watcher: &FileWatcher, target: &Path) -> Result<(), String> {
    let dir = target
        .parent()
        .ok_or_else(|| format!("{} has no parent directory", target.display()))?;
    held(watcher)
        .watch(dir, RecursiveMode::NonRecursive)
        .map_err(|cause| format!("cannot watch {}: {cause}", dir.display()))
}

fn build_watcher(app: AppHandle) -> notify::Result<RecommendedWatcher> {
    notify::recommended_watcher(move |event: notify::Result<notify::Event>| {
        let Ok(event) = event else {
            return;
        };
        if !matches!(
            event.kind,
            EventKind::Create(_) | EventKind::Modify(_) | EventKind::Remove(_)
        ) {
            return;
        }
        for path in event.paths {
            report_disk_event(&app, &path);
        }
    })
}

fn report_disk_event(app: &AppHandle, path: &Path) {
    let Some(docs) = app.try_state::<Documents>() else {
        return;
    };
    if document::note_disk_event(&docs, path) {
        let payload = ExternalChange {
            path: path.display().to_string(),
        };
        let _ = app.emit(EXTERNAL_CHANGE_EVENT, payload);
    }
}

pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .manage(Documents::default())
        .setup(|app| {
            let watcher = build_watcher(app.handle().clone())?;
            app.manage(Mutex::new(watcher));
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            open_document,
            close_document,
            apply_commands,
            undo,
            redo,
            reload_document,
            parse_file,
            open_in_editor
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}

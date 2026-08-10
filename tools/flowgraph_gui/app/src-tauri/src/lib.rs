pub mod document;

use std::path::Path;
use std::sync::{Mutex, MutexGuard, PoisonError};

use notify::{EventKind, RecommendedWatcher, RecursiveMode, Watcher};
use serde::Serialize;
use serde_json::Value;
use tauri::{AppHandle, Emitter, Manager, State};

use document::{DocumentState, Documents};

const EXTERNAL_CHANGE_EVENT: &str = "document-changed-externally";

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
            parse_file
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}

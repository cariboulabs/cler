#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod graph;

use std::path::PathBuf;

#[tauri::command]
fn parse_file(path: String) -> Result<String, String> {
    graph::parse(&PathBuf::from(path))
}

fn main() {
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .invoke_handler(tauri::generate_handler![parse_file])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}

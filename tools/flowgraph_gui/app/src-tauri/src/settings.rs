use std::path::{Path, PathBuf};
use std::sync::{PoisonError, RwLock};

use serde::{Deserialize, Serialize};

const FILE: &str = "settings.json";
const MARKER: &str = "include/cler.hpp";
const PALETTE_DIR: &str = "desktop_blocks";

#[derive(Debug, Default, Clone, Serialize, Deserialize, PartialEq)]
#[serde(rename_all = "camelCase", default)]
pub struct AppSettings {
    pub cler_root: Option<String>,
    pub block_libraries: Vec<String>,
    pub assistant_model: Option<String>,
}

static CURRENT: RwLock<AppSettings> = RwLock::new(AppSettings {
    cler_root: None,
    block_libraries: Vec::new(),
    assistant_model: None,
});

pub fn current() -> AppSettings {
    CURRENT
        .read()
        .unwrap_or_else(PoisonError::into_inner)
        .clone()
}

pub fn init(dir: &Path) {
    let loaded = read(dir);
    *CURRENT.write().unwrap_or_else(PoisonError::into_inner) = loaded;
}

pub fn read(dir: &Path) -> AppSettings {
    std::fs::read_to_string(dir.join(FILE))
        .ok()
        .and_then(|text| serde_json::from_str(&text).ok())
        .unwrap_or_default()
}

pub fn store(dir: &Path, settings: &AppSettings) -> Result<AppSettings, String> {
    let validated = prune(validate(settings)?);
    std::fs::create_dir_all(dir)
        .map_err(|cause| format!("cannot create {}: {cause}", dir.display()))?;
    let text = serde_json::to_string_pretty(&validated)
        .map_err(|cause| format!("cannot serialize settings: {cause}"))?;
    std::fs::write(dir.join(FILE), text)
        .map_err(|cause| format!("cannot write {}: {cause}", dir.join(FILE).display()))?;
    *CURRENT.write().unwrap_or_else(PoisonError::into_inner) = validated.clone();
    Ok(validated)
}

fn validate(settings: &AppSettings) -> Result<AppSettings, String> {
    let mut validated = settings.clone();
    if let Some(root) = settings.cler_root.as_deref() {
        let repo = Path::new(root)
            .ancestors()
            .find(|dir| dir.join(MARKER).is_file())
            .ok_or_else(|| format!("{root} is not inside a cler repository (no {MARKER})"))?;
        validated.cler_root = Some(repo.display().to_string());
    }
    for library in &settings.block_libraries {
        if !Path::new(library).is_dir() {
            return Err(format!("{library} is not a directory"));
        }
    }
    Ok(validated)
}

fn prune(settings: AppSettings) -> AppSettings {
    let mut dirs: Vec<PathBuf> = settings
        .block_libraries
        .iter()
        .filter_map(|library| Path::new(library).canonicalize().ok())
        .collect();
    if let Some(palette) = cler_root_fallback().map(|root| root.join(PALETTE_DIR)) {
        dirs.retain(|dir| !dir.starts_with(&palette));
    }
    prune_nested(&mut dirs);
    AppSettings {
        cler_root: settings.cler_root,
        block_libraries: dirs
            .into_iter()
            .map(|dir| dir.display().to_string())
            .collect(),
        assistant_model: settings
            .assistant_model
            .filter(|model| !model.trim().is_empty()),
    }
}

pub fn cler_root_fallback() -> Option<PathBuf> {
    configured_root().or_else(builtin_root)
}

fn configured_root() -> Option<PathBuf> {
    let settings = current();
    let root = PathBuf::from(settings.cler_root?);
    root.join(MARKER).is_file().then_some(root)
}

fn builtin_root() -> Option<PathBuf> {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .ancestors()
        .find(|dir| dir.join(MARKER).is_file())
        .map(Path::to_path_buf)
}

#[cfg(test)]
pub(crate) fn test_guard() -> std::sync::MutexGuard<'static, ()> {
    static LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());
    LOCK.lock().unwrap_or_else(PoisonError::into_inner)
}

pub fn block_library_dirs() -> Vec<PathBuf> {
    let mut dirs: Vec<PathBuf> = current()
        .block_libraries
        .iter()
        .map(PathBuf::from)
        .filter_map(|dir| dir.canonicalize().ok())
        .filter(|dir| dir.is_dir())
        .collect();
    prune_nested(&mut dirs);
    dirs
}

pub fn prune_nested(dirs: &mut Vec<PathBuf>) {
    dirs.sort();
    dirs.dedup();
    let ordered = dirs.clone();
    dirs.retain(|dir| !ordered.iter().any(|other| other != dir && dir.starts_with(other)));
}

#[cfg(test)]
mod tests {
    use super::*;

    fn temp(tag: &str) -> PathBuf {
        let dir = std::env::temp_dir().join(format!("cler-settings-{tag}-{}", std::process::id()));
        std::fs::remove_dir_all(&dir).ok();
        std::fs::create_dir_all(&dir).expect("temp dir");
        dir
    }

    #[test]
    fn settings_round_trip_and_validation() {
        let _guard = test_guard();
        let dir = temp("roundtrip");
        assert_eq!(read(&dir), AppSettings::default());

        let fake_repo = temp("fake-repo");
        std::fs::create_dir_all(fake_repo.join("include")).expect("include");
        std::fs::write(fake_repo.join(MARKER), "#pragma once\n").expect("marker");
        let library = temp("library");

        let wanted = AppSettings {
            assistant_model: None,
            cler_root: Some(fake_repo.display().to_string()),
            block_libraries: vec![library.display().to_string()],
        };
        store(&dir, &wanted).expect("store");
        assert_eq!(read(&dir), wanted);

        let bogus = AppSettings {
            assistant_model: None,
            cler_root: Some("/nonexistent/never".to_string()),
            block_libraries: Vec::new(),
        };
        assert!(store(&dir, &bogus).is_err());
        assert_eq!(read(&dir), wanted);

        let inside = AppSettings {
            assistant_model: None,
            cler_root: Some(fake_repo.join("include").display().to_string()),
            block_libraries: Vec::new(),
        };
        let normalized = store(&dir, &inside).expect("subdir normalizes to the repo root");
        assert_eq!(normalized.cler_root, Some(fake_repo.display().to_string()));
        store(&dir, &wanted).expect("restore wanted");

        let redundant = AppSettings {
            assistant_model: None,
            cler_root: Some(fake_repo.display().to_string()),
            block_libraries: vec![
                fake_repo.join(PALETTE_DIR).display().to_string(),
                library.display().to_string(),
                library.join("nested").display().to_string(),
            ],
        };
        std::fs::create_dir_all(fake_repo.join(PALETTE_DIR)).expect("palette dir");
        std::fs::create_dir_all(library.join("nested")).expect("nested dir");
        let stored = store(&dir, &redundant).expect("redundant libraries prune instead of failing");
        assert_eq!(stored.block_libraries.len(), 1);
        assert!(stored.block_libraries[0].ends_with(library.file_name().unwrap().to_str().unwrap()));
        store(&dir, &wanted).expect("restore");

        let external = temp("external");
        let outside_file = external.join("flowgraph.cpp");
        std::fs::write(&outside_file, "int main() { return 0; }\n").expect("external file");
        assert_eq!(crate::build::repo_root(&outside_file), Some(fake_repo.clone()));

        store(&dir, &AppSettings::default()).expect("clear");
        let builtin = crate::build::repo_root(&outside_file).expect("builtin repo fallback");
        assert!(builtin.join(MARKER).is_file());
        std::fs::remove_dir_all(&external).ok();
        std::fs::remove_dir_all(&dir).ok();
        std::fs::remove_dir_all(&fake_repo).ok();
        std::fs::remove_dir_all(&library).ok();
    }
}

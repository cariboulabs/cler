use std::path::{Path, PathBuf};
use std::sync::{PoisonError, RwLock};

use serde::{Deserialize, Serialize};

const FILE: &str = "settings.json";
const MARKER: &str = "include/cler.hpp";

#[derive(Debug, Default, Clone, Serialize, Deserialize, PartialEq)]
#[serde(rename_all = "camelCase", default)]
pub struct AppSettings {
    pub cler_root: Option<String>,
    pub block_libraries: Vec<String>,
}

static CURRENT: RwLock<AppSettings> = RwLock::new(AppSettings {
    cler_root: None,
    block_libraries: Vec::new(),
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
    let validated = validate(settings)?;
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
    if let Some(root) = settings.cler_root.as_deref() {
        if !Path::new(root).join(MARKER).is_file() {
            return Err(format!("{root} is not a cler repository (no {MARKER})"));
        }
    }
    for library in &settings.block_libraries {
        if !Path::new(library).is_dir() {
            return Err(format!("{library} is not a directory"));
        }
    }
    Ok(settings.clone())
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

pub fn block_library_dirs() -> Vec<PathBuf> {
    current()
        .block_libraries
        .iter()
        .map(PathBuf::from)
        .filter(|dir| dir.is_dir())
        .collect()
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
        let dir = temp("roundtrip");
        assert_eq!(read(&dir), AppSettings::default());

        let fake_repo = temp("fake-repo");
        std::fs::create_dir_all(fake_repo.join("include")).expect("include");
        std::fs::write(fake_repo.join(MARKER), "#pragma once\n").expect("marker");
        let library = temp("library");

        let wanted = AppSettings {
            cler_root: Some(fake_repo.display().to_string()),
            block_libraries: vec![library.display().to_string()],
        };
        store(&dir, &wanted).expect("store");
        assert_eq!(read(&dir), wanted);

        let bogus = AppSettings {
            cler_root: Some("/nonexistent/never".to_string()),
            block_libraries: Vec::new(),
        };
        assert!(store(&dir, &bogus).is_err());
        assert_eq!(read(&dir), wanted);

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

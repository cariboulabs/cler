use std::collections::{BTreeMap, HashMap};
use std::path::Path;

use serde::{Deserialize, Serialize};
use serde_json::{Map, Value};

pub const CATALOG_VERSION: u32 = 1;

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct InputKey {
    pub inputs: BTreeMap<String, String>,
    pub recipe_sha256: String,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ArtifactRecord {
    pub input_key: InputKey,
    pub producer: String,
    pub artifact_path: String,
    pub completed_unix_ms: u64,
    #[serde(flatten)]
    pub extra: Map<String, Value>,
}

#[derive(Clone, Debug, Serialize)]
#[serde(
    tag = "state",
    rename_all = "snake_case",
    rename_all_fields = "camelCase"
)]
pub enum ArtifactStatus {
    Unavailable { reason: String },
    NeedsBuild { reason: String },
    Building { job_id: u64 },
    Ready { artifact_path: String },
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ArtifactCatalog {
    #[serde(default)]
    pub version: u32,
    #[serde(default)]
    pub artifacts: HashMap<String, ArtifactRecord>,
    #[serde(flatten)]
    pub extra: Map<String, Value>,
}

impl ArtifactCatalog {
    pub fn read(value: &Value) -> Self {
        serde_json::from_value(value.clone()).unwrap_or_default()
    }

    pub fn get(&self, name: &str) -> Option<&ArtifactRecord> {
        (self.version == CATALOG_VERSION)
            .then(|| self.artifacts.get(name))
            .flatten()
    }

    pub fn put(&mut self, name: String, record: ArtifactRecord) -> Result<(), String> {
        if self.version != 0 && self.version != CATALOG_VERSION {
            return Err(format!(
                "artifact catalog version {} is newer than supported version {CATALOG_VERSION}",
                self.version
            ));
        }
        self.version = CATALOG_VERSION;
        self.artifacts.insert(name, record);
        Ok(())
    }

    pub fn prune_missing(&mut self) -> bool {
        if self.version != CATALOG_VERSION {
            return false;
        }
        let before = self.artifacts.len();
        self.artifacts
            .retain(|_, record| Path::new(&record.artifact_path).is_file());
        self.artifacts.len() != before
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn artifact(path: &Path) -> ArtifactRecord {
        ArtifactRecord {
            input_key: InputKey {
                inputs: BTreeMap::new(),
                recipe_sha256: "recipe".to_string(),
            },
            producer: "test".to_string(),
            artifact_path: path.display().to_string(),
            completed_unix_ms: 1,
            extra: Map::new(),
        }
    }

    #[test]
    fn pruning_removes_only_missing_artifact_records() {
        let dir = std::env::temp_dir().join(format!(
            "cler-provenance-prune-{}",
            std::process::id()
        ));
        std::fs::create_dir_all(&dir).expect("temporary directory");
        let present = dir.join("present");
        let missing = dir.join("missing");
        std::fs::write(&present, "artifact").expect("present artifact");
        std::fs::remove_file(&missing).ok();
        let mut catalog = ArtifactCatalog {
            version: CATALOG_VERSION,
            artifacts: HashMap::from([
                ("present".to_string(), artifact(&present)),
                ("missing".to_string(), artifact(&missing)),
            ]),
            extra: Map::new(),
        };

        assert!(catalog.prune_missing());
        assert!(catalog.get("present").is_some());
        assert!(catalog.get("missing").is_none());
    }

    #[test]
    fn pruning_does_not_rewrite_a_future_catalog() {
        let mut catalog = ArtifactCatalog {
            version: CATALOG_VERSION + 1,
            artifacts: HashMap::from([(
                "future".to_string(),
                artifact(Path::new("/missing")),
            )]),
            extra: Map::new(),
        };

        assert!(!catalog.prune_missing());
        assert_eq!(catalog.artifacts.len(), 1);
    }

    #[test]
    fn artifact_status_fields_follow_the_frontend_contract() {
        assert_eq!(
            serde_json::to_value(ArtifactStatus::Building { job_id: 7 }).expect("building status"),
            serde_json::json!({ "state": "building", "jobId": 7 })
        );
        assert_eq!(
            serde_json::to_value(ArtifactStatus::Ready {
                artifact_path: "/tmp/example".to_string(),
            })
            .expect("ready status"),
            serde_json::json!({ "state": "ready", "artifactPath": "/tmp/example" })
        );
    }
}

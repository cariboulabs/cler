use std::collections::{BTreeMap, HashMap};

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
#[serde(tag = "state", rename_all = "snake_case")]
pub enum ArtifactStatus {
    Unavailable { reason: String },
    NeedsBuild { reason: String },
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
}

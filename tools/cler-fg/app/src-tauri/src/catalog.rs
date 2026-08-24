use std::collections::HashMap;
use std::sync::OnceLock;

use serde::{Deserialize, Serialize};

const CATALOG: &str = include_str!("../models.json");

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct CatalogModel {
    pub id: String,
    pub name: String,
    pub context: usize,
    pub output: usize,
    pub input_cost: f64,
    pub output_cost: f64,
    pub released: String,
}

fn catalog() -> &'static HashMap<String, Vec<CatalogModel>> {
    static PARSED: OnceLock<HashMap<String, Vec<CatalogModel>>> = OnceLock::new();
    PARSED.get_or_init(|| serde_json::from_str(CATALOG).unwrap_or_default())
}

pub fn models(provider: &str) -> &'static [CatalogModel] {
    catalog().get(vendor_of(provider)).map_or(&[], Vec::as_slice)
}

/// A ChatGPT subscription serves OpenAI's models through a different endpoint, so it
/// reads the same shelf of the catalog rather than carrying a duplicate of it.
fn vendor_of(provider: &str) -> &str {
    match provider {
        "openai-codex" => "openai",
        other => other,
    }
}

pub fn find(provider: &str, model: &str) -> Option<&'static CatalogModel> {
    models(provider).iter().find(|known| known.id == model)
}

/// Names a model the catalog has never heard of after its own id, so an id typed by
/// hand or fetched live still reads as a choice rather than a blank.
pub fn name_of(provider: &str, model: &str) -> String {
    find(provider, model).map_or_else(|| model.to_string(), |known| known.name.clone())
}

pub fn context_of(provider: &str, model: &str) -> Option<usize> {
    find(provider, model)
        .map(|known| known.context)
        .filter(|context| *context > 0)
}

/// Merges what the provider says it has today with what the catalog knows about those
/// ids. The live list is the authority on availability; the catalog supplies the human
/// name and the context window it never returns.
pub fn merge(provider: &str, live: Vec<String>) -> Vec<Listed> {
    if live.is_empty() {
        return models(provider)
            .iter()
            .map(|known| Listed {
                id: known.id.clone(),
                name: known.name.clone(),
                context: known.context,
                input_cost: known.input_cost,
                output_cost: known.output_cost,
            })
            .collect();
    }
    let mut listed: Vec<Listed> = live
        .into_iter()
        .filter(|id| find(provider, id).is_some())
        .map(|id| {
            let known = find(provider, &id);
            Listed {
                name: known.map_or_else(|| id.clone(), |one| one.name.clone()),
                context: known.map_or(0, |one| one.context),
                input_cost: known.map_or(0.0, |one| one.input_cost),
                output_cost: known.map_or(0.0, |one| one.output_cost),
                id,
            }
        })
        .collect();
    listed.sort_by(|left, right| right.context.cmp(&left.context).then(left.id.cmp(&right.id)));
    listed
}

#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct Listed {
    pub id: String,
    pub name: String,
    pub context: usize,
    pub input_cost: f64,
    pub output_cost: f64,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_vendored_catalog_parses_and_carries_both_providers() {
        assert!(!models("anthropic").is_empty());
        assert!(!models("openai").is_empty());
        assert!(models("mistral").is_empty());
    }

    #[test]
    fn every_entry_is_usable_as_a_choice() {
        for provider in ["anthropic", "openai"] {
            for model in models(provider) {
                assert!(!model.id.trim().is_empty(), "{provider} has a nameless id");
                assert!(!model.name.trim().is_empty(), "{} has no name", model.id);
                assert!(model.context > 0, "{} has no context window", model.id);
            }
        }
    }

    #[test]
    fn a_live_list_decides_what_is_offered_and_the_catalog_names_it() {
        let live = vec![
            "claude-opus-5".to_string(),
            "claude-3-retired".to_string(),
            "text-embedding-3-small".to_string(),
        ];
        let listed = merge("anthropic", live);
        assert_eq!(listed.len(), 1, "only ids the catalog knows survive: {listed:?}");
        assert_eq!(listed[0].name, "Claude Opus 5");
        assert!(listed[0].context > 100_000);
    }

    #[test]
    fn without_a_live_list_the_catalog_stands_in() {
        let listed = merge("anthropic", Vec::new());
        assert_eq!(listed.len(), models("anthropic").len());
    }

    #[test]
    fn chatgpt_reads_openais_shelf() {
        assert_eq!(models("openai-codex"), models("openai"));
        assert!(!models("openai-codex").is_empty());
        assert_eq!(name_of("openai-codex", "gpt-5"), name_of("openai", "gpt-5"));
    }

    #[test]
    fn an_unknown_model_falls_back_to_its_own_id() {
        assert_eq!(name_of("anthropic", "claude-opus-5"), "Claude Opus 5");
        assert_eq!(name_of("openai", "gpt-9-imaginary"), "gpt-9-imaginary");
        assert_eq!(context_of("openai", "gpt-9-imaginary"), None);
        assert!(context_of("anthropic", "claude-opus-5").unwrap_or(0) > 100_000);
    }
}

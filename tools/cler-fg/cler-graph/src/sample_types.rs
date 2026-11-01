use crate::model::{Block, Site};
use crate::palette::is_whole_word;
use crate::palette_types::{BlockSpec, Direction};

const TIGHT: &[char] = &['<', '>', ',', '*', '&', ':'];
const NEVER_RESOLVED: &[&str] = &["typename", "decltype", "auto", "..."];

pub(crate) fn annotate(site: &mut Site, specs: &[&BlockSpec]) {
    let resolved: Vec<(Option<String>, Option<String>)> = site
        .edges
        .iter()
        .map(|edge| {
            (
                input_element(site, specs, &edge.to, &edge.port.name),
                output_element(site, specs, &edge.from, edge.arg_index),
            )
        })
        .collect();
    for (edge, (sample, source)) in site.edges.iter_mut().zip(resolved) {
        edge.type_conflict = matches!((&sample, &source), (Some(a), Some(b)) if a != b);
        edge.sample_type = sample;
        edge.source_type = source;
    }
}

fn input_element(site: &Site, specs: &[&BlockSpec], var: &str, port: &str) -> Option<String> {
    let (block, spec) = specced(site, specs, var)?;
    let found = spec
        .ports
        .iter()
        .find(|p| p.direction == Direction::Input && p.name == port)?;
    element(&found.element_type, spec, block)
}

fn output_element(
    site: &Site,
    specs: &[&BlockSpec],
    var: &str,
    arg_index: usize,
) -> Option<String> {
    let (block, spec) = specced(site, specs, var)?;
    let found = spec
        .outputs()
        .nth(arg_index.checked_sub(1)?)
        .filter(|p| !p.variable)?;
    element(&found.element_type, spec, block)
}

fn specced<'a>(
    site: &'a Site,
    specs: &'a [&BlockSpec],
    var: &str,
) -> Option<(&'a Block, &'a BlockSpec)> {
    let block = site.block(var)?;
    let spec = specs.iter().find(|s| s.name == block.type_name)?;
    Some((block, spec))
}

fn element(text: &str, spec: &BlockSpec, block: &Block) -> Option<String> {
    if spec
        .template_params
        .iter()
        .any(|param| dependent(text, &param.name))
    {
        return None;
    }
    let substituted = substitute(text, &bindings(spec, block));
    if opaque(&substituted, spec) {
        return None;
    }
    Some(tighten(&substituted))
}

fn bindings(spec: &BlockSpec, block: &Block) -> Vec<(String, String)> {
    spec.template_params
        .iter()
        .enumerate()
        .filter(|(_, param)| !param.pack)
        .filter_map(|(index, param)| {
            let value = block
                .template_args
                .get(index)
                .map(|arg| arg.resolved.clone().unwrap_or_else(|| arg.text.clone()))
                .or_else(|| param.default.clone())?;
            Some((param.name.clone(), value))
        })
        .collect()
}

fn dependent(text: &str, name: &str) -> bool {
    word_ends(text, name).any(|end| text[end..].starts_with("::"))
}

fn opaque(text: &str, spec: &BlockSpec) -> bool {
    text.trim().is_empty()
        || NEVER_RESOLVED.iter().any(|token| text.contains(token))
        || spec
            .template_params
            .iter()
            .any(|param| word_ends(text, &param.name).next().is_some())
}

fn word_ends<'a>(text: &'a str, name: &'a str) -> impl Iterator<Item = usize> + 'a {
    text.match_indices(name)
        .filter(move |(at, _)| is_whole_word(text, *at, at + name.len()))
        .map(move |(at, _)| at + name.len())
}

fn substitute(text: &str, bindings: &[(String, String)]) -> String {
    let mut out = String::with_capacity(text.len());
    let mut word = String::new();
    for character in text.chars() {
        if character.is_alphanumeric() || character == '_' {
            word.push(character);
            continue;
        }
        flush(&mut out, &mut word, bindings);
        out.push(character);
    }
    flush(&mut out, &mut word, bindings);
    out
}

fn flush(out: &mut String, word: &mut String, bindings: &[(String, String)]) {
    if word.is_empty() {
        return;
    }
    match bindings.iter().find(|(name, _)| name == word) {
        Some((_, value)) => out.push_str(value),
        None => out.push_str(word),
    }
    word.clear();
}

fn tighten(text: &str) -> String {
    let collapsed = text.split_whitespace().collect::<Vec<_>>().join(" ");
    let characters: Vec<char> = collapsed.chars().collect();
    let mut out = String::with_capacity(collapsed.len());
    for (index, character) in characters.iter().enumerate() {
        let clinging = out.chars().next_back().is_some_and(|p| TIGHT.contains(&p))
            || characters.get(index + 1).is_some_and(|n| TIGHT.contains(n));
        if *character == ' ' && clinging {
            continue;
        }
        out.push(*character);
    }
    out
}

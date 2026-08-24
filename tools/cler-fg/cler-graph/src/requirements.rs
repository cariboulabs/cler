use std::collections::{BTreeSet, HashMap, HashSet};

use tree_sitter::Node;

use crate::{parse_source, BlockSpec};

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct BlockRequirements {
    pub origins: Vec<String>,
    pub has_local_blocks: bool,
    pub unknown_block_types: Vec<String>,
    pub has_parse_errors: bool,
}

impl BlockRequirements {
    pub fn needs_fallback(&self) -> bool {
        !self.unknown_block_types.is_empty() || self.has_parse_errors
    }
}

pub fn required_block_origins(source: &str, palette: &[BlockSpec]) -> Vec<String> {
    block_requirements(source, palette).origins
}

pub fn block_requirements(source: &str, palette: &[BlockSpec]) -> BlockRequirements {
    let Ok(tree) = parse_source(source) else {
        return BlockRequirements {
            origins: Vec::new(),
            has_local_blocks: false,
            unknown_block_types: Vec::new(),
            has_parse_errors: true,
        };
    };

    let root = tree.root_node();
    let records = record_names(root, source);
    let known = known_origins(palette);
    let mut origins = BTreeSet::new();
    let mut unknown = BTreeSet::new();
    let mut has_local_blocks = false;

    collect_desktop_block_includes(root, source, &mut origins);

    for node in descendants(root) {
        if node.kind() != "type_identifier" {
            continue;
        }
        let name = text(node, source);
        if records.local_blocks.contains(name) {
            has_local_blocks = true;
            continue;
        }
        if records.other_records.contains(name) {
            continue;
        }
        if let Some(found) = known.get(name) {
            origins.extend(found.iter().cloned());
        } else if looks_like_block(name) {
            unknown.insert(name.to_string());
        }
    }

    for node in descendants(root) {
        if node.kind() != "call_expression" {
            continue;
        }
        let Some(function) = node.child_by_field_name("function") else {
            continue;
        };
        let name = terminal_name(function, source);
        if records.local_blocks.contains(name) {
            has_local_blocks = true;
        } else if records.other_records.contains(name) {
            continue;
        } else if let Some(found) = known.get(name) {
            origins.extend(found.iter().cloned());
        } else if looks_like_block(name) {
            unknown.insert(name.to_string());
        }
    }

    BlockRequirements {
        origins: origins.into_iter().collect(),
        has_local_blocks,
        unknown_block_types: unknown.into_iter().collect(),
        has_parse_errors: root.has_error(),
    }
}

#[derive(Default)]
struct Records {
    local_blocks: HashSet<String>,
    other_records: HashSet<String>,
}

fn record_names(root: Node, source: &str) -> Records {
    let mut records = Records::default();
    for node in descendants(root) {
        if !matches!(node.kind(), "struct_specifier" | "class_specifier") {
            continue;
        }
        let Some(name) = node.child_by_field_name("name") else {
            continue;
        };
        let name = text(name, source).to_string();
        if derives_block_base(node, source) {
            records.local_blocks.insert(name);
        } else {
            records.other_records.insert(name);
        }
    }
    records
}

fn derives_block_base(node: Node, source: &str) -> bool {
    let mut cursor = node.walk();
    let Some(bases) = node
        .children(&mut cursor)
        .find(|child| child.kind() == "base_class_clause")
    else {
        return false;
    };
    descendants(bases)
        .into_iter()
        .any(|child| child.kind() == "type_identifier" && text(child, source) == "BlockBase")
}

fn known_origins(palette: &[BlockSpec]) -> HashMap<String, BTreeSet<String>> {
    let mut known = HashMap::<String, BTreeSet<String>>::new();
    for spec in palette {
        if spec.origin.is_empty() {
            continue;
        }
        known
            .entry(spec.name.clone())
            .or_default()
            .insert(spec.origin.clone());
        for synonym in &spec.synonyms {
            known
                .entry(synonym.name.clone())
                .or_default()
                .insert(spec.origin.clone());
        }
    }
    known
}

fn terminal_name<'a>(node: Node<'a>, source: &'a str) -> &'a str {
    let value = text(node, source);
    value
        .rsplit("::")
        .next()
        .unwrap_or(value)
        .split('<')
        .next()
        .unwrap_or(value)
        .trim()
}

fn looks_like_block(name: &str) -> bool {
    name.ends_with("Block") && name != "BlockBase"
}

fn collect_desktop_block_includes(root: Node, source: &str, origins: &mut BTreeSet<String>) {
    for node in descendants(root) {
        if node.kind() != "preproc_include" {
            continue;
        }
        let Some(path) = node.child_by_field_name("path") else {
            continue;
        };
        let path = text(path, source);
        let path = path
            .strip_prefix('"')
            .and_then(|path| path.strip_suffix('"'))
            .or_else(|| {
                path.strip_prefix('<')
                    .and_then(|path| path.strip_suffix('>'))
            });
        let Some(path) = path else {
            continue;
        };
        let path = path.trim_start_matches("./");
        if path.starts_with("desktop_blocks/") {
            origins.insert(path.to_string());
        }
    }
}

fn descendants(node: Node) -> Vec<Node> {
    let mut out = Vec::new();
    let mut stack = vec![node];
    while let Some(current) = stack.pop() {
        out.push(current);
        let mut cursor = current.walk();
        let children: Vec<Node> = current.children(&mut cursor).collect();
        stack.extend(children.into_iter().rev());
    }
    out
}

fn text<'a>(node: Node, source: &'a str) -> &'a str {
    source.get(node.byte_range()).unwrap_or("")
}

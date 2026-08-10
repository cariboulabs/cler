use std::collections::HashSet;
use std::fmt;

use serde::{Deserialize, Serialize};
use tree_sitter::{Node, Tree};

use crate::model::{Block, FileModel, Reason, Runner, Site, Span, SCHEMA_VERSION};
use crate::parse::{descendants, named_children};
use crate::{parse_source, DocumentSession};

#[derive(Debug, Clone, Deserialize, Serialize)]
#[serde(tag = "command", rename_all = "snake_case", deny_unknown_fields)]
pub enum Command {
    SetParam {
        site: usize,
        block: String,
        ctor_arg_index: usize,
        new_text: String,
    },
    SetTemplateArg {
        site: usize,
        block: String,
        template_arg_index: usize,
        new_text: String,
    },
    SetDisplayName {
        site: usize,
        block: String,
        new_text: String,
    },
    SetConfig {
        site: usize,
        path: String,
        new_value: String,
    },
    Connect {
        site: usize,
        from: String,
        to: String,
        port: String,
        #[serde(default)]
        port_index: Option<usize>,
    },
    Disconnect {
        site: usize,
        edge: usize,
    },
    AddBlock {
        site: usize,
        #[serde(rename = "type")]
        type_name: String,
        #[serde(default)]
        template_args: Vec<String>,
        #[serde(default)]
        ctor_args: Vec<String>,
        var_name: String,
    },
    RemoveFromGraph {
        site: usize,
        block: String,
    },
    DeleteBlock {
        site: usize,
        block: String,
    },
}

#[derive(Debug, Clone, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct Transaction {
    pub version: String,
    pub base_revision: u64,
    pub commands: Vec<Command>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct Splice {
    pub start: usize,
    pub end: usize,
    pub text: String,
}

impl Splice {
    fn replace(span: Span, text: impl Into<String>) -> Self {
        Splice {
            start: span.start,
            end: span.end,
            text: text.into(),
        }
    }

    fn insert(at: usize, text: impl Into<String>) -> Self {
        Splice {
            start: at,
            end: at,
            text: text.into(),
        }
    }

    fn remove(start: usize, end: usize) -> Self {
        Splice {
            start,
            end,
            text: String::new(),
        }
    }

    fn span(&self) -> Span {
        Span {
            start: self.start,
            end: self.end,
        }
    }
}

#[derive(Debug, Clone, Serialize)]
pub struct ApplyOutcome {
    pub revision: u64,
    pub splices: Vec<Splice>,
}

enum PendingKind {
    Edit,
    Undo,
    Redo,
}

pub struct PendingApply {
    source: String,
    tree: Tree,
    splices: Vec<Splice>,
    changed: bool,
    kind: PendingKind,
}

impl PendingApply {
    pub fn source(&self) -> &str {
        &self.source
    }

    pub fn changes(&self) -> bool {
        self.changed
    }
}

#[derive(Debug, Clone, Serialize)]
#[serde(tag = "error", rename_all = "snake_case")]
pub enum ApplyError {
    SchemaMismatch {
        expected: &'static str,
        found: String,
    },
    RevisionMismatch {
        base_revision: u64,
        current_revision: u64,
    },
    UnknownSite {
        site: usize,
    },
    UnknownBlock {
        site: usize,
        block: String,
    },
    NotEditable {
        element: String,
        reason: Option<Reason>,
    },
    IndexOutOfRange {
        element: String,
        index: usize,
        len: usize,
    },
    NoDisplayNameArgument {
        block: String,
    },
    AliasedTemplateArguments {
        block: String,
        alias: String,
    },
    NoConfig {
        site: usize,
    },
    NotInGraph {
        block: String,
    },
    DuplicateVariable {
        var_name: String,
    },
    InvalidIdentifier {
        text: String,
    },
    InvalidExpression {
        element: String,
        text: String,
    },
    ReservedIdentifier {
        text: String,
    },
    FileHasErrors {
        errors: Vec<Span>,
    },
    EmptyConstructorArguments {
        var_name: String,
    },
    ReferencesOutsideGraph {
        block: String,
        spans: Vec<Span>,
    },
    UnsupportedShape {
        detail: String,
    },
    StaleSpan {
        span: Span,
    },
    OverlappingSplices {
        first: Span,
        second: Span,
    },
    ProducesParseError,
    Reparse {
        detail: String,
    },
    NothingToUndo,
    NothingToRedo,
}

impl fmt::Display for ApplyError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match serde_json::to_string(self) {
            Ok(text) => write!(f, "{text}"),
            Err(cause) => write!(f, "unserializable apply error: {cause}"),
        }
    }
}

impl std::error::Error for ApplyError {}

const CPP_KEYWORDS: &[&str] = &[
    "alignas",
    "alignof",
    "and",
    "and_eq",
    "asm",
    "auto",
    "bitand",
    "bitor",
    "bool",
    "break",
    "case",
    "catch",
    "char",
    "char8_t",
    "char16_t",
    "char32_t",
    "class",
    "compl",
    "concept",
    "const",
    "consteval",
    "constexpr",
    "constinit",
    "const_cast",
    "continue",
    "co_await",
    "co_return",
    "co_yield",
    "decltype",
    "default",
    "delete",
    "do",
    "double",
    "dynamic_cast",
    "else",
    "enum",
    "explicit",
    "export",
    "extern",
    "false",
    "float",
    "for",
    "friend",
    "goto",
    "if",
    "inline",
    "int",
    "long",
    "mutable",
    "namespace",
    "new",
    "noexcept",
    "not",
    "not_eq",
    "nullptr",
    "operator",
    "or",
    "or_eq",
    "private",
    "protected",
    "public",
    "register",
    "reinterpret_cast",
    "requires",
    "return",
    "short",
    "signed",
    "sizeof",
    "static",
    "static_assert",
    "static_cast",
    "struct",
    "switch",
    "template",
    "this",
    "thread_local",
    "throw",
    "true",
    "try",
    "typedef",
    "typeid",
    "typename",
    "union",
    "unsigned",
    "using",
    "virtual",
    "void",
    "volatile",
    "wchar_t",
    "while",
    "xor",
    "xor_eq",
];

const EXPRESSION_PROBE: (&str, &str) = ("void cler_probe(){cler_probe_call(", ");}\n");
const TEMPLATE_PROBE: (&str, &str) = ("using cler_probe = cler_probe_template<", ">;\n");

fn is_identifier(text: &str) -> bool {
    let mut chars = text.chars();
    matches!(chars.next(), Some(first) if first.is_ascii_alphabetic() || first == '_')
        && chars.all(|c| c.is_ascii_alphanumeric() || c == '_')
}

fn is_keyword(text: &str) -> bool {
    CPP_KEYWORDS.contains(&text)
}

fn is_qualified_type(text: &str) -> bool {
    !text.is_empty()
        && text
            .split("::")
            .all(|segment| is_identifier(segment) || segment.is_empty())
        && text.split("::").any(is_identifier)
        && !text.ends_with("::")
}

fn is_member_path(text: &str) -> bool {
    !text.is_empty() && text.split('.').all(is_identifier)
}

fn holds_one_node(probe: (&str, &str), text: &str, list_kind: &str) -> bool {
    if text.is_empty() || text.trim() != text {
        return false;
    }
    let (prefix, suffix) = probe;
    let source = format!("{prefix}{text}{suffix}");
    let Ok(tree) = parse_source(&source) else {
        return false;
    };
    let root = tree.root_node();
    if root.has_error() {
        return false;
    }
    let start = prefix.len();
    let end = start + text.len();
    descendants(root).into_iter().any(|node| {
        if node.kind() != list_kind {
            return false;
        }
        let held = named_children(node);
        held.len() == 1 && held[0].start_byte() == start && held[0].end_byte() == end
    })
}

fn is_expression(text: &str) -> bool {
    holds_one_node(EXPRESSION_PROBE, text, "argument_list")
}

fn is_template_argument(text: &str) -> bool {
    holds_one_node(TEMPLATE_PROBE, text, "template_argument_list")
}

fn expression(element: &str, text: &str) -> Result<(), ApplyError> {
    if is_expression(text) {
        return Ok(());
    }
    Err(ApplyError::InvalidExpression {
        element: element.to_string(),
        text: text.to_string(),
    })
}

fn quoted(text: &str) -> Result<String, ApplyError> {
    if text.chars().any(|c| c.is_control()) {
        return Err(ApplyError::InvalidExpression {
            element: "display name".to_string(),
            text: text.to_string(),
        });
    }
    let escaped = text.replace('\\', "\\\\").replace('"', "\\\"");
    Ok(format!("\"{escaped}\""))
}

fn word_spans(haystack: &str, needle: &str, offset: usize) -> Vec<Span> {
    let boundary = |c: Option<char>| !matches!(c, Some(c) if c.is_ascii_alphanumeric() || c == '_');
    let mut out = Vec::new();
    let mut from = 0;
    while let Some(found) = haystack.get(from..).and_then(|tail| tail.find(needle)) {
        let start = from + found;
        let end = start + needle.len();
        let before = haystack
            .get(..start)
            .and_then(|head| head.chars().next_back());
        let after = haystack.get(end..).and_then(|tail| tail.chars().next());
        if boundary(before) && boundary(after) {
            out.push(Span {
                start: offset + start,
                end: offset + end,
            });
        }
        from = end;
    }
    out
}

fn channel_argument(block: &str, port: &str, index: Option<usize>) -> String {
    match index {
        Some(index) => format!("&{block}.{port}[{index}]"),
        None => format!("&{block}.{port}"),
    }
}

fn contains(outer: Span, inner: Span) -> bool {
    inner.start >= outer.start && inner.end <= outer.end
}

struct Planner<'a> {
    model: &'a FileModel,
    tree: &'a Tree,
    src: &'a str,
}

impl<'a> Planner<'a> {
    fn text(&self, node: Node) -> &'a str {
        self.src.get(node.byte_range()).unwrap_or("")
    }

    fn site(&self, index: usize) -> Result<&'a Site, ApplyError> {
        self.model
            .sites
            .get(index)
            .ok_or(ApplyError::UnknownSite { site: index })
    }

    fn usable_site(&self, index: usize) -> Result<&'a Site, ApplyError> {
        let site = self.site(index)?;
        match site.read_only_reason {
            Some(reason) if reason != Reason::SiteHasReadOnlyElements => {
                Err(ApplyError::NotEditable {
                    element: format!("site {index}"),
                    reason: Some(reason),
                })
            }
            _ => Ok(site),
        }
    }

    fn block(&self, site: usize, var: &str) -> Result<&'a Block, ApplyError> {
        self.site(site)?
            .block(var)
            .ok_or_else(|| ApplyError::UnknownBlock {
                site,
                block: var.to_string(),
            })
    }

    fn editable_block(&self, site: usize, var: &str) -> Result<&'a Block, ApplyError> {
        let block = self.block(site, var)?;
        if !block.editable {
            return Err(ApplyError::NotEditable {
                element: format!("block {var}"),
                reason: block.read_only_reason,
            });
        }
        Ok(block)
    }

    fn editable_runner<'s>(&self, site: &'s Site, var: &str) -> Result<&'s Runner, ApplyError> {
        let runner = site
            .runners
            .iter()
            .find(|r| r.block.as_deref() == Some(var))
            .ok_or_else(|| ApplyError::NotInGraph {
                block: var.to_string(),
            })?;
        if !runner.editable {
            return Err(ApplyError::NotEditable {
                element: format!("runner for {var}"),
                reason: runner.read_only_reason,
            });
        }
        Ok(runner)
    }

    fn node(&self, span: Span) -> Result<Node<'a>, ApplyError> {
        let mut node = self
            .tree
            .root_node()
            .descendant_for_byte_range(span.start, span.end)
            .ok_or(ApplyError::StaleSpan { span })?;
        while node.start_byte() != span.start || node.end_byte() != span.end {
            node = node.parent().ok_or(ApplyError::StaleSpan { span })?;
        }
        Ok(node)
    }

    fn argument_list(&self, call: Node<'a>) -> Result<Node<'a>, ApplyError> {
        call.child_by_field_name("arguments")
            .ok_or_else(|| ApplyError::UnsupportedShape {
                detail: format!("{} has no argument list", call.kind()),
            })
    }

    fn statement_of(&self, node: Node<'a>) -> Node<'a> {
        let mut current = node;
        while let Some(parent) = current.parent() {
            if matches!(
                parent.kind(),
                "compound_statement" | "translation_unit" | "declaration_list"
            ) {
                return current;
            }
            current = parent;
        }
        current
    }

    fn function_body(&self, site: &Site) -> Node<'a> {
        let root = self.tree.root_node();
        let Ok(mut node) = self.node(site.span) else {
            return root;
        };
        loop {
            if node.kind() == "function_definition" {
                return node.child_by_field_name("body").unwrap_or(node);
            }
            match node.parent() {
                Some(parent) => node = parent,
                None => return root,
            }
        }
    }

    fn scope_nodes(&self, body: Node<'a>) -> Vec<Node<'a>> {
        let root = self.tree.root_node();
        let mut out = descendants(body);
        if body.id() == root.id() {
            return out;
        }
        let mut stack = vec![root];
        while let Some(current) = stack.pop() {
            out.push(current);
            let mut cursor = current.walk();
            let children: Vec<Node<'a>> = current
                .children(&mut cursor)
                .filter(|child| child.kind() != "function_definition")
                .collect();
            for child in children.into_iter().rev() {
                stack.push(child);
            }
        }
        out
    }

    fn scope_identifiers(&self, body: Node<'a>) -> HashSet<&'a str> {
        self.scope_nodes(body)
            .into_iter()
            .filter(|node| matches!(node.kind(), "identifier" | "type_identifier"))
            .map(|node| self.text(node))
            .collect()
    }

    fn line_ending(&self) -> &'static str {
        let crlf = self.src.matches("\r\n").count();
        if crlf * 2 > self.src.matches('\n').count() {
            "\r\n"
        } else {
            "\n"
        }
    }

    fn line_start(&self, offset: usize) -> usize {
        self.src
            .get(..offset)
            .and_then(|head| head.rfind('\n'))
            .map(|index| index + 1)
            .unwrap_or(0)
    }

    fn indent(&self, offset: usize) -> &'a str {
        let start = self.line_start(offset);
        let line = self.src.get(start..offset).unwrap_or("");
        let end = line.find(|c| c != ' ' && c != '\t').unwrap_or(line.len());
        line.get(..end).unwrap_or("")
    }

    fn remove_argument(&self, arg: Node<'a>) -> Result<Splice, ApplyError> {
        let list = arg
            .parent()
            .filter(|parent| parent.kind() == "argument_list")
            .ok_or_else(|| ApplyError::UnsupportedShape {
                detail: "argument is not held by an argument list".to_string(),
            })?;
        let siblings = named_children(list);
        let position = siblings
            .iter()
            .position(|node| node.id() == arg.id())
            .ok_or_else(|| ApplyError::UnsupportedShape {
                detail: "argument not found among its siblings".to_string(),
            })?;
        if position > 0 {
            return Ok(Splice::remove(
                siblings[position - 1].end_byte(),
                arg.end_byte(),
            ));
        }
        match siblings.get(1) {
            Some(next) => Ok(Splice::remove(arg.start_byte(), next.start_byte())),
            None => Ok(Splice::remove(arg.start_byte(), arg.end_byte())),
        }
    }

    fn remove_statement(&self, statement: Span) -> Splice {
        let head_start = self.line_start(statement.start);
        let head = self.src.get(head_start..statement.start).unwrap_or("");
        let start = if head.trim().is_empty() {
            head_start
        } else {
            statement.start
        };

        let bytes = self.src.as_bytes();
        let is_blank = |at: usize| matches!(bytes.get(at), Some(b' ' | b'\t' | b'\r'));
        let mut end = statement.end;
        let mut cursor = end;
        while is_blank(cursor) {
            cursor += 1;
        }
        if self.src.get(cursor..).unwrap_or("").starts_with("//") {
            while cursor < bytes.len() && bytes[cursor] != b'\n' {
                cursor += 1;
            }
            end = cursor;
        }
        let mut tail = end;
        while is_blank(tail) {
            tail += 1;
        }
        if bytes.get(tail) == Some(&b'\n') {
            end = tail + 1;
        }
        Splice::remove(start, end)
    }

    fn runner_factory(&self, site: &Site) -> String {
        site.runners
            .iter()
            .find(|runner| runner.editable)
            .and_then(|runner| self.node(runner.span).ok())
            .and_then(|node| node.child_by_field_name("function"))
            .map(|function| self.text(function).to_string())
            .unwrap_or_else(|| "cler::BlockRunner".to_string())
    }

    fn remaining_references(&self, site: &Site, block: &Block, removed: &[Splice]) -> Vec<Span> {
        let mut found: Vec<Span> = Vec::new();
        for node in self.scope_nodes(self.function_body(site)) {
            match node.kind() {
                "identifier" if self.text(node) == block.var => found.push(Span {
                    start: node.start_byte(),
                    end: node.end_byte(),
                }),
                "preproc_arg" => {
                    found.extend(word_spans(self.text(node), &block.var, node.start_byte()))
                }
                _ => {}
            }
        }
        found.sort_by_key(|span| (span.start, span.end));
        found.dedup();
        found.retain(|span| !removed.iter().any(|splice| contains(splice.span(), *span)));
        found
    }

    fn plan(&self, command: &Command) -> Result<Vec<Splice>, ApplyError> {
        if self.model.has_errors {
            return Err(ApplyError::FileHasErrors {
                errors: self.model.errors.clone(),
            });
        }
        let site_index = match command {
            Command::SetParam { site, .. }
            | Command::SetTemplateArg { site, .. }
            | Command::SetDisplayName { site, .. }
            | Command::SetConfig { site, .. }
            | Command::Connect { site, .. }
            | Command::Disconnect { site, .. }
            | Command::AddBlock { site, .. }
            | Command::RemoveFromGraph { site, .. }
            | Command::DeleteBlock { site, .. } => *site,
        };
        self.usable_site(site_index)?;

        match command {
            Command::SetParam {
                site,
                block,
                ctor_arg_index,
                new_text,
            } => {
                expression("new_text", new_text)?;
                let target = self.editable_block(*site, block)?;
                let arg = target.ctor_args.get(*ctor_arg_index).ok_or_else(|| {
                    ApplyError::IndexOutOfRange {
                        element: format!("{block} constructor arguments"),
                        index: *ctor_arg_index,
                        len: target.ctor_args.len(),
                    }
                })?;
                Ok(vec![Splice::replace(arg.span, new_text)])
            }

            Command::SetTemplateArg {
                site,
                block,
                template_arg_index,
                new_text,
            } => {
                if !is_template_argument(new_text) {
                    return Err(ApplyError::InvalidExpression {
                        element: "new_text".to_string(),
                        text: new_text.clone(),
                    });
                }
                let target = self.editable_block(*site, block)?;
                if let Some(alias) = &target.alias {
                    return Err(ApplyError::AliasedTemplateArguments {
                        block: block.clone(),
                        alias: alias.clone(),
                    });
                }
                let arg = target
                    .template_args
                    .get(*template_arg_index)
                    .ok_or_else(|| ApplyError::IndexOutOfRange {
                        element: format!("{block} template arguments"),
                        index: *template_arg_index,
                        len: target.template_args.len(),
                    })?;
                Ok(vec![Splice::replace(arg.span, new_text)])
            }

            Command::SetDisplayName {
                site,
                block,
                new_text,
            } => {
                let target = self.editable_block(*site, block)?;
                let arg = target
                    .ctor_args
                    .first()
                    .filter(|arg| arg.text.starts_with('"'))
                    .ok_or_else(|| ApplyError::NoDisplayNameArgument {
                        block: block.clone(),
                    })?;
                Ok(vec![Splice::replace(arg.span, quoted(new_text)?)])
            }

            Command::SetConfig {
                site,
                path,
                new_value,
            } => {
                if !is_member_path(path) {
                    return Err(ApplyError::InvalidIdentifier { text: path.clone() });
                }
                expression("new_value", new_value)?;
                let target = self.site(*site)?;
                let config = target
                    .config
                    .as_ref()
                    .ok_or(ApplyError::NoConfig { site: *site })?;
                if let Some(existing) = config
                    .assignments
                    .iter()
                    .find(|assignment| assignment.path == *path)
                {
                    if !existing.editable {
                        return Err(ApplyError::NotEditable {
                            element: format!("config assignment {path}"),
                            reason: existing.read_only_reason,
                        });
                    }
                    return Ok(vec![Splice::replace(existing.value_span, new_value)]);
                }
                if !config.editable {
                    return Err(ApplyError::NotEditable {
                        element: "config".to_string(),
                        reason: config.read_only_reason,
                    });
                }
                let var = config
                    .var
                    .as_deref()
                    .ok_or_else(|| ApplyError::UnsupportedShape {
                        detail: "config has no variable to assign through".to_string(),
                    })?;
                let assignment = format!("{var}.{path} = {new_value};");
                let eol = self.line_ending();
                match config
                    .assignments
                    .iter()
                    .max_by_key(|assignment| assignment.span.end)
                {
                    Some(last) => {
                        let statement = self.statement_of(self.node(last.span)?);
                        let indent = self.indent(statement.start_byte());
                        Ok(vec![Splice::insert(
                            statement.end_byte(),
                            format!("{eol}{indent}{assignment}"),
                        )])
                    }
                    None => {
                        let statement = self.statement_of(self.node(config.run_call_span)?);
                        let indent = self.indent(statement.start_byte());
                        Ok(vec![Splice::insert(
                            self.line_start(statement.start_byte()),
                            format!("{indent}{assignment}{eol}"),
                        )])
                    }
                }
            }

            Command::Connect {
                site,
                from,
                to,
                port,
                port_index,
            } => {
                if !is_identifier(port) {
                    return Err(ApplyError::InvalidIdentifier { text: port.clone() });
                }
                let target = self.site(*site)?;
                let source = self.editable_block(*site, from)?;
                let sink = self.editable_block(*site, to)?;
                let argument = channel_argument(&sink.var, port, *port_index);
                match target
                    .runners
                    .iter()
                    .find(|runner| runner.block.as_deref() == Some(source.var.as_str()))
                {
                    Some(_) => {
                        let runner = self.editable_runner(target, &source.var)?;
                        let list = self.argument_list(self.node(runner.span)?)?;
                        let last = named_children(list).last().copied().ok_or_else(|| {
                            ApplyError::UnsupportedShape {
                                detail: format!("runner for {from} has no arguments"),
                            }
                        })?;
                        Ok(vec![Splice::insert(
                            last.end_byte(),
                            format!(", {argument}"),
                        )])
                    }
                    None => {
                        let list = self.argument_list(self.node(target.span)?)?;
                        let last = named_children(list).last().copied().ok_or_else(|| {
                            ApplyError::UnsupportedShape {
                                detail: "flowgraph call has no runners to append after".to_string(),
                            }
                        })?;
                        let factory = self.runner_factory(target);
                        let indent = self.indent(last.start_byte());
                        let eol = self.line_ending();
                        Ok(vec![Splice::insert(
                            last.end_byte(),
                            format!(",{eol}{indent}{factory}(&{}, {argument})", source.var),
                        )])
                    }
                }
            }

            Command::Disconnect { site, edge } => {
                let target = self.site(*site)?;
                let wire = target
                    .edges
                    .get(*edge)
                    .ok_or_else(|| ApplyError::IndexOutOfRange {
                        element: "edges".to_string(),
                        index: *edge,
                        len: target.edges.len(),
                    })?;
                if !wire.editable {
                    return Err(ApplyError::NotEditable {
                        element: format!("edge {} -> {}", wire.from, wire.to),
                        reason: wire.read_only_reason,
                    });
                }
                Ok(vec![self.remove_argument(self.node(wire.span)?)?])
            }

            Command::AddBlock {
                site,
                type_name,
                template_args,
                ctor_args,
                var_name,
            } => {
                let target = self.site(*site)?;
                if !is_identifier(var_name) {
                    return Err(ApplyError::InvalidIdentifier {
                        text: var_name.clone(),
                    });
                }
                if is_keyword(var_name) {
                    return Err(ApplyError::ReservedIdentifier {
                        text: var_name.clone(),
                    });
                }
                if !is_qualified_type(type_name) {
                    return Err(ApplyError::InvalidIdentifier {
                        text: type_name.clone(),
                    });
                }
                for argument in template_args {
                    if !is_template_argument(argument) {
                        return Err(ApplyError::InvalidExpression {
                            element: "template_args".to_string(),
                            text: argument.clone(),
                        });
                    }
                }
                for argument in ctor_args {
                    expression("ctor_args", argument)?;
                }
                if ctor_args.is_empty() {
                    return Err(ApplyError::EmptyConstructorArguments {
                        var_name: var_name.clone(),
                    });
                }
                if self
                    .scope_identifiers(self.function_body(target))
                    .contains(var_name.as_str())
                {
                    return Err(ApplyError::DuplicateVariable {
                        var_name: var_name.clone(),
                    });
                }
                let statement = self.statement_of(self.node(target.span)?);
                let indent = self.indent(statement.start_byte());
                let generics = if template_args.is_empty() {
                    String::new()
                } else {
                    format!("<{}>", template_args.join(", "))
                };
                let declaration = format!(
                    "{indent}{type_name}{generics} {var_name}({});{}",
                    ctor_args.join(", "),
                    self.line_ending()
                );
                Ok(vec![Splice::insert(
                    self.line_start(statement.start_byte()),
                    declaration,
                )])
            }

            Command::RemoveFromGraph { site, block } => {
                let target = self.site(*site)?;
                let runner = self.editable_runner(target, block)?;
                Ok(vec![self.remove_argument(self.node(runner.span)?)?])
            }

            Command::DeleteBlock { site, block } => {
                let target = self.site(*site)?;
                let declared = self.editable_block(*site, block)?;
                let own = target
                    .runners
                    .iter()
                    .position(|runner| runner.block.as_deref() == Some(block.as_str()));
                let mut splices = Vec::new();
                if own.is_some() {
                    let runner = self.editable_runner(target, block)?;
                    splices.push(self.remove_argument(self.node(runner.span)?)?);
                }
                for edge in target.edges.iter().filter(|edge| edge.to == *block) {
                    if Some(edge.runner_index) == own {
                        continue;
                    }
                    if !edge.editable {
                        return Err(ApplyError::NotEditable {
                            element: format!("edge {} -> {}", edge.from, edge.to),
                            reason: edge.read_only_reason,
                        });
                    }
                    splices.push(self.remove_argument(self.node(edge.span)?)?);
                }
                splices.push(self.remove_statement(declared.span));
                let remaining = self.remaining_references(target, declared, &splices);
                if !remaining.is_empty() {
                    return Err(ApplyError::ReferencesOutsideGraph {
                        block: block.clone(),
                        spans: remaining,
                    });
                }
                Ok(splices)
            }
        }
    }
}

fn merge(
    source: &str,
    mut ordered: Vec<(usize, Splice)>,
) -> Result<(String, Vec<Splice>), ApplyError> {
    ordered.sort_by_key(|(index, splice)| (splice.start, splice.end, *index));
    let splices: Vec<Splice> = ordered.into_iter().map(|(_, splice)| splice).collect();
    for pair in splices.windows(2) {
        let (first, second) = (&pair[0], &pair[1]);
        let both_insert = first.start == first.end && second.start == second.end;
        if second.start < first.end || (second.start == first.start && !both_insert) {
            return Err(ApplyError::OverlappingSplices {
                first: first.span(),
                second: second.span(),
            });
        }
    }
    let mut out = String::with_capacity(source.len());
    let mut cursor = 0;
    for splice in &splices {
        let head = source
            .get(cursor..splice.start)
            .ok_or(ApplyError::StaleSpan {
                span: splice.span(),
            })?;
        out.push_str(head);
        out.push_str(&splice.text);
        cursor = splice.end;
    }
    let tail = source.get(cursor..).ok_or(ApplyError::StaleSpan {
        span: Span {
            start: cursor,
            end: source.len(),
        },
    })?;
    out.push_str(tail);
    Ok((out, splices))
}

impl DocumentSession {
    pub fn apply(&mut self, transaction: Transaction) -> Result<ApplyOutcome, ApplyError> {
        let pending = self.preview(transaction)?;
        Ok(self.commit(pending))
    }

    pub fn preview(&self, transaction: Transaction) -> Result<PendingApply, ApplyError> {
        if transaction.version != SCHEMA_VERSION {
            return Err(ApplyError::SchemaMismatch {
                expected: SCHEMA_VERSION,
                found: transaction.version,
            });
        }
        if transaction.base_revision != self.revision {
            return Err(ApplyError::RevisionMismatch {
                base_revision: transaction.base_revision,
                current_revision: self.revision,
            });
        }

        let model = self.parse();
        let planner = Planner {
            model: &model,
            tree: &self.tree,
            src: &self.source,
        };
        let mut splices = Vec::new();
        for (index, command) in transaction.commands.iter().enumerate() {
            splices.extend(planner.plan(command)?.into_iter().map(|s| (index, s)));
        }
        let (next, splices) = merge(&self.source, splices)?;
        if next == self.source {
            return Ok(PendingApply {
                source: next,
                tree: self.tree.clone(),
                splices,
                changed: false,
                kind: PendingKind::Edit,
            });
        }

        let tree = parse_source(&next).map_err(|cause| ApplyError::Reparse {
            detail: cause.to_string(),
        })?;
        if tree.root_node().has_error() {
            return Err(ApplyError::ProducesParseError);
        }

        Ok(PendingApply {
            source: next,
            tree,
            splices,
            changed: true,
            kind: PendingKind::Edit,
        })
    }

    pub fn preview_undo(&self) -> Result<PendingApply, ApplyError> {
        self.pending_snapshot(
            self.undo.last(),
            PendingKind::Undo,
            ApplyError::NothingToUndo,
        )
    }

    pub fn preview_redo(&self) -> Result<PendingApply, ApplyError> {
        self.pending_snapshot(
            self.redo.last(),
            PendingKind::Redo,
            ApplyError::NothingToRedo,
        )
    }

    pub fn commit(&mut self, pending: PendingApply) -> ApplyOutcome {
        if !pending.changed {
            return ApplyOutcome {
                revision: self.revision,
                splices: pending.splices,
            };
        }
        let previous = std::mem::replace(&mut self.source, pending.source);
        self.tree = pending.tree;
        self.revision += 1;
        match pending.kind {
            PendingKind::Edit => {
                self.undo.push(previous);
                self.redo.clear();
            }
            PendingKind::Undo => {
                self.undo.pop();
                self.redo.push(previous);
            }
            PendingKind::Redo => {
                self.redo.pop();
                self.undo.push(previous);
            }
        }
        ApplyOutcome {
            revision: self.revision,
            splices: pending.splices,
        }
    }

    pub fn undo(&mut self) -> Result<u64, ApplyError> {
        let pending = self.preview_undo()?;
        Ok(self.commit(pending).revision)
    }

    pub fn redo(&mut self) -> Result<u64, ApplyError> {
        let pending = self.preview_redo()?;
        Ok(self.commit(pending).revision)
    }

    fn pending_snapshot(
        &self,
        snapshot: Option<&String>,
        kind: PendingKind,
        missing: ApplyError,
    ) -> Result<PendingApply, ApplyError> {
        let source = snapshot.ok_or(missing)?.clone();
        let tree = parse_source(&source).map_err(|cause| ApplyError::Reparse {
            detail: cause.to_string(),
        })?;
        Ok(PendingApply {
            source,
            tree,
            splices: Vec::new(),
            changed: true,
            kind,
        })
    }

    pub fn reload(&mut self, text: impl Into<String>) -> Result<(u64, bool), ApplyError> {
        self.swap_source(text.into())?;
        self.undo.clear();
        self.redo.clear();
        Ok((self.revision, self.has_errors()))
    }

    fn swap_source(&mut self, text: String) -> Result<String, ApplyError> {
        let tree = parse_source(&text).map_err(|cause| ApplyError::Reparse {
            detail: cause.to_string(),
        })?;
        self.tree = tree;
        self.revision += 1;
        Ok(std::mem::replace(&mut self.source, text))
    }
}

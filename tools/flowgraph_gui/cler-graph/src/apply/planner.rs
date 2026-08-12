use std::collections::HashSet;

use tree_sitter::{Node, Tree};

use crate::model::{Block, ConfigSource, FileModel, Reason, Runner, Site, Span};
use crate::parse::{descendants, named_children};
use crate::parse_source;

use super::splice::Splice;
use super::{ApplyError, BlockParam, Command, InputPort};

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
const TYPE_PROBE: (&str, &str) = ("using cler_probe = ", ";\n");

const BLOCK_SUFFIX: &str = "Block";
const GUI_HEADER: &str = "desktop_blocks/gui/gui_manager.hpp";
const SKELETON_TODO: &str = "// TODO: implement";
const CHANNEL_CAPACITY: &str = "cler::DOUBLY_MAPPED_MIN_SIZE / sizeof";

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

fn is_type(text: &str) -> bool {
    if text.is_empty() || text.trim() != text {
        return false;
    }
    let (prefix, suffix) = TYPE_PROBE;
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
        node.kind() == "alias_declaration"
            && node
                .child_by_field_name("type")
                .is_some_and(|held| held.start_byte() == start && held.end_byte() == end)
    })
}

fn type_text(element: &str, text: &str) -> Result<(), ApplyError> {
    if is_type(text) {
        return Ok(());
    }
    Err(ApplyError::InvalidType {
        element: element.to_string(),
        text: text.to_string(),
    })
}

fn identifier_name(text: &str) -> Result<(), ApplyError> {
    if !is_identifier(text) {
        return Err(ApplyError::InvalidIdentifier {
            text: text.to_string(),
        });
    }
    if is_keyword(text) {
        return Err(ApplyError::ReservedIdentifier {
            text: text.to_string(),
        });
    }
    Ok(())
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

struct Skeleton<'a> {
    name: &'a str,
    value_type: &'a str,
    inputs: &'a [InputPort],
    outputs: usize,
    params: &'a [BlockParam],
    may_block: bool,
    eol: &'a str,
}

impl Skeleton<'_> {
    fn shortfall(&self) -> &'static str {
        match (self.inputs.is_empty(), self.outputs == 0) {
            (true, _) => "NotEnoughSpace",
            (_, true) => "NotEnoughSamples",
            _ => "NotEnoughSpaceOrSamples",
        }
    }

    fn render(&self) -> String {
        let outs: Vec<String> = (0..self.outputs)
            .map(|index| format!("out{index}"))
            .collect();
        let mut lines: Vec<String> = Vec::new();

        lines.push(format!("struct {} : public cler::BlockBase {{", self.name));
        if self.may_block {
            lines.push("    static constexpr bool may_block = true;".to_string());
        }
        for input in self.inputs {
            lines.push(format!(
                "    cler::Channel<{}> {};",
                self.value_type, input.name
            ));
        }
        if lines.len() > 1 {
            lines.push(String::new());
        }

        let mut declared = vec!["const char* name".to_string()];
        for param in self.params {
            declared.push(match &param.default {
                Some(value) => format!("{} {} = {}", param.cpp_type, param.name, value),
                None => format!("{} {}", param.cpp_type, param.name),
            });
        }
        lines.push(format!("    {}({})", self.name, declared.join(", ")));

        let mut inits = vec!["cler::BlockBase(name)".to_string()];
        for input in self.inputs {
            inits.push(format!(
                "{}({CHANNEL_CAPACITY}({}))",
                input.name, self.value_type
            ));
        }
        for param in self.params {
            inits.push(format!("_{}({})", param.name, param.name));
        }
        lines.push(format!("        : {} {{}}", inits.join(", ")));
        lines.push(String::new());

        let taken: Vec<String> = outs
            .iter()
            .map(|out| format!("cler::ChannelBase<{}>* {out}", self.value_type))
            .collect();
        lines.push(format!(
            "    cler::Result<cler::Empty, cler::Error> procedure({}) {{",
            taken.join(", ")
        ));
        lines.push(format!("        {SKELETON_TODO}"));

        let mut sizes: Vec<String> = Vec::new();
        for input in self.inputs {
            lines.push(format!(
                "        auto [{0}_ptr, {0}_size] = {0}.read_dbf();",
                input.name
            ));
            sizes.push(format!("{}_size", input.name));
        }
        for out in &outs {
            lines.push(format!(
                "        auto [{out}_ptr, {out}_size] = {out}->write_dbf();"
            ));
            sizes.push(format!("{out}_size"));
        }
        let transferable = match sizes.as_slice() {
            [only] => only.clone(),
            [first, second] => format!("std::min({first}, {second})"),
            many => format!("std::min({{{}}})", many.join(", ")),
        };
        lines.push(format!("        size_t transferable = {transferable};"));
        lines.push("        if (transferable == 0) {".to_string());
        lines.push(format!(
            "            return cler::Error::{};",
            self.shortfall()
        ));
        lines.push("        }".to_string());

        if !outs.is_empty() {
            let sample = match self.inputs.first() {
                Some(first) => format!("{}_ptr[i]", first.name),
                None => format!("{}{{}}", self.value_type),
            };
            lines.push(String::new());
            lines.push("        for (size_t i = 0; i < transferable; ++i) {".to_string());
            for out in &outs {
                lines.push(format!("            {out}_ptr[i] = {sample};"));
            }
            lines.push("        }".to_string());
        }

        lines.push(String::new());
        for input in self.inputs {
            lines.push(format!("        {}.commit_read(transferable);", input.name));
        }
        for out in &outs {
            lines.push(format!("        {out}->commit_write(transferable);"));
        }
        lines.push("        return cler::Empty{};".to_string());
        lines.push("    }".to_string());

        if !self.params.is_empty() {
            lines.push(String::new());
            lines.push("private:".to_string());
            for param in self.params {
                lines.push(format!("    {} _{};", param.cpp_type, param.name));
            }
        }
        lines.push("};".to_string());
        lines.join(self.eol)
    }
}

pub(super) struct Planner<'a> {
    pub(super) model: &'a FileModel,
    pub(super) tree: &'a Tree,
    pub(super) src: &'a str,
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

    fn enclosing_function(&self, site: &Site) -> Result<Node<'a>, ApplyError> {
        let mut node = self.node(site.span)?;
        loop {
            if node.kind() == "function_definition" {
                return Ok(node);
            }
            node = node.parent().ok_or_else(|| ApplyError::UnsupportedShape {
                detail: "site is not held by a function definition".to_string(),
            })?;
        }
    }

    fn unit_identifiers(&self) -> HashSet<&'a str> {
        descendants(self.tree.root_node())
            .into_iter()
            .filter(|node| matches!(node.kind(), "identifier" | "type_identifier"))
            .map(|node| self.text(node))
            .collect()
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

    fn materialize_gui(&self, site: &Site, block: &str) -> Result<Vec<Splice>, ApplyError> {
        let eol = self.line_ending();
        let run = self.statement_of(self.node(
            site.config
                .as_ref()
                .map(|config| config.run_call_span)
                .ok_or_else(|| ApplyError::UnsupportedShape {
                    detail: "this site has no run call to render after".to_string(),
                })?,
        )?);
        let indent = self.indent(run.start_byte());
        let inner = format!("{indent}    ");
        let mut splices = Vec::new();

        if !self.src.contains(GUI_HEADER) {
            let at = self.last_include_end();
            splices.push(Splice::insert(at, format!("#include \"{GUI_HEADER}\"{eol}")));
        }

        let declaration = self.site_first_statement(site)?;
        splices.push(Splice::insert(
            self.line_start(declaration.start_byte()),
            format!(
                "{indent}cler::GuiManager gui(800, 400, \"{}\");{eol}",
                self.window_title()
            ),
        ));

        let idle = self.idle_loop_after(run);
        let loop_text = format!(
            "{indent}while (!gui.should_close()) {{{eol}{inner}gui.begin_frame();{eol}{inner}{block}.render();{eol}{inner}gui.end_frame();{eol}{indent}}}{eol}"
        );
        match idle {
            Some(node) => splices.push(Splice::replace(
                Span {
                    start: self.line_start(node.start_byte()),
                    end: self.line_end(node.end_byte()),
                },
                &loop_text,
            )),
            None => splices.push(Splice::insert(self.line_end(run.end_byte()), loop_text)),
        }
        splices.sort_by_key(|splice| splice.start);
        Ok(splices)
    }

    fn line_end(&self, offset: usize) -> usize {
        match self.src.get(offset..).and_then(|tail| tail.find('\n')) {
            Some(found) => offset + found + 1,
            None => self.src.len(),
        }
    }

    fn window_title(&self) -> String {
        self.model
            .file
            .as_deref()
            .and_then(|path| path.rsplit('/').next())
            .and_then(|name| name.split('.').next())
            .filter(|stem| !stem.is_empty())
            .unwrap_or("cler flowgraph")
            .to_string()
    }

    fn last_include_end(&self) -> usize {
        let mut at = 0;
        let mut offset = 0;
        for line in self.src.split_inclusive('\n') {
            offset += line.len();
            if line.trim_start().starts_with("#include") {
                at = offset;
            }
        }
        at
    }

    fn site_first_statement(&self, site: &Site) -> Result<Node<'a>, ApplyError> {
        let body = self.function_body(site);
        named_children(body)
            .into_iter()
            .next()
            .ok_or_else(|| ApplyError::UnsupportedShape {
                detail: "the site function has no statements".to_string(),
            })
    }

    fn idle_loop_after(&self, run: Node<'a>) -> Option<Node<'a>> {
        let mut found = run.next_named_sibling();
        while let Some(node) = found {
            if node.kind() == "while_statement" {
                let condition = self.text(node);
                if condition.contains("is_stopped") {
                    return Some(node);
                }
                return None;
            }
            found = node.next_named_sibling();
        }
        None
    }

    fn append_runners(&self, site: &Site, tails: &[String]) -> Result<Splice, ApplyError> {
        let call = self.node(site.span)?;
        let list = self.argument_list(call)?;
        let factory = self.runner_factory(site);
        let eol = self.line_ending();
        match named_children(list).last().copied() {
            Some(last) => {
                let indent = self.indent(last.start_byte());
                let written = tails
                    .iter()
                    .map(|tail| format!(",{eol}{indent}{factory}{tail}"))
                    .collect::<String>();
                Ok(Splice::insert(last.end_byte(), written))
            }
            None => {
                let statement = self.statement_of(call);
                let outer = self.indent(statement.start_byte());
                let inner = format!("{outer}    ");
                let written = tails
                    .iter()
                    .map(|tail| format!("{factory}{tail}"))
                    .collect::<Vec<_>>()
                    .join(&format!(",{eol}{inner}"));
                Ok(Splice::insert(
                    list.end_byte() - 1,
                    format!("{eol}{inner}{written}{eol}{outer}"),
                ))
            }
        }
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

    pub(super) fn plan(&self, command: &Command) -> Result<Vec<Splice>, ApplyError> {
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
            | Command::AddToGraph { site, .. }
            | Command::AddRender { site, .. }
            | Command::RemoveRender { site, .. }
            | Command::DeleteBlock { site, .. }
            | Command::DefineBlock { site, .. } => *site,
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
                let Some(var) = config.var.as_deref() else {
                    if config.source != ConfigSource::Absent {
                        return Err(ApplyError::UnsupportedShape {
                            detail: "config has no variable to assign through".to_string(),
                        });
                    }
                    let run = self.node(config.run_call_span)?;
                    let arguments = run.child_by_field_name("arguments").ok_or_else(|| {
                        ApplyError::UnsupportedShape {
                            detail: "run call has no argument list".to_string(),
                        }
                    })?;
                    let call_patch = if arguments.named_child_count() > 0 {
                        ", config"
                    } else {
                        "config"
                    };
                    let statement = self.statement_of(run);
                    let indent = self.indent(statement.start_byte());
                    let eol = self.line_ending();
                    let declaration = format!(
                        "{indent}cler::FlowGraphConfig config;{eol}{indent}config.{path} = {new_value};{eol}"
                    );
                    return Ok(vec![
                        Splice::insert(self.line_start(statement.start_byte()), declaration),
                        Splice::insert(arguments.end_byte() - 1, call_patch.to_string()),
                    ]);
                };
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
                let runs = |var: &str| {
                    target
                        .runners
                        .iter()
                        .any(|runner| runner.block.as_deref() == Some(var))
                };
                let mut tails: Vec<String> = Vec::new();
                if !runs(&source.var) {
                    tails.push(format!("(&{}, {argument})", source.var));
                }
                if !runs(&sink.var) {
                    tails.push(format!("(&{})", sink.var));
                }
                if runs(&source.var) {
                    let runner = self.editable_runner(target, &source.var)?;
                    let list = self.argument_list(self.node(runner.span)?)?;
                    let last = named_children(list).last().copied().ok_or_else(|| {
                        ApplyError::UnsupportedShape {
                            detail: format!("runner for {from} has no arguments"),
                        }
                    })?;
                    let mut splices =
                        vec![Splice::insert(last.end_byte(), format!(", {argument}"))];
                    if !tails.is_empty() {
                        splices.push(self.append_runners(target, &tails)?);
                    }
                    splices.sort_by_key(|splice| splice.start);
                    return Ok(splices);
                }
                Ok(vec![self.append_runners(target, &tails)?])
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
                identifier_name(var_name)?;
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

            Command::AddToGraph { site, block } => {
                let target = self.site(*site)?;
                self.editable_block(*site, block)?;
                if target
                    .runners
                    .iter()
                    .any(|runner| runner.block.as_deref() == Some(block.as_str()))
                {
                    return Err(ApplyError::UnsupportedShape {
                        detail: format!("{block} already has a runner"),
                    });
                }
                Ok(vec![self.append_runners(target, &[format!("(&{block})")])?])
            }

            Command::AddRender { site, block } => {
                let target = self.site(*site)?;
                self.editable_block(*site, block)?;
                match &target.gui {
                    Some(gui) => {
                        if gui.renders.iter().any(|call| call.block == *block) {
                            return Err(ApplyError::UnsupportedShape {
                                detail: format!("{block} already renders"),
                            });
                        }
                        let anchor = self.node(gui.end_frame_span)?;
                        let indent = self.indent(anchor.start_byte());
                        let eol = self.line_ending();
                        Ok(vec![Splice::insert(
                            self.line_start(anchor.start_byte()),
                            format!("{indent}{block}.render();{eol}"),
                        )])
                    }
                    None => self.materialize_gui(target, block),
                }
            }

            Command::RemoveRender { site, block } => {
                let target = self.site(*site)?;
                let gui = target
                    .gui
                    .as_ref()
                    .ok_or_else(|| ApplyError::UnsupportedShape {
                        detail: "this site has no render loop".to_string(),
                    })?;
                let call = gui
                    .renders
                    .iter()
                    .find(|call| call.block == *block)
                    .ok_or_else(|| ApplyError::UnsupportedShape {
                        detail: format!("{block} does not render"),
                    })?;
                let statement = self.node(call.span)?;
                Ok(vec![Splice::remove(
                    self.line_start(statement.start_byte()),
                    self.line_end(statement.end_byte()),
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

            Command::DefineBlock {
                site,
                name,
                value_type,
                inputs,
                outputs,
                params,
                may_block,
            } => {
                let target = self.site(*site)?;
                identifier_name(name)?;
                if name.len() <= BLOCK_SUFFIX.len() || !name.ends_with(BLOCK_SUFFIX) {
                    return Err(ApplyError::InvalidBlockName { name: name.clone() });
                }
                if self.unit_identifiers().contains(name.as_str()) {
                    return Err(ApplyError::DuplicateType { name: name.clone() });
                }
                type_text("value_type", value_type)?;
                if inputs.is_empty() && *outputs == 0 {
                    return Err(ApplyError::UnsupportedShape {
                        detail: format!("{name} declares no inputs and no outputs"),
                    });
                }
                let mut taken: HashSet<String> =
                    (0..*outputs).map(|index| format!("out{index}")).collect();
                taken.insert("name".to_string());
                for member in inputs
                    .iter()
                    .map(|input| &input.name)
                    .chain(params.iter().map(|param| &param.name))
                {
                    identifier_name(member)?;
                    if member.starts_with('_') {
                        return Err(ApplyError::ReservedIdentifier {
                            text: member.clone(),
                        });
                    }
                    if !taken.insert(member.clone()) {
                        return Err(ApplyError::DuplicateVariable {
                            var_name: member.clone(),
                        });
                    }
                }
                for param in params {
                    type_text("cpp_type", &param.cpp_type)?;
                    if let Some(default) = &param.default {
                        expression("default", default)?;
                    }
                }

                let eol = self.line_ending();
                let skeleton = Skeleton {
                    name,
                    value_type,
                    inputs,
                    outputs: *outputs,
                    params,
                    may_block: *may_block,
                    eol,
                }
                .render();
                let function = self.enclosing_function(target)?;
                let at = self.line_start(function.start_byte());
                let head = self.src.get(..at).unwrap_or("");
                let gap = if at == 0 || head.ends_with(&format!("{eol}{eol}")) {
                    ""
                } else {
                    eol
                };
                Ok(vec![Splice::insert(
                    at,
                    format!("{gap}{skeleton}{eol}{eol}"),
                )])
            }
        }
    }
}

use std::collections::{HashMap, HashSet};

use tree_sitter::Node;

use crate::model::*;
use crate::palette_types::BlockSpec;
use crate::sample_types;

const FLOWGRAPH_FACTORY: &str = "make_desktop_flowgraph";
const RUNNER: &str = "BlockRunner";
const RUNNER_MAY_BLOCK: &str = "BlockRunnerMayBlock";
const BLOCK_BASE: &str = "BlockBase";
const BLOCK_SUFFIX: &str = "Block";
const FILE_SCOPE_SKIP: &[&str] = &["function_definition", "field_declaration_list"];
const LOCAL_SCOPE_SKIP: &[&str] = &["field_declaration_list"];

pub fn extract<'t>(root: Node<'t>, src: &'t str, specs: &[&BlockSpec]) -> (Vec<Site>, Vec<Span>) {
    let errors = error_spans(root);
    let file = Extractor::new(root, src);
    let calls: Vec<Node<'t>> = descendants(root)
        .into_iter()
        .filter(|n| n.kind() == "call_expression" && file.callee_name(*n) == FLOWGRAPH_FACTORY)
        .collect();
    let scopes: Vec<(String, Node<'t>)> = calls
        .iter()
        .map(|call| file.enclosing_function(*call))
        .collect();
    let mut per_scope: HashMap<usize, usize> = HashMap::new();
    for (_, scope) in &scopes {
        *per_scope.entry(scope.id()).or_default() += 1;
    }

    let mut sites = Vec::new();
    for (call, (function, scope)) in calls.into_iter().zip(scopes) {
        let mut site = file.scoped(scope).site(call, function, scope);
        sample_types::annotate(&mut site, specs);
        if per_scope.get(&scope.id()).copied().unwrap_or_default() > 1 {
            force_read_only(&mut site, Reason::MultiSiteFunction);
        }
        if !errors.is_empty() {
            force_read_only(&mut site, Reason::ParseError);
        }
        sites.push(site);
    }
    (sites, errors)
}

pub(crate) fn descendants(node: Node) -> Vec<Node> {
    let mut out = Vec::new();
    let mut stack = vec![node];
    while let Some(current) = stack.pop() {
        out.push(current);
        let mut cursor = current.walk();
        let children: Vec<Node> = current.children(&mut cursor).collect();
        for child in children.into_iter().rev() {
            stack.push(child);
        }
    }
    out
}

pub(crate) fn named_children(node: Node) -> Vec<Node> {
    let mut cursor = node.walk();
    node.named_children(&mut cursor)
        .filter(|c| c.kind() != "comment")
        .collect()
}

fn field_children<'t>(node: Node<'t>, field: &str) -> Vec<Node<'t>> {
    let mut cursor = node.walk();
    node.children_by_field_name(field, &mut cursor).collect()
}

fn span(node: Node) -> Span {
    Span {
        start: node.start_byte(),
        end: node.end_byte(),
    }
}

fn error_spans(root: Node) -> Vec<Span> {
    let mut out = Vec::new();
    let mut stack = vec![root];
    while let Some(node) = stack.pop() {
        if node.is_error() || node.is_missing() {
            out.push(span(node));
            continue;
        }
        if !node.has_error() {
            continue;
        }
        let mut cursor = node.walk();
        let children: Vec<Node> = node.children(&mut cursor).collect();
        for child in children.into_iter().rev() {
            stack.push(child);
        }
    }
    out.sort_by_key(|s| s.start);
    out
}

fn enclosed_by(node: Node, stop: Node, kinds: &[&str]) -> bool {
    let mut current = node;
    while let Some(parent) = current.parent() {
        if parent.id() == stop.id() {
            return false;
        }
        if kinds.contains(&parent.kind()) {
            return true;
        }
        current = parent;
    }
    false
}

fn last_identifier<'t>(node: Node<'t>) -> Option<Node<'t>> {
    descendants(node).into_iter().rfind(|n| {
        matches!(
            n.kind(),
            "identifier" | "type_identifier" | "field_identifier"
        )
    })
}

fn leftmost_identifier<'t>(node: Node<'t>) -> Option<Node<'t>> {
    let mut current = node;
    loop {
        match current.kind() {
            "identifier" => return Some(current),
            _ => match current.named_child(0) {
                Some(child) => current = child,
                None => return None,
            },
        }
    }
}

fn unescape(literal: &str) -> Option<String> {
    let inner = literal.strip_prefix('"')?.strip_suffix('"')?;
    let mut out = String::with_capacity(inner.len());
    let mut chars = inner.chars();
    while let Some(c) = chars.next() {
        if c != '\\' {
            out.push(c);
            continue;
        }
        match chars.next()? {
            '\\' => out.push('\\'),
            '"' => out.push('"'),
            '\'' => out.push('\''),
            '?' => out.push('?'),
            _ => return None,
        }
    }
    Some(out)
}

fn force_read_only(site: &mut Site, reason: Reason) {
    for block in site.blocks.iter_mut() {
        block.editable = false;
        block.read_only_reason = block.read_only_reason.or(Some(reason));
    }
    for runner in site.runners.iter_mut() {
        runner.editable = false;
        runner.read_only_reason = runner.read_only_reason.or(Some(reason));
    }
    for edge in site.edges.iter_mut() {
        edge.editable = false;
        edge.read_only_reason = edge.read_only_reason.or(Some(reason));
    }
    if let Some(config) = site.config.as_mut() {
        config.editable = false;
        config.read_only_reason = config.read_only_reason.or(Some(reason));
        for assignment in config.assignments.iter_mut() {
            assignment.editable = false;
            assignment.read_only_reason = assignment.read_only_reason.or(Some(reason));
        }
    }
    site.editable = false;
    site.read_only_reason = Some(reason);
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum DeclForm {
    CtorCall,
    BraceInit,
    Bare,
    Other,
}

#[derive(Clone)]
struct Decl<'t> {
    var: String,
    type_node: Node<'t>,
    args: Option<Node<'t>>,
    node: Node<'t>,
    form: DeclForm,
}

struct TypeRef {
    text: String,
    name: String,
    alias: Option<String>,
    template_args: Vec<TemplateArg>,
    optional_wrapped: bool,
}

enum BlockExpr {
    Var(String),
    VarReadOnly(String, Reason),
    Unresolved,
}

enum PortExpr {
    Resolved {
        var: String,
        port: Port,
        reason: Option<Reason>,
    },
    Unresolved,
}

#[derive(Clone)]
struct Extractor<'t> {
    src: &'t str,
    root: Node<'t>,
    aliases: HashMap<String, Option<Node<'t>>>,
    constants: HashMap<String, Option<String>>,
    block_base_types: HashSet<String>,
    file_decls: Vec<Decl<'t>>,
}

impl<'t> Extractor<'t> {
    fn new(root: Node<'t>, src: &'t str) -> Self {
        let mut extractor = Extractor {
            src,
            root,
            aliases: HashMap::new(),
            constants: HashMap::new(),
            block_base_types: HashSet::new(),
            file_decls: Vec::new(),
        };
        extractor.block_base_types = extractor.collect_block_base_types();
        let file_nodes = extractor.scope_nodes(root, FILE_SCOPE_SKIP);
        extractor.aliases = extractor.collect_aliases(&file_nodes);
        extractor.constants = extractor.collect_constants(&file_nodes);
        let mut file_decls = Vec::new();
        extractor.push_declarations(&mut file_decls, &file_nodes, usize::MAX);
        extractor.file_decls = file_decls;
        extractor
    }

    fn scoped(&self, scope: Node<'t>) -> Self {
        if scope.id() == self.root.id() {
            return self.clone();
        }
        let nodes = self.scope_nodes(scope, LOCAL_SCOPE_SKIP);
        let mut scoped = self.clone();
        scoped.aliases.extend(self.collect_aliases(&nodes));
        scoped.constants.extend(self.collect_constants(&nodes));
        scoped
    }

    fn scope_nodes(&self, scope: Node<'t>, skip: &[&str]) -> Vec<Node<'t>> {
        let mut out = Vec::new();
        let mut stack = vec![scope];
        while let Some(current) = stack.pop() {
            out.push(current);
            let mut cursor = current.walk();
            let children: Vec<Node<'t>> = current
                .children(&mut cursor)
                .filter(|child| !skip.contains(&child.kind()))
                .collect();
            for child in children.into_iter().rev() {
                stack.push(child);
            }
        }
        out
    }

    fn text(&self, node: Node) -> &'t str {
        self.src.get(node.byte_range()).unwrap_or("")
    }

    fn collect_block_base_types(&self) -> HashSet<String> {
        let mut names = HashSet::new();
        for node in descendants(self.root) {
            if !matches!(node.kind(), "struct_specifier" | "class_specifier") {
                continue;
            }
            let mut cursor = node.walk();
            let bases = node
                .children(&mut cursor)
                .find(|c| c.kind() == "base_class_clause");
            let (Some(name), Some(bases)) = (node.child_by_field_name("name"), bases) else {
                continue;
            };
            if descendants(bases)
                .into_iter()
                .any(|n| n.kind() == "type_identifier" && self.text(n) == BLOCK_BASE)
            {
                names.insert(self.text(name).to_string());
            }
        }
        names
    }

    fn collect_aliases(&self, nodes: &[Node<'t>]) -> HashMap<String, Option<Node<'t>>> {
        let mut aliases: HashMap<String, Option<Node<'t>>> = HashMap::new();
        for node in nodes {
            if node.kind() != "alias_declaration" {
                continue;
            }
            let children = named_children(*node);
            let (Some(name), Some(target)) = (children.first(), children.last()) else {
                continue;
            };
            if name.id() == target.id() {
                continue;
            }
            let key = self.text(*name).to_string();
            match aliases.get(&key) {
                Some(Some(previous)) if self.text(*previous) != self.text(*target) => {
                    aliases.insert(key, None);
                }
                Some(_) => {}
                None => {
                    aliases.insert(key, Some(*target));
                }
            }
        }
        aliases
    }

    fn collect_constants(&self, nodes: &[Node<'t>]) -> HashMap<String, Option<String>> {
        let mut constants: HashMap<String, Option<String>> = HashMap::new();
        for node in nodes {
            if node.kind() != "declaration" || !self.is_constant_declaration(*node) {
                continue;
            }
            for declarator in field_children(*node, "declarator") {
                if declarator.kind() != "init_declarator" {
                    continue;
                }
                let (Some(name), Some(value)) = (
                    declarator.child_by_field_name("declarator"),
                    declarator.child_by_field_name("value"),
                ) else {
                    continue;
                };
                if name.kind() != "identifier" || value.kind() != "number_literal" {
                    continue;
                }
                let key = self.text(name).to_string();
                let text = self.text(value).to_string();
                match constants.get(&key) {
                    Some(Some(previous)) if *previous != text => {
                        constants.insert(key, None);
                    }
                    Some(_) => {}
                    None => {
                        constants.insert(key, Some(text));
                    }
                }
            }
        }
        constants
    }

    fn constant(&self, name: &str) -> Option<String> {
        self.constants.get(name).cloned().flatten()
    }

    fn is_constant_declaration(&self, node: Node) -> bool {
        let mut cursor = node.walk();
        let qualified = node.children(&mut cursor).any(|child| {
            child.kind() == "type_qualifier" && matches!(self.text(child), "const" | "constexpr")
        });
        qualified
    }

    fn callee_name(&self, call: Node) -> String {
        let Some(function) = call.child_by_field_name("function") else {
            return String::new();
        };
        match function.kind() {
            "identifier" | "field_identifier" => self.text(function).to_string(),
            _ => last_identifier(function)
                .map(|n| self.text(n).to_string())
                .unwrap_or_default(),
        }
    }

    fn enclosing_function(&self, node: Node<'t>) -> (String, Node<'t>) {
        let mut current = node;
        while let Some(parent) = current.parent() {
            if parent.kind() == "function_definition" {
                let name = parent
                    .child_by_field_name("declarator")
                    .and_then(function_name_node)
                    .map(|n| self.text(n).to_string())
                    .unwrap_or_else(|| "<anonymous>".to_string());
                let body = parent.child_by_field_name("body").unwrap_or(parent);
                return (name, body);
            }
            current = parent;
        }
        ("<file scope>".to_string(), self.root)
    }

    fn declarations(&self, scope: Node<'t>, before: usize) -> Vec<Decl<'t>> {
        if scope.id() == self.root.id() {
            let mut decls = Vec::new();
            self.push_declarations(
                &mut decls,
                &self.scope_nodes(scope, LOCAL_SCOPE_SKIP),
                before,
            );
            return decls;
        }
        let mut decls = self.file_decls.clone();
        self.push_declarations(
            &mut decls,
            &self.scope_nodes(scope, LOCAL_SCOPE_SKIP),
            before,
        );
        decls
    }

    fn push_declarations(&self, decls: &mut Vec<Decl<'t>>, nodes: &[Node<'t>], before: usize) {
        for node in nodes {
            if node.kind() != "declaration" || node.start_byte() >= before {
                continue;
            }
            let Some(type_node) = node.child_by_field_name("type") else {
                continue;
            };
            for declarator in field_children(*node, "declarator") {
                match declarator.kind() {
                    "identifier" => decls.push(Decl {
                        var: self.text(declarator).to_string(),
                        type_node,
                        args: None,
                        node: *node,
                        form: DeclForm::Bare,
                    }),
                    "init_declarator" => {
                        let Some(name) = declarator.child_by_field_name("declarator") else {
                            continue;
                        };
                        if name.kind() != "identifier" {
                            continue;
                        }
                        let value = declarator.child_by_field_name("value");
                        let form = match value.map(|v| v.kind()) {
                            Some("argument_list") => DeclForm::CtorCall,
                            Some("initializer_list") => DeclForm::BraceInit,
                            _ => DeclForm::Other,
                        };
                        decls.push(Decl {
                            var: self.text(name).to_string(),
                            type_node,
                            args: match form {
                                DeclForm::CtorCall | DeclForm::BraceInit => value,
                                _ => None,
                            },
                            node: *node,
                            form,
                        });
                    }
                    _ => {}
                }
            }
        }
    }

    fn type_ref(&self, node: Node<'t>) -> TypeRef {
        let text = self.text(node).to_string();
        let mut current = node;
        if current.kind() == "type_descriptor" {
            if let Some(inner) = current.named_child(0) {
                current = inner;
            }
        }
        if current.kind() == "qualified_identifier" {
            if let Some(inner) = current.child_by_field_name("name") {
                current = inner;
            }
        }
        let (name_node, arg_list) = match current.kind() {
            "template_type" => (
                current.child_by_field_name("name"),
                current.child_by_field_name("arguments"),
            ),
            _ => (Some(current), None),
        };
        let name = name_node
            .map(|n| self.text(n).to_string())
            .unwrap_or_default();

        if let Some(Some(target)) = self.aliases.get(&name) {
            let mut resolved = self.type_ref(*target);
            resolved.text = text;
            resolved.alias = Some(name);
            return resolved;
        }

        let template_args = arg_list
            .map(|list| self.template_args(list))
            .unwrap_or_default();

        if name == "optional" && template_args.len() == 1 {
            if let Some(inner) = arg_list.and_then(|list| named_children(list).into_iter().next()) {
                let mut resolved = self.type_ref(inner);
                resolved.text = text;
                resolved.optional_wrapped = true;
                return resolved;
            }
        }

        TypeRef {
            text,
            name,
            alias: None,
            template_args,
            optional_wrapped: false,
        }
    }

    fn template_args(&self, list: Node<'t>) -> Vec<TemplateArg> {
        named_children(list)
            .into_iter()
            .map(|arg| {
                let text = self.text(arg).to_string();
                let resolved = if arg.kind() == "number_literal" {
                    Some(text.clone())
                } else {
                    self.constant(&text)
                };
                TemplateArg {
                    text,
                    resolved,
                    span: span(arg),
                }
            })
            .collect()
    }

    fn ctor_args(&self, list: Node<'t>) -> Vec<CtorArg> {
        named_children(list)
            .into_iter()
            .map(|arg| CtorArg {
                text: self.text(arg).to_string(),
                span: span(arg),
            })
            .collect()
    }

    fn display_name(&self, args: Option<Node<'t>>) -> (Option<String>, Option<Reason>) {
        let Some(first) = args.and_then(|list| named_children(list).into_iter().next()) else {
            return (None, None);
        };
        let text = self.text(first);
        match first.kind() {
            "string_literal" => match unescape(text) {
                Some(value) => (Some(value), None),
                None => (Some(text.to_string()), Some(Reason::UnsupportedDisplayName)),
            },
            "concatenated_string" => (
                Some(text.to_string()),
                Some(Reason::ConcatenatedDisplayName),
            ),
            _ => (None, None),
        }
    }

    fn emplace_args(&self, scope: Node<'t>, var: &str) -> Vec<Node<'t>> {
        descendants(scope)
            .into_iter()
            .filter_map(|node| {
                if node.kind() != "call_expression" || self.callee_name(node) != "emplace" {
                    return None;
                }
                let function = node.child_by_field_name("function")?;
                let object = function.child_by_field_name("argument")?;
                if self.text(object) == var {
                    node.child_by_field_name("arguments")
                } else {
                    None
                }
            })
            .collect()
    }

    fn block_expr(&self, node: Node<'t>) -> BlockExpr {
        if node.kind() != "pointer_expression" || !self.text(node).starts_with('&') {
            return BlockExpr::Unresolved;
        }
        let Some(inner) = node.named_child(0) else {
            return BlockExpr::Unresolved;
        };
        match inner.kind() {
            "identifier" => BlockExpr::Var(self.text(inner).to_string()),
            "pointer_expression" => match inner.named_child(0) {
                Some(target) if target.kind() == "identifier" => BlockExpr::VarReadOnly(
                    self.text(target).to_string(),
                    Reason::UnsupportedBlockExpression,
                ),
                _ => BlockExpr::Unresolved,
            },
            _ => BlockExpr::Unresolved,
        }
    }

    fn port_expr(&self, node: Node<'t>) -> PortExpr {
        if node.kind() == "call_expression" {
            let Some(function) = node.child_by_field_name("function") else {
                return PortExpr::Unresolved;
            };
            if function.kind() != "field_expression" {
                return PortExpr::Unresolved;
            }
            let (Some(object), Some(field)) = (
                function.child_by_field_name("argument"),
                function.child_by_field_name("field"),
            ) else {
                return PortExpr::Unresolved;
            };
            if object.kind() != "identifier" {
                return PortExpr::Unresolved;
            }
            return PortExpr::Resolved {
                var: self.text(object).to_string(),
                port: Port {
                    name: self.text(field).to_string(),
                    index: None,
                    kind: PortKind::MethodCall,
                },
                reason: Some(Reason::MethodCallPort),
            };
        }

        if node.kind() != "pointer_expression" || !self.text(node).starts_with('&') {
            return PortExpr::Unresolved;
        }
        let Some(inner) = node.named_child(0) else {
            return PortExpr::Unresolved;
        };
        match inner.kind() {
            "field_expression" => self.field_port(inner, None),
            "subscript_expression" => {
                let (Some(base), Some(index)) = (
                    inner.child_by_field_name("argument"),
                    inner.child_by_field_name("index").or_else(|| {
                        inner
                            .child_by_field_name("indices")
                            .and_then(|list| named_children(list).into_iter().next())
                    }),
                ) else {
                    return PortExpr::Unresolved;
                };
                if base.kind() != "field_expression" {
                    return PortExpr::Unresolved;
                }
                self.field_port(base, Some(index))
            }
            _ => PortExpr::Unresolved,
        }
    }

    fn field_port(&self, field_expr: Node<'t>, index: Option<Node<'t>>) -> PortExpr {
        let (Some(object), Some(field)) = (
            field_expr.child_by_field_name("argument"),
            field_expr.child_by_field_name("field"),
        ) else {
            return PortExpr::Unresolved;
        };
        if object.kind() != "identifier" {
            return PortExpr::Unresolved;
        }
        let (kind, resolved_index, reason) = match index {
            None => (PortKind::Field, None, None),
            Some(node) => {
                let text = self.text(node);
                let value = text
                    .parse::<usize>()
                    .ok()
                    .or_else(|| self.constant(text).and_then(|c| c.parse().ok()));
                match value {
                    Some(value) => (PortKind::IndexedField, Some(value), None),
                    None => (
                        PortKind::IndexedField,
                        None,
                        Some(Reason::UnresolvedPortIndex),
                    ),
                }
            }
        };
        PortExpr::Resolved {
            var: self.text(object).to_string(),
            port: Port {
                name: self.text(field).to_string(),
                index: resolved_index,
                kind,
            },
            reason,
        }
    }

    fn site(&self, call: Node<'t>, function: String, scope: Node<'t>) -> Site {
        let decls = self.declarations(scope, call.start_byte());
        let flowgraph_var = flowgraph_variable(call).map(|n| self.text(n).to_string());
        let args = call.child_by_field_name("arguments");

        let mut runners = Vec::new();
        let mut edges = Vec::new();
        let mut unresolved = Vec::new();
        let mut referenced: Vec<(String, Option<Reason>)> = Vec::new();

        for (runner_index, entry) in args
            .map(named_children)
            .unwrap_or_default()
            .into_iter()
            .enumerate()
        {
            let (runner_node, form, form_reason) = match self.runner_source(entry, &decls) {
                Some(source) => source,
                None => {
                    unresolved.push(Unresolved {
                        text: self.text(entry).to_string(),
                        span: span(entry),
                        runner_index,
                        arg_index: 0,
                        reason: Reason::UnsupportedRunnerExpression,
                    });
                    runners.push(Runner {
                        index: runner_index,
                        block: None,
                        block_expr: self.text(entry).to_string(),
                        may_block: false,
                        form: RunnerForm::Inline,
                        span: span(entry),
                        editable: false,
                        read_only_reason: Some(Reason::UnsupportedRunnerExpression),
                    });
                    continue;
                }
            };

            let may_block = self.callee_name(runner_node) == RUNNER_MAY_BLOCK
                || runner_node
                    .child_by_field_name("type")
                    .map(|t| self.text(t).ends_with(RUNNER_MAY_BLOCK))
                    .unwrap_or(false);

            let runner_args = self
                .runner_arguments(runner_node)
                .map(named_children)
                .unwrap_or_default();

            let mut runner_reason = form_reason;
            let mut block_var = None;
            let block_expr_text = runner_args
                .first()
                .map(|n| self.text(*n).to_string())
                .unwrap_or_default();

            if let Some(first) = runner_args.first() {
                match self.block_expr(*first) {
                    BlockExpr::Var(var) => {
                        referenced.push((var.clone(), None));
                        block_var = Some(var);
                    }
                    BlockExpr::VarReadOnly(var, reason) => {
                        referenced.push((var.clone(), Some(reason)));
                        block_var = Some(var);
                        runner_reason = runner_reason.or(Some(reason));
                    }
                    BlockExpr::Unresolved => {
                        unresolved.push(Unresolved {
                            text: self.text(*first).to_string(),
                            span: span(*first),
                            runner_index,
                            arg_index: 0,
                            reason: Reason::UnresolvedRunnerArgument,
                        });
                        runner_reason = runner_reason.or(Some(Reason::UnresolvedRunnerArgument));
                    }
                }
            }

            for (offset, arg) in runner_args.iter().skip(1).enumerate() {
                let arg_index = offset + 1;
                match self.port_expr(*arg) {
                    PortExpr::Resolved { var, port, reason } => {
                        referenced.push((var.clone(), reason));
                        let edge_reason = reason.or(runner_reason);
                        edges.push(Edge {
                            from: block_var.clone().unwrap_or_default(),
                            to: var,
                            port,
                            runner_index,
                            arg_index,
                            text: self.text(*arg).to_string(),
                            span: span(*arg),
                            editable: edge_reason.is_none() && block_var.is_some(),
                            read_only_reason: edge_reason,
                            sample_type: None,
                            source_type: None,
                            type_conflict: false,
                        });
                    }
                    PortExpr::Unresolved => {
                        unresolved.push(Unresolved {
                            text: self.text(*arg).to_string(),
                            span: span(*arg),
                            runner_index,
                            arg_index,
                            reason: Reason::UnresolvedRunnerArgument,
                        });
                        runner_reason = runner_reason.or(Some(Reason::UnresolvedRunnerArgument));
                    }
                }
            }

            runners.push(Runner {
                index: runner_index,
                block: block_var,
                block_expr: block_expr_text,
                may_block,
                form,
                span: span(entry),
                editable: runner_reason.is_none(),
                read_only_reason: runner_reason,
            });
        }

        for edge in edges.iter_mut() {
            if edge.read_only_reason.is_none() {
                if let Some(reason) = runners
                    .get(edge.runner_index)
                    .and_then(|r| r.read_only_reason)
                {
                    edge.read_only_reason = Some(reason);
                    edge.editable = false;
                }
            }
        }

        let blocks = self.blocks(scope, &decls, &referenced);
        let config = self.config(scope, call, flowgraph_var.as_deref());

        let mut site = Site {
            function,
            call_offset: call.start_byte(),
            span: span(call),
            flowgraph_var,
            blocks,
            runners,
            edges,
            config,
            unresolved,
            editable: true,
            read_only_reason: None,
        };
        if site.read_only_elements() > 0 {
            site.editable = false;
            site.read_only_reason = Some(Reason::SiteHasReadOnlyElements);
        }
        if site.flowgraph_var.is_none() {
            force_read_only(&mut site, Reason::UnboundFlowgraph);
        }
        site
    }

    fn runner_source(
        &self,
        entry: Node<'t>,
        decls: &[Decl<'t>],
    ) -> Option<(Node<'t>, RunnerForm, Option<Reason>)> {
        if entry.kind() == "call_expression" {
            let callee = self.callee_name(entry);
            if callee == RUNNER || callee == RUNNER_MAY_BLOCK {
                return Some((entry, RunnerForm::Inline, None));
            }
            return None;
        }
        if entry.kind() == "identifier" {
            let var = self.text(entry);
            let decl = decls
                .iter()
                .rfind(|d| d.var == var && d.form == DeclForm::CtorCall)?;
            let type_name = self.type_ref(decl.type_node).name;
            if type_name != RUNNER && type_name != RUNNER_MAY_BLOCK {
                return None;
            }
            return Some((
                decl.node,
                RunnerForm::NamedVariable,
                Some(Reason::NamedRunnerVariable),
            ));
        }
        None
    }

    fn runner_arguments(&self, runner: Node<'t>) -> Option<Node<'t>> {
        match runner.kind() {
            "call_expression" => runner.child_by_field_name("arguments"),
            "declaration" => field_children(runner, "declarator")
                .into_iter()
                .find(|d| d.kind() == "init_declarator")
                .and_then(|d| d.child_by_field_name("value")),
            _ => None,
        }
    }

    fn renders_unwired(&self, type_ref: &TypeRef, args: &[CtorArg]) -> bool {
        if self.block_base_types.contains(&type_ref.name) {
            return true;
        }
        type_ref.name.ends_with(BLOCK_SUFFIX)
            && args
                .first()
                .map(|arg| arg.text.starts_with('"'))
                .unwrap_or(false)
    }

    fn blocks(
        &self,
        scope: Node<'t>,
        decls: &[Decl<'t>],
        referenced: &[(String, Option<Reason>)],
    ) -> Vec<Block> {
        let mut blocks: Vec<Block> = Vec::new();
        for (index, decl) in decls.iter().enumerate() {
            if decls[index + 1..].iter().any(|d| d.var == decl.var) {
                continue;
            }
            let type_ref = self.type_ref(decl.type_node);
            let optional = decl.form == DeclForm::Bare && type_ref.optional_wrapped;
            let emplaces = if optional {
                self.emplace_args(scope, &decl.var)
            } else {
                Vec::new()
            };
            let arg_list = decl.args.or_else(|| emplaces.first().copied());
            let args = arg_list
                .map(|list| self.ctor_args(list))
                .unwrap_or_default();
            let in_graph = referenced.iter().any(|(var, _)| *var == decl.var);
            if !in_graph && !self.renders_unwired(&type_ref, &args) {
                continue;
            }
            let (display_name, name_reason) = self.display_name(arg_list);
            let reason = if decls[..index].iter().any(|d| d.var == decl.var) {
                Some(Reason::AmbiguousDeclaration)
            } else if optional {
                Some(if emplaces.len() > 1 {
                    Reason::ConditionalConstruction
                } else {
                    Reason::OptionalEmplaceDeclaration
                })
            } else {
                match decl.form {
                    DeclForm::CtorCall => name_reason,
                    DeclForm::BraceInit => Some(Reason::BraceInitDeclaration),
                    _ => Some(Reason::UnsupportedDeclaration),
                }
            };
            blocks.push(Block {
                var: decl.var.clone(),
                type_text: type_ref.text,
                type_name: type_ref.name,
                alias: type_ref.alias,
                template_args: type_ref.template_args,
                display_name,
                ctor_args: args,
                in_graph,
                span: span(decl.node),
                editable: reason.is_none(),
                read_only_reason: reason,
            });
        }
        for (var, _) in referenced {
            if blocks.iter().any(|b| b.var == *var) {
                continue;
            }
            blocks.push(Block {
                var: var.clone(),
                type_text: String::new(),
                type_name: String::new(),
                alias: None,
                template_args: Vec::new(),
                ctor_args: Vec::new(),
                display_name: None,
                in_graph: true,
                span: Span { start: 0, end: 0 },
                editable: false,
                read_only_reason: Some(Reason::UndeclaredBlock),
            });
        }
        blocks
    }

    fn run_call(
        &self,
        scope: Node<'t>,
        call: Node<'t>,
        flowgraph_var: Option<&str>,
    ) -> Option<Node<'t>> {
        let Some(var) = flowgraph_var else {
            let field = call.parent()?;
            if field.kind() != "field_expression" {
                return None;
            }
            let outer = field.parent()?;
            return (outer.kind() == "call_expression"
                && matches!(self.callee_name(outer).as_str(), "run" | "run_for"))
            .then_some(outer);
        };
        descendants(scope).into_iter().find(|node| {
            node.kind() == "call_expression"
                && node.start_byte() > call.end_byte()
                && matches!(self.callee_name(*node).as_str(), "run" | "run_for")
                && node
                    .child_by_field_name("function")
                    .and_then(|f| f.child_by_field_name("argument"))
                    .map(|object| self.text(object) == var)
                    .unwrap_or(false)
        })
    }

    fn config(
        &self,
        scope: Node<'t>,
        call: Node<'t>,
        flowgraph_var: Option<&str>,
    ) -> Option<Config> {
        let run_call = self.run_call(scope, call, flowgraph_var)?;
        let arguments = run_call
            .child_by_field_name("arguments")
            .map(named_children)
            .unwrap_or_default();
        let argument = if self.callee_name(run_call) == "run_for" {
            (arguments.len() > 1).then(|| arguments[arguments.len() - 1])?
        } else {
            arguments.first().copied()?
        };

        if argument.kind() != "identifier" {
            return Some(Config {
                var: None,
                source: ConfigSource::InlineTemporary,
                assignments: Vec::new(),
                run_call_span: span(run_call),
                editable: false,
                read_only_reason: Some(Reason::InlineConfigTemporary),
            });
        }

        let var = self.text(argument).to_string();
        let decls = self.declarations(scope, run_call.start_byte());
        let decl = decls.iter().rfind(|d| d.var == var);
        let decl_reason = match decl {
            Some(decl) if decl.form == DeclForm::Bare => None,
            Some(decl) if self.text(decl.node).contains("flowgraph_config::") => {
                Some(Reason::FactoryConfig)
            }
            _ => Some(Reason::UnsupportedConfigDeclaration),
        };
        let assignments = self.config_assignments(scope, &var, run_call.start_byte());
        let reason = decl_reason.or_else(|| {
            assignments
                .iter()
                .find_map(|assignment| assignment.read_only_reason)
        });
        Some(Config {
            var: Some(var),
            source: ConfigSource::Variable,
            assignments,
            run_call_span: span(run_call),
            editable: reason.is_none(),
            read_only_reason: reason,
        })
    }

    fn config_assignments(
        &self,
        scope: Node<'t>,
        var: &str,
        before: usize,
    ) -> Vec<ConfigAssignment> {
        let mut assignments = Vec::new();
        for node in descendants(scope) {
            if node.kind() != "assignment_expression" || node.start_byte() >= before {
                continue;
            }
            if enclosed_by(node, scope, &["lambda_expression"]) {
                continue;
            }
            let (Some(left), Some(right)) = (
                node.child_by_field_name("left"),
                node.child_by_field_name("right"),
            ) else {
                continue;
            };
            if leftmost_identifier(left).map(|n| self.text(n)) != Some(var) {
                continue;
            }
            let path = self.field_path(left, var);
            assignments.push(ConfigAssignment {
                path: path.clone().unwrap_or_else(|| self.text(left).to_string()),
                value: self.text(right).to_string(),
                span: span(node),
                value_span: span(right),
                editable: path.is_some(),
                read_only_reason: path.is_none().then_some(Reason::UnsupportedConfigTarget),
            });
        }
        let repeated: Vec<String> = assignments
            .iter()
            .filter(|assignment| {
                assignments
                    .iter()
                    .filter(|other| other.path == assignment.path)
                    .count()
                    > 1
            })
            .map(|assignment| assignment.path.clone())
            .collect();
        for assignment in assignments.iter_mut() {
            if repeated.contains(&assignment.path) {
                assignment.editable = false;
                assignment.read_only_reason = assignment
                    .read_only_reason
                    .or(Some(Reason::AmbiguousConfigAssignment));
            }
        }
        assignments
    }

    fn field_path(&self, node: Node<'t>, var: &str) -> Option<String> {
        if node.kind() != "field_expression" {
            return None;
        }
        let object = node.child_by_field_name("argument")?;
        let field = self.text(node.child_by_field_name("field")?).to_string();
        if object.kind() == "identifier" && self.text(object) == var {
            return Some(field);
        }
        let parent = self.field_path(object, var)?;
        Some(format!("{parent}.{field}"))
    }
}

fn function_name_node(declarator: Node) -> Option<Node> {
    let mut current = declarator;
    loop {
        match current.kind() {
            "identifier" | "qualified_identifier" | "field_identifier" | "destructor_name" => {
                return Some(current)
            }
            _ => current = current.child_by_field_name("declarator")?,
        }
    }
}

fn flowgraph_variable(call: Node) -> Option<Node> {
    let parent = call.parent()?;
    if parent.kind() != "init_declarator" {
        return None;
    }
    let declarator = parent.child_by_field_name("declarator")?;
    (declarator.kind() == "identifier").then_some(declarator)
}

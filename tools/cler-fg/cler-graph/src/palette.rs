use std::path::{Path, PathBuf};

use crate::palette_types::*;
use tree_sitter::{Node, Parser, Tree};

const MAY_BLOCK: &str = "may_block";
const PALETTE_EXTENSIONS: &[&str] = &["hpp", "cpp"];
const CHANNEL: &str = "Channel<";
const CHANNEL_BASE: &str = "ChannelBase<";
const PROCEDURE: &str = "procedure(";

pub fn extract_specs(source: &str, origin: &str) -> Vec<BlockSpec> {
    let Some(tree) = parse_cpp(source) else {
        return Vec::new();
    };
    let code = blank_noncode(source);
    let root = tree.root_node();
    let mut specs = Vec::new();
    collect_blocks(root, source, &code, origin, &mut specs);
    attach_synonyms(root, source, &mut specs);
    specs
}

pub fn source_files(root: &Path, extensions: &[&str]) -> Result<Vec<PathBuf>, crate::Error> {
    if root.is_file() {
        return Ok(vec![root.to_path_buf()]);
    }
    let mut files = Vec::new();
    let mut stack = vec![root.to_path_buf()];
    while let Some(current) = stack.pop() {
        let entries = std::fs::read_dir(&current).map_err(|cause| crate::Error::Read {
            path: current.clone(),
            cause,
        })?;
        for entry in entries.flatten() {
            let path = entry.path();
            if path.is_dir() {
                stack.push(path);
            } else if path
                .extension()
                .is_some_and(|found| extensions.iter().any(|wanted| found == *wanted))
            {
                files.push(path);
            }
        }
    }
    files.sort();
    Ok(files)
}

pub fn palette_specs(roots: &[PathBuf]) -> Result<Vec<BlockSpec>, crate::Error> {
    let mut specs = Vec::new();
    for root in roots {
        for file in source_files(root, PALETTE_EXTENSIONS)? {
            let source = std::fs::read_to_string(&file).map_err(|cause| crate::Error::Read {
                path: file.clone(),
                cause,
            })?;
            specs.extend(extract_specs(&source, &file.display().to_string()));
        }
    }
    Ok(specs)
}

fn parse_cpp(source: &str) -> Option<Tree> {
    let mut parser = Parser::new();
    parser
        .set_language(&tree_sitter_cpp::LANGUAGE.into())
        .ok()?;
    parser.parse(source, None)
}

fn collect_blocks(node: Node, src: &str, code: &str, origin: &str, out: &mut Vec<BlockSpec>) {
    let is_record = node.kind() == "struct_specifier" || node.kind() == "class_specifier";
    if is_record && derives_block_base(node, src) {
        if let Some(spec) = block_spec(node, src, code, origin) {
            out.push(spec);
            return;
        }
    }
    let mut cursor = node.walk();
    for child in node.children(&mut cursor) {
        collect_blocks(child, src, code, origin, out);
    }
}

fn derives_block_base(node: Node, src: &str) -> bool {
    let Some(bases) = child_of_kind(node, "base_class_clause") else {
        return false;
    };
    let mut cursor = bases.walk();
    let derived = bases
        .named_children(&mut cursor)
        .any(|n| last_segment(text(n, src)) == "BlockBase");
    derived
}

fn block_spec(node: Node, src: &str, code: &str, origin: &str) -> Option<BlockSpec> {
    let name = text(node.child_by_field_name("name")?, src).to_string();
    let body = node.child_by_field_name("body")?;
    let extent = struct_end(code, body.start_byte());
    let body_code = &code[body.start_byte()..extent];

    let template_params = enclosing_template_params(node, src);
    let aliases = struct_aliases(body, src);
    let ctors = find_ctors(body, &name, src);
    let members = public_members(body, src, extent, node.kind() == "struct_specifier");
    let conditional_members = conditional_fields(body, extent);

    let mut ports = Vec::new();
    let mut inputs = Vec::new();
    for member in &members {
        if let Member::Channel {
            name,
            element,
            group,
        } = member
        {
            ports.push(Port {
                name: name.clone(),
                direction: Direction::Input,
                element_type: resolve(element, &aliases),
                variable: !matches!(group, Group::Scalar),
            });
            inputs.push(group_count(group, name, &ctors, &template_params, code));
        }
    }
    let variadic = variadic_procedure(body_code);
    let mut outputs = Vec::new();
    match &variadic {
        Some(pack) => {
            ports.push(Port {
                name: pack.param.clone(),
                direction: Direction::Output,
                element_type: pack.type_name.clone(),
                variable: true,
            });
            outputs.push(agreed(&ctors, |ctor| {
                pack_count_ref(body_code, &pack.type_name)
                    .map(|bound| classify(&bound, &template_params, ctor))
                    .unwrap_or(PortCount::Unknown)
            }));
        }
        None => {
            for member in &members {
                if let Member::Procedure { outputs: declared } = member {
                    for (out_name, element) in declared {
                        ports.push(Port {
                            name: out_name.clone(),
                            direction: Direction::Output,
                            element_type: resolve(element, &aliases),
                            variable: false,
                        });
                        outputs.push(PortCount::Fixed(1));
                    }
                }
            }
        }
    }

    let opaque = conditional_members
        || ports.iter().any(|p| unresolved(&p.element_type))
        || members.iter().any(|m| matches!(m, Member::ChannelAccessor))
        || code[body.end_byte().min(extent)..extent].contains(CHANNEL);

    Some(BlockSpec {
        name,
        origin: origin.to_string(),
        synonyms: Vec::new(),
        template_params,
        ctor_params: ctors
            .into_iter()
            .next()
            .map(|c| c.params)
            .unwrap_or_default(),
        may_block: declares_may_block(body, src, extent),
        is_gui: declares_is_gui(body, src, extent),
        conditional_members,
        ports,
        input_count: if opaque {
            PortCount::Unknown
        } else {
            combine(&inputs)
        },
        output_count: if opaque {
            PortCount::Unknown
        } else {
            combine(&outputs)
        },
    })
}

fn combine(parts: &[PortCount]) -> PortCount {
    let mut total = 0usize;
    for part in parts {
        match part {
            PortCount::Fixed(n) => total += n,
            _ => {
                return if parts.len() == 1 {
                    parts[0]
                } else {
                    PortCount::Unknown
                }
            }
        }
    }
    PortCount::Fixed(total)
}

fn group_count(
    group: &Group,
    port: &str,
    ctors: &[Ctor],
    template_params: &[TemplateParam],
    code: &str,
) -> PortCount {
    match group {
        Group::Scalar => PortCount::Fixed(1),
        Group::Array(size) => match size.trim().parse::<usize>() {
            Ok(n) => PortCount::Fixed(n),
            Err(_) => template_params
                .iter()
                .position(|p| p.name == size.trim())
                .map(PortCount::TemplateArg)
                .unwrap_or(PortCount::Unknown),
        },
        Group::Pointer => agreed(ctors, |ctor| match ctor.body {
            Some(ctor_body) => placement_new_bound(ctor_body, code, port)
                .map(|bound| classify(&bound, template_params, ctor))
                .unwrap_or(PortCount::Unknown),
            None => sole_vector_param(ctor)
                .map(PortCount::CtorArgLen)
                .unwrap_or(PortCount::Unknown),
        }),
    }
}

fn agreed(ctors: &[Ctor], evidence: impl Fn(&Ctor) -> PortCount) -> PortCount {
    let mut verdict: Option<PortCount> = None;
    for ctor in ctors {
        let count = evidence(ctor);
        match verdict {
            None => verdict = Some(count),
            Some(previous) if previous == count => {}
            Some(_) => return PortCount::Unknown,
        }
    }
    verdict.unwrap_or(PortCount::Unknown)
}

fn classify(ident: &str, template_params: &[TemplateParam], ctor: &Ctor) -> PortCount {
    if let Some(i) = template_params.iter().position(|p| p.name == ident) {
        return PortCount::TemplateArg(i);
    }
    if let Some(i) = ctor.params.iter().position(|p| p.name == ident) {
        return PortCount::CtorArg(i);
    }
    let Some((_, init)) = ctor.inits.iter().find(|(member, _)| member == ident) else {
        return PortCount::Unknown;
    };
    if let Some(i) = ctor.params.iter().position(|p| p.name == *init) {
        return PortCount::CtorArg(i);
    }
    if let Some(source) = init.strip_suffix(".size()") {
        if let Some(i) = ctor.params.iter().position(|p| p.name == source.trim()) {
            return PortCount::CtorArgLen(i);
        }
    }
    PortCount::Unknown
}

fn sole_vector_param(ctor: &Ctor) -> Option<usize> {
    let mut found = None;
    for (i, param) in ctor.params.iter().enumerate() {
        if param.param_type.contains("vector<") {
            if found.is_some() {
                return None;
            }
            found = Some(i);
        }
    }
    found
}

fn placement_new_bound(ctor_body: Node, src: &str, port: &str) -> Option<String> {
    let placement = format!("new (&{}[", port);
    let compact = format!("new(&{}[", port);
    let mut found = None;
    for_statements(ctor_body, &mut |loop_node| {
        if found.is_some() {
            return;
        }
        let body = text(loop_node, src);
        if !body.contains(&placement) && !body.contains(&compact) {
            return;
        }
        let Some(condition) = loop_node.child_by_field_name("condition") else {
            return;
        };
        let condition = if condition.kind() == "expression_statement" {
            condition.named_child(0)
        } else {
            Some(condition)
        };
        let Some(condition) = condition else { return };
        if condition.kind() != "binary_expression" {
            return;
        }
        let Some(right) = condition.child_by_field_name("right") else {
            return;
        };
        if right.kind() == "identifier" || right.kind() == "field_expression" {
            found = Some(text(right, src).to_string());
        }
    });
    found
}

fn for_statements<'t>(node: Node<'t>, visit: &mut dyn FnMut(Node<'t>)) {
    if node.kind() == "for_statement" {
        visit(node);
    }
    let mut cursor = node.walk();
    for child in node.children(&mut cursor) {
        for_statements(child, visit);
    }
}

fn pack_count_ref(body_text: &str, pack: &str) -> Option<String> {
    let sizeof_pack = format!("sizeof...({})", pack);
    let at = body_text.find(&sizeof_pack)?;
    if let Some(ident) = comparand_after(body_text, at + sizeof_pack.len()) {
        return Some(ident);
    }
    if let Some(ident) = comparand_before(body_text, at) {
        return Some(ident);
    }
    let alias = assigned_name(body_text, at)?;
    comparand_of(body_text, &alias)
}

fn comparand_of(body_text: &str, name: &str) -> Option<String> {
    let mut from = 0;
    while let Some(rel) = body_text[from..].find(name) {
        let at = from + rel;
        let end = at + name.len();
        from = end;
        if !is_whole_word(body_text, at, end) {
            continue;
        }
        if let Some(ident) = comparand_after(body_text, end) {
            return Some(ident);
        }
        if let Some(ident) = comparand_before(body_text, at) {
            return Some(ident);
        }
    }
    None
}

fn comparand_after(hay: &str, at: usize) -> Option<String> {
    let rest = hay[at..].trim_start();
    let rest = rest
        .strip_prefix("==")
        .or_else(|| rest.strip_prefix("!="))?;
    let ident: String = rest
        .trim_start()
        .chars()
        .take_while(|c| c.is_alphanumeric() || *c == '_')
        .collect();
    (!ident.is_empty()).then_some(ident)
}

fn comparand_before(hay: &str, at: usize) -> Option<String> {
    let head = hay[..at].trim_end();
    let head = head
        .strip_suffix("==")
        .or_else(|| head.strip_suffix("!="))?;
    let ident: String = head
        .trim_end()
        .chars()
        .rev()
        .take_while(|c| c.is_alphanumeric() || *c == '_')
        .collect();
    (!ident.is_empty()).then(|| ident.chars().rev().collect())
}

fn assigned_name(hay: &str, at: usize) -> Option<String> {
    let head = hay[..at].trim_end();
    let head = head.strip_suffix('=')?;
    if head.ends_with(['=', '!', '<', '>']) {
        return None;
    }
    let ident: String = head
        .trim_end()
        .chars()
        .rev()
        .take_while(|c| c.is_alphanumeric() || *c == '_')
        .collect();
    (!ident.is_empty()).then(|| ident.chars().rev().collect())
}

pub(crate) fn is_whole_word(hay: &str, start: usize, end: usize) -> bool {
    let before = hay[..start].chars().next_back();
    let after = hay[end..].chars().next();
    let word = |c: Option<char>| c.is_some_and(|c| c.is_alphanumeric() || c == '_');
    !word(before) && !word(after)
}

struct VariadicOutputs {
    type_name: String,
    param: String,
}

fn variadic_procedure(body_code: &str) -> Option<VariadicOutputs> {
    let mut from = 0;
    while let Some(rel) = body_code[from..].find(PROCEDURE) {
        let open = from + rel + PROCEDURE.len();
        from = open;
        let head = body_code[..from - PROCEDURE.len()].trim_end();
        if head.ends_with("->") || head.ends_with('.') {
            continue;
        }
        let Some(close) = matching_paren(body_code, open) else {
            continue;
        };
        let args = &body_code[open..close];
        if !args.contains("...") {
            continue;
        }
        let mut identifiers = args
            .split(|c: char| !(c.is_alphanumeric() || c == '_'))
            .filter(|w| !w.is_empty());
        let type_name = identifiers.next()?.to_string();
        let param = identifiers.next_back()?.to_string();
        return Some(VariadicOutputs { type_name, param });
    }
    None
}

fn matching_paren(text: &str, after_open: usize) -> Option<usize> {
    let mut depth = 1usize;
    for (offset, c) in text[after_open..].char_indices() {
        match c {
            '(' => depth += 1,
            ')' => {
                depth -= 1;
                if depth == 0 {
                    return Some(after_open + offset);
                }
            }
            _ => {}
        }
    }
    None
}

enum Group {
    Scalar,
    Pointer,
    Array(String),
}

enum Member {
    Channel {
        name: String,
        element: String,
        group: Group,
    },
    Procedure {
        outputs: Vec<(String, String)>,
    },
    ChannelAccessor,
}

fn conditional_fields(body: Node, extent: usize) -> bool {
    let mut cursor = body.walk();
    let found = body
        .children(&mut cursor)
        .take_while(|child| child.start_byte() < extent)
        .filter(|child| child.kind().starts_with("preproc_"))
        .any(guards_a_field);
    found
}

fn guards_a_field(node: Node) -> bool {
    if node.kind() == "field_declaration" {
        return true;
    }
    let mut cursor = node.walk();
    let found = node.children(&mut cursor).any(guards_a_field);
    found
}

fn declares_is_gui(body: Node, src: &str, extent: usize) -> bool {
    let mut cursor = body.walk();
    let members: Vec<Node> = body
        .children(&mut cursor)
        .take_while(|child| child.start_byte() < extent)
        .collect();
    members
        .into_iter()
        .any(|child| child.kind() == "field_declaration" && text(child, src).contains("is_gui"))
}

fn declares_may_block(body: Node, src: &str, extent: usize) -> bool {
    let mut cursor = body.walk();
    let found = body
        .children(&mut cursor)
        .take_while(|child| child.start_byte() < extent)
        .any(|child| {
            child.kind() == "field_declaration"
                && declared_name(child, src) == Some(MAY_BLOCK)
                && initialized_true(child, src)
        });
    found
}

fn initialized_true(node: Node, src: &str) -> bool {
    if let Some(value) = node.child_by_field_name("default_value") {
        return text(value, src).trim() == "true";
    }
    match text(node, src).split_once('=') {
        Some((_, value)) => value.trim().trim_end_matches(';').trim() == "true",
        None => false,
    }
}

fn public_members(body: Node, src: &str, extent: usize, default_public: bool) -> Vec<Member> {
    let mut members = Vec::new();
    let mut public = default_public;
    let mut cursor = body.walk();
    for child in body.children(&mut cursor) {
        if child.start_byte() >= extent {
            break;
        }
        if child.kind() == "access_specifier" {
            public = text(child, src).trim() == "public";
            continue;
        }
        if !public {
            continue;
        }
        match declarator(child) {
            Some(function) => {
                if let Some(member) = method_member(child, function, src) {
                    members.push(member);
                }
            }
            None => {
                if child.kind() == "field_declaration" {
                    if let Some(member) = channel_member(child, src) {
                        members.push(member);
                    }
                }
            }
        }
    }
    members
}

fn method_member(node: Node, function: Node, src: &str) -> Option<Member> {
    let name = declared_name(function, src)?;
    let params = function.child_by_field_name("parameters")?;
    if name == "procedure" {
        let mut outputs = Vec::new();
        let mut cursor = params.walk();
        for param in params.named_children(&mut cursor) {
            let param_text = text(param, src);
            let Some(element) = template_argument(param_text, CHANNEL_BASE) else {
                continue;
            };
            let Some(port) = declared_name(param, src) else {
                continue;
            };
            outputs.push((port.to_string(), element.to_string()));
        }
        return Some(Member::Procedure { outputs });
    }
    let returns_channel = src[node.start_byte()..function.start_byte()].contains(CHANNEL_BASE);
    let mut cursor = params.walk();
    let no_params = params.named_children(&mut cursor).count() == 0;
    (returns_channel && no_params).then_some(Member::ChannelAccessor)
}

fn channel_member(node: Node, src: &str) -> Option<Member> {
    let mut cursor = node.walk();
    if node
        .children(&mut cursor)
        .any(|c| c.kind() == "storage_class_specifier")
    {
        return None;
    }
    let type_node = node.child_by_field_name("type")?;
    let element = template_argument(text(type_node, src), CHANNEL)?;
    let declarator = node.child_by_field_name("declarator")?;
    let group = match declarator.kind() {
        "pointer_declarator" => Group::Pointer,
        "array_declarator" => Group::Array(
            declarator
                .child_by_field_name("size")
                .map(|size| text(size, src).to_string())
                .unwrap_or_default(),
        ),
        _ => Group::Scalar,
    };
    let name = declared_name(node, src)?;
    Some(Member::Channel {
        name: name.to_string(),
        element: element.to_string(),
        group,
    })
}

fn template_argument<'a>(type_text: &'a str, template: &str) -> Option<&'a str> {
    let at = type_text.find(template)?;
    let head = &type_text[..at];
    if !head.is_empty() && !head.ends_with("::") {
        return None;
    }
    let inner = &type_text[at + template.len()..];
    let close = inner.rfind('>')?;
    Some(inner[..close].trim())
}

struct Ctor<'t> {
    params: Vec<CtorParam>,
    inits: Vec<(String, String)>,
    body: Option<Node<'t>>,
}

fn find_ctors<'t>(body: Node<'t>, struct_name: &str, src: &str) -> Vec<Ctor<'t>> {
    let mut out = Vec::new();
    let mut cursor = body.walk();
    for child in body.children(&mut cursor) {
        let Some(function) = declarator(child) else {
            continue;
        };
        if declared_name(function, src) != Some(struct_name) {
            continue;
        }
        if text(child, src).contains("= delete") {
            continue;
        }
        let params = function
            .child_by_field_name("parameters")
            .map(|p| ctor_params(p, src))
            .unwrap_or_default();
        if params.len() == 1 && params[0].param_type.contains(struct_name) {
            continue;
        }
        out.push(Ctor {
            params,
            inits: field_initializers(child, src),
            body: child_of_kind(child, "compound_statement"),
        });
    }
    out
}

fn ctor_params(params: Node, src: &str) -> Vec<CtorParam> {
    let mut out = Vec::new();
    let mut cursor = params.walk();
    for param in params.named_children(&mut cursor) {
        if !param.kind().ends_with("parameter_declaration") {
            continue;
        }
        let name = declared_name(param, src).unwrap_or("").to_string();
        let default = param
            .child_by_field_name("default_value")
            .map(|d| text(d, src).to_string());
        let param_type = param_type_text(param, src, &name);
        out.push(CtorParam {
            name,
            param_type,
            default,
        });
    }
    out
}

fn param_type_text(param: Node, src: &str, name: &str) -> String {
    let whole = text(param, src);
    let head = match whole.find('=') {
        Some(at) => &whole[..at],
        None => whole,
    };
    if name.is_empty() {
        return normalize(head);
    }
    match head.rfind(name) {
        Some(at) if is_whole_word(head, at, at + name.len()) => normalize(&head[..at]),
        _ => normalize(head),
    }
}

fn normalize(text: &str) -> String {
    text.split_whitespace().collect::<Vec<_>>().join(" ")
}

fn field_initializers(node: Node, src: &str) -> Vec<(String, String)> {
    let Some(list) = child_of_kind(node, "field_initializer_list") else {
        return Vec::new();
    };
    let mut out = Vec::new();
    let mut cursor = list.walk();
    for init in list.named_children(&mut cursor) {
        if init.kind() != "field_initializer" {
            continue;
        }
        let Some(member) = init.named_child(0) else {
            continue;
        };
        let Some(args) = child_of_kind(init, "argument_list") else {
            continue;
        };
        let Some(value) = args.named_child(0) else {
            continue;
        };
        out.push((
            text(member, src).to_string(),
            normalize(text(value, src)).replace(' ', ""),
        ));
    }
    out
}

fn struct_aliases(body: Node, src: &str) -> Vec<(String, String)> {
    let mut out = Vec::new();
    let mut cursor = body.walk();
    for child in body.children(&mut cursor) {
        if child.kind() != "alias_declaration" {
            continue;
        }
        let declaration = text(child, src);
        let Some(rest) = declaration.strip_prefix("using") else {
            continue;
        };
        let Some((name, value)) = rest.split_once('=') else {
            continue;
        };
        out.push((
            name.trim().to_string(),
            value.trim().trim_end_matches(';').trim().to_string(),
        ));
    }
    out
}

fn resolve(element: &str, aliases: &[(String, String)]) -> String {
    aliases
        .iter()
        .find(|(name, _)| name == element)
        .map(|(_, value)| value.clone())
        .unwrap_or_else(|| element.to_string())
}

fn unresolved(element_type: &str) -> bool {
    element_type.contains("typename")
}

fn enclosing_template_params(node: Node, src: &str) -> Vec<TemplateParam> {
    let mut current = node;
    loop {
        let Some(parent) = current.parent() else {
            return Vec::new();
        };
        match parent.kind() {
            "template_declaration" => {
                let Some(list) = child_of_kind(parent, "template_parameter_list") else {
                    return Vec::new();
                };
                let mut out = Vec::new();
                let mut cursor = list.walk();
                for param in list.named_children(&mut cursor) {
                    if let Some(parsed) = template_param(param, src) {
                        out.push(parsed);
                    }
                }
                return out;
            }
            _ if parent.start_byte() == node.start_byte() => current = parent,
            _ => return Vec::new(),
        }
    }
}

fn template_param(param: Node, src: &str) -> Option<TemplateParam> {
    let kind = param.kind();
    if !kind.ends_with("parameter_declaration") {
        return None;
    }
    let whole = text(param, src);
    let (head, default) = match whole.find('=') {
        Some(at) => (&whole[..at], Some(normalize(&whole[at + 1..]))),
        None => (whole, None),
    };
    let pack = head.contains("...");
    let head = head.replace("...", " ");
    let mut words = head.split_whitespace().collect::<Vec<_>>();
    let name = words.pop()?.to_string();
    let type_words = words.join(" ");
    let kind = if type_words == "typename" || type_words == "class" || type_words.is_empty() {
        TemplateParamKind::Type
    } else {
        TemplateParamKind::NonType {
            param_type: type_words,
        }
    };
    Some(TemplateParam {
        name,
        kind,
        default,
        pack,
    })
}

fn attach_synonyms(root: Node, src: &str, specs: &mut [BlockSpec]) {
    let mut stack = vec![root];
    while let Some(child) = stack.pop() {
        if child.kind() != "alias_declaration" {
            let mut cursor = child.walk();
            let children: Vec<Node> = child.children(&mut cursor).collect();
            stack.extend(children.into_iter().rev());
            continue;
        }
        let declaration = text(child, src);
        let Some(rest) = declaration.strip_prefix("using") else {
            continue;
        };
        let Some((name, value)) = rest.split_once('=') else {
            continue;
        };
        let value = value.trim().trim_end_matches(';').trim();
        let (base, template_args) = match value.find('<') {
            Some(open) => {
                let Some(close) = value.rfind('>') else {
                    continue;
                };
                (
                    last_segment(value[..open].trim()),
                    split_template_args(&value[open + 1..close]),
                )
            }
            None => (last_segment(value), Vec::new()),
        };
        let Some(spec) = specs.iter_mut().find(|s| s.name == base) else {
            continue;
        };
        let synonym = Synonym {
            name: name.trim().to_string(),
            template_args,
        };
        if !spec.synonyms.contains(&synonym) {
            spec.synonyms.push(synonym);
        }
    }
}

fn split_template_args(args: &str) -> Vec<String> {
    let mut out = Vec::new();
    let mut depth = 0usize;
    let mut current = String::new();
    for c in args.chars() {
        match c {
            '<' | '(' => {
                depth += 1;
                current.push(c);
            }
            '>' | ')' => {
                depth = depth.saturating_sub(1);
                current.push(c);
            }
            ',' if depth == 0 => {
                out.push(normalize(&current));
                current.clear();
            }
            _ => current.push(c),
        }
    }
    if !current.trim().is_empty() {
        out.push(normalize(&current));
    }
    out
}

fn struct_end(code: &str, open_brace: usize) -> usize {
    let bytes = code.as_bytes();
    let mut depth = 0usize;
    for (offset, byte) in bytes[open_brace..].iter().enumerate() {
        match byte {
            b'{' => depth += 1,
            b'}' => {
                depth -= 1;
                if depth == 0 {
                    return open_brace + offset + 1;
                }
            }
            _ => {}
        }
    }
    bytes.len()
}

fn blank_noncode(source: &str) -> String {
    let bytes = source.as_bytes();
    let mut out = bytes.to_vec();
    let mut i = 0;
    while i < bytes.len() {
        match bytes[i] {
            b'/' if bytes.get(i + 1) == Some(&b'/') => {
                while i < bytes.len() && bytes[i] != b'\n' {
                    out[i] = b' ';
                    i += 1;
                }
            }
            b'/' if bytes.get(i + 1) == Some(&b'*') => {
                let end = find_comment_end(bytes, i + 2);
                out[i..end].fill(b' ');
                i = end;
            }
            b'"' => i = blank_quoted(bytes, &mut out, i, b'"'),
            b'\'' if !preceded_by_word(bytes, i) => i = blank_quoted(bytes, &mut out, i, b'\''),
            _ => i += 1,
        }
    }
    String::from_utf8(out).unwrap_or_else(|_| source.to_string())
}

fn find_comment_end(bytes: &[u8], from: usize) -> usize {
    let mut i = from;
    while i + 1 < bytes.len() {
        if bytes[i] == b'*' && bytes[i + 1] == b'/' {
            return i + 2;
        }
        i += 1;
    }
    bytes.len()
}

fn blank_quoted(bytes: &[u8], out: &mut [u8], at: usize, quote: u8) -> usize {
    let end = skip_quoted(bytes, at, quote);
    out[at + 1..end.saturating_sub(1).max(at + 1)].fill(b' ');
    end
}

fn skip_quoted(bytes: &[u8], at: usize, quote: u8) -> usize {
    let mut i = at + 1;
    while i < bytes.len() && bytes[i] != quote {
        if bytes[i] == b'\\' {
            i += 1;
        }
        i += 1;
    }
    (i + 1).min(bytes.len())
}

fn preceded_by_word(bytes: &[u8], at: usize) -> bool {
    at > 0 && (bytes[at - 1].is_ascii_alphanumeric() || bytes[at - 1] == b'_')
}

fn declarator(node: Node) -> Option<Node> {
    let mut current = node.child_by_field_name("declarator")?;
    loop {
        match current.kind() {
            "function_declarator" => return Some(current),
            "reference_declarator" | "variadic_declarator" => {
                current = current.named_child(0)?;
            }
            "pointer_declarator" | "init_declarator" | "parenthesized_declarator" => {
                current = current.child_by_field_name("declarator")?;
            }
            _ => return None,
        }
    }
}

fn declared_name<'a>(node: Node, src: &'a str) -> Option<&'a str> {
    let mut current = node.child_by_field_name("declarator")?;
    loop {
        match current.kind() {
            "identifier" | "field_identifier" | "type_identifier" => {
                return Some(text(current, src))
            }
            "reference_declarator" | "variadic_declarator" => current = current.named_child(0)?,
            _ => current = current.child_by_field_name("declarator")?,
        }
    }
}

fn child_of_kind<'t>(node: Node<'t>, kind: &str) -> Option<Node<'t>> {
    let mut cursor = node.walk();
    let found = node.children(&mut cursor).find(|c| c.kind() == kind);
    found
}

fn last_segment(name: &str) -> &str {
    name.rsplit("::").next().unwrap_or(name).trim()
}

fn text<'a>(node: Node, src: &'a str) -> &'a str {
    &src[node.start_byte()..node.end_byte()]
}

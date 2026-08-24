use std::collections::HashMap;
use std::io::{BufRead, BufReader};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex, MutexGuard, PoisonError};
use std::thread;
use std::time::Duration;

use base64::engine::general_purpose::URL_SAFE_NO_PAD;
use base64::Engine;
use cler_graph::palette_types::Port;
use cler_graph::{BlockSpec, Command};
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};

use crate::build::Emit;
use crate::document::{self, DocumentState, Documents};

pub const TOOL_NAME: &str = "propose_commands";

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Wire {
    Anthropic,
    OpenAi,
    Responses,
}

#[derive(Debug, Clone, Copy)]
pub struct Provider {
    pub id: &'static str,
    pub label: &'static str,
    pub endpoint: &'static str,
    pub models_endpoint: &'static str,
    pub key_env: &'static str,
    pub key_file: &'static str,
    pub key_prefix: &'static str,
    pub default_model: &'static str,
    pub wire: Wire,
    // Brand the sign-in is offered under, empty when the provider has no oauth.
    pub signin: &'static str,
}

pub const ANTHROPIC: Provider = Provider {
    id: "anthropic",
    label: "the Anthropic API",
    endpoint: "https://api.anthropic.com/v1/messages",
    models_endpoint: "https://api.anthropic.com/v1/models?limit=100",
    key_env: "ANTHROPIC_API_KEY",
    key_file: "anthropic-key",
    key_prefix: "sk-ant-",
    default_model: "claude-opus-5",
    wire: Wire::Anthropic,
    signin: "Claude",
};

pub const OPENAI: Provider = Provider {
    id: "openai",
    label: "the OpenAI API",
    endpoint: "https://api.openai.com/v1/chat/completions",
    models_endpoint: "https://api.openai.com/v1/models",
    key_env: "OPENAI_API_KEY",
    key_file: "openai-key",
    key_prefix: "sk-",
    default_model: "gpt-5",
    wire: Wire::OpenAi,
    signin: "",
};

// No key file and no key env var: the browser sign-in is the only way in, so
// every key-shaped path below branches on the empty key_file rather than on
// what a lookup of the empty string would return.
pub const OPENAI_CODEX: Provider = Provider {
    id: "openai-codex",
    label: "ChatGPT",
    endpoint: "https://chatgpt.com/backend-api/codex/responses",
    models_endpoint: "https://chatgpt.com/backend-api/codex/models",
    key_env: "",
    key_file: "",
    key_prefix: "",
    default_model: "gpt-5.4",
    wire: Wire::Responses,
    signin: "ChatGPT",
};

pub const PROVIDERS: [Provider; 3] = [ANTHROPIC, OPENAI, OPENAI_CODEX];

const API_VERSION: &str = "2023-06-01";
const USER_AGENT: &str = concat!("cler/", env!("CARGO_PKG_VERSION"));
const EFFORT: &str = "xhigh";
const OPENAI_EFFORT: &str = "high";
const MAX_TOKENS: u32 = 16000;
const CONNECT_TIMEOUT: Duration = Duration::from_secs(20);

const DELTA_EVENT: &str = "ai-agent-delta";
const DONE_EVENT: &str = "ai-agent-done";
const PROPOSAL_EVENT: &str = "ai-agent-proposal";

const GUIDE: &str = include_str!("../../../../../AGENTS.md");
const GUIDE_SECTIONS: [&str; 4] = [
    "# AI Bringup Guide",
    "## Writing a Block",
    "## Channels & Buffer Access",
    "## Flowgraph & Schedulers",
];

const MODEL_BUDGET: usize = 16_000;
const SOURCE_BUDGET: usize = 32_000;
const PALETTE_BUDGET: usize = 8_000;
const BUDGET_CONTEXT: usize = 200_000;

const EXPLAIN: &str = "\
You are the cler flowgraph assistant, embedded in the cler flowgraph editor.

You explain; you do not act. You cannot edit the graph, run commands, or write \
files, and nothing you say is applied. When the user asks for a change, say \
exactly what you would do — which blocks, which wires, which parameters — and \
tell them this file is open read-only, so nothing can be applied to it.";

const ACT: &str = "\
You are the cler flowgraph assistant, embedded in the cler flowgraph editor.

You explain, and you propose changes. You never apply anything yourself: a \
proposal is checked by the editor's validator and shown to the user as a diff \
they accept or reject.

When the user asks a question, answer in text and call no tool. When the user \
asks for a change to the graph, say in a sentence what you are changing and \
call propose_commands once, with every command of that change in that one \
call — a call is applied atomically, as a single undo step, and any second \
tool call in the same turn is dropped unread.

Commands name blocks by the variable spelled in the graph model, and a site by \
its index there. Never invent a variable the model does not have; add_block is \
what introduces one. Every command in a call is checked against the graph as it \
stands now, so a block you add in this call cannot also be wired in it — \
propose the block, and wire it once the user has accepted. If what was asked \
cannot be expressed in these commands, say so plainly instead of proposing \
something near it.";

const GROUND: &str = "\
Answer from the material you are given: the parsed graph model, the source of \
the open file, the block palette, and the cler guide below. Say plainly when \
the answer is not in that material instead of guessing.

When a <selected_block> is given, that is the block the user is looking \
at — read \"this block\" and bare \"it\" as that one unless they name another.

Be concise. A sentence or two for a simple question; short paragraphs or a \
short list for a hard one. Cite a block by its display name and its variable, \
like Chirp (chirp), and an edge as source -> target.port. Use plain text, \
**bold**, `code` and - lists only.";

const TOOL_PURPOSE: &str = "\
Propose one atomic change to the open flowgraph. Every command the change \
needs goes in this single call: the editor validates them together, applies \
them together as one undo step, and refuses them together. Call this only when \
the user asks for a change — answer questions in text instead. The user sees \
the resulting diff and accepts or rejects it; nothing is applied by calling \
this tool. Every command is validated against the graph as it stands before \
the call, so a block introduced by add_block here cannot be wired by a connect \
in the same call.";

#[derive(Debug, Serialize)]
pub struct Status {
    pub available: bool,
    pub provider: String,
    pub model: String,
    pub reason: Option<String>,
    pub method: Option<String>,
}

#[derive(Debug, PartialEq, Eq)]
pub enum Auth {
    ApiKey(String),
    OAuth(String),
}

impl Auth {
    pub fn method(&self) -> &'static str {
        match self {
            Auth::ApiKey(_) => "api_key",
            Auth::OAuth(_) => "oauth",
        }
    }
}

#[derive(Debug, Deserialize)]
pub struct Turn {
    pub role: String,
    pub text: String,
}

#[derive(Debug, PartialEq, Eq)]
pub enum Chunk {
    Text(String),
    ToolStart(String),
    ToolJson(String),
    BlockEnd,
    Usage(u64, u64),
    Failed(String),
    Done,
}

#[derive(Debug, PartialEq, Serialize)]
pub struct Proposal {
    pub rationale: String,
    pub commands: Vec<Value>,
}

#[derive(Debug, Default, PartialEq)]
pub struct Reply {
    pub input: u64,
    pub output: u64,
    pub failure: Option<String>,
    pub proposal: Option<Proposal>,
    pub dropped: usize,
}

impl Reply {
    fn failed(message: String) -> Self {
        Reply {
            failure: Some(message),
            ..Reply::default()
        }
    }
}

#[derive(Default, Clone)]
pub struct Talks(Arc<Mutex<HashMap<String, Arc<AtomicBool>>>>);

pub fn provider() -> Provider {
    let chosen = crate::settings::current().ai_agent_provider;
    let wanted = chosen.as_deref().unwrap_or(ANTHROPIC.id).trim().to_string();
    PROVIDERS
        .into_iter()
        .find(|known| known.id == wanted)
        .unwrap_or(ANTHROPIC)
}

pub fn endpoint(provider: &Provider) -> String {
    // ChatGPT tokens are only accepted at chatgpt.com/backend-api; a leftover
    // api.openai.com base URL comes back as "Missing scopes: api.responses.write".
    if provider.wire == Wire::Responses {
        return provider.endpoint.to_string();
    }
    crate::settings::current()
        .ai_agent_base_url
        .filter(|url| !url.trim().is_empty())
        .unwrap_or_else(|| provider.endpoint.to_string())
}

/// The account id lives in the access token's claims, so it follows refresh
/// rotation with nothing stored. No signature check: it is our own token and the
/// value only fills a header.
pub fn account_of(access_token: &str) -> Option<String> {
    let claims = access_token.split('.').nth(1)?;
    let decoded = URL_SAFE_NO_PAD.decode(claims).ok()?;
    let value: Value = serde_json::from_slice(&decoded).ok()?;
    value
        .get("https://api.openai.com/auth")
        .and_then(|auth| auth.get("chatgpt_account_id"))
        .or_else(|| value.get("chatgpt_account_id"))
        .and_then(Value::as_str)
        .map(str::to_string)
}

pub fn model() -> String {
    crate::settings::current()
        .ai_agent_model
        .filter(|choice| !choice.trim().is_empty())
        .unwrap_or_else(|| provider().default_model.to_string())
}

pub fn key_path(config_dir: &Path) -> PathBuf {
    config_dir.join(provider().key_file)
}

pub fn store_key(key: &str, config_dir: &Path) -> Result<Status, String> {
    let provider = provider();
    if oauth_only(&provider) {
        return Err(format!(
            "{} signs in through the browser; there is no API key to store",
            provider.label
        ));
    }
    let trimmed = key.trim();
    if trimmed.is_empty() {
        return Err("the key is empty".to_string());
    }
    if !trimmed.starts_with(provider.key_prefix) {
        return Err(format!(
            "that does not look like a key for {} ({}…)",
            provider.label, provider.key_prefix
        ));
    }
    std::fs::create_dir_all(config_dir)
        .map_err(|cause| format!("cannot create {}: {cause}", config_dir.display()))?;
    let file = key_path(config_dir);
    std::fs::write(&file, trimmed)
        .map_err(|cause| format!("cannot write {}: {cause}", file.display()))?;
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        std::fs::set_permissions(&file, std::fs::Permissions::from_mode(0o600))
            .map_err(|cause| format!("cannot chmod {}: {cause}", file.display()))?;
    }
    Ok(status(config_dir))
}

pub fn locate(env_key: Option<String>, config_dir: &Path) -> Result<Auth, String> {
    let provider = provider();
    if oauth_only(&provider) {
        if !crate::oauth::signed_in(config_dir) {
            return Err(format!(
                "not signed in — sign in with {} to use it",
                provider.signin
            ));
        }
        return crate::oauth::access(config_dir).map(Auth::OAuth);
    }
    if let Some(key) = env_key.map(|text| text.trim().to_string()).filter(|text| !text.is_empty()) {
        return Ok(Auth::ApiKey(key));
    }
    let file = key_path(config_dir);
    let Ok(contents) = std::fs::read_to_string(&file) else {
        if !provider.signin.is_empty() && crate::oauth::signed_in(config_dir) {
            return crate::oauth::access(config_dir).map(Auth::OAuth);
        }
        return Err(missing(&file));
    };
    guard_permissions(&file)?;
    let key = contents.trim().to_string();
    if key.is_empty() {
        return Err(format!("{} is empty — put the API key in it", file.display()));
    }
    Ok(Auth::ApiKey(key))
}

fn oauth_only(provider: &Provider) -> bool {
    provider.key_file.is_empty()
}

fn env_key() -> Option<String> {
    let provider = provider();
    if provider.key_env.is_empty() {
        return None;
    }
    std::env::var(provider.key_env).ok()
}

pub fn status(config_dir: &Path) -> Status {
    match locate(env_key(), config_dir) {
        Ok(auth) => Status {
            available: true,
            provider: provider().id.to_string(),
            model: model(),
            reason: None,
            method: Some(auth.method().to_string()),
        },
        Err(reason) => Status {
            available: false,
            provider: provider().id.to_string(),
            model: model(),
            reason: Some(reason),
            method: None,
        },
    }
}

fn missing(file: &Path) -> String {
    let provider = provider();
    let signin = if provider.signin.is_empty() {
        String::new()
    } else {
        format!(", or sign in with {}", provider.signin)
    };
    format!(
        "no key for {} — export {} before starting the editor, write the key into {} and chmod 600 it{signin}",
        provider.label,
        provider.key_env,
        file.display()
    )
}

#[cfg(unix)]
fn guard_permissions(file: &Path) -> Result<(), String> {
    use std::os::unix::fs::PermissionsExt;
    let mode = std::fs::metadata(file)
        .map_err(|cause| format!("cannot read {}: {cause}", file.display()))?
        .permissions()
        .mode();
    if mode & 0o077 == 0 {
        return Ok(());
    }
    Err(format!(
        "{} is readable by other users (mode {:o}) — chmod 600 it before the editor will use it",
        file.display(),
        mode & 0o777
    ))
}

#[cfg(not(unix))]
fn guard_permissions(_file: &Path) -> Result<(), String> {
    Ok(())
}

pub fn guide() -> String {
    GUIDE_SECTIONS
        .iter()
        .filter_map(|head| section(GUIDE, head))
        .collect::<Vec<&str>>()
        .join("\n")
}

fn section<'a>(text: &'a str, head: &str) -> Option<&'a str> {
    let start = text.find(head)?;
    let rest = &text[start..];
    let end = rest[head.len()..]
        .find("\n## ")
        .map_or(rest.len(), |at| at + head.len());
    Some(&rest[..end])
}

pub fn clamp(text: &str, budget: usize) -> String {
    if text.len() <= budget {
        return text.to_string();
    }
    let head = boundary(text, budget / 2, false);
    let tail = boundary(text, text.len() - budget / 2, true);
    format!(
        "{}\n… {} characters elided …\n{}",
        &text[..head],
        tail - head,
        &text[tail..]
    )
}

fn boundary(text: &str, at: usize, forward: bool) -> usize {
    let mut at = at.min(text.len());
    while at > 0 && at < text.len() && !text.is_char_boundary(at) {
        at = if forward { at + 1 } else { at - 1 };
    }
    at
}

pub fn palette_list(specs: &[BlockSpec]) -> String {
    specs
        .iter()
        .map(|spec| {
            format!(
                "{} | in: {} | out: {}{}",
                spec.name,
                ports(spec.inputs()),
                ports(spec.outputs()),
                if spec.may_block { " | may_block" } else { "" }
            )
        })
        .collect::<Vec<String>>()
        .join("\n")
}

fn ports<'a>(listed: impl Iterator<Item = &'a Port>) -> String {
    let names: Vec<String> = listed
        .map(|port| format!("{}:{}", port.name, port.element_type))
        .collect();
    if names.is_empty() {
        "none".to_string()
    } else {
        names.join(", ")
    }
}

pub fn selection(context: String, selected: Option<String>) -> String {
    match selected.filter(|block| !block.trim().is_empty()) {
        Some(block) => format!("{context}\n\n<selected_block>{}</selected_block>", block.trim()),
        None => context,
    }
}

pub fn context(path: &str, model_json: &str, source: &str, specs: &[BlockSpec]) -> String {
    let room = budget_scale();
    format!(
        "<open_file>{path}</open_file>\n\n<graph_model>\n{}\n</graph_model>\n\n<source>\n{}\n</source>\n\n<palette>\n{}\n</palette>",
        clamp(model_json, scaled(MODEL_BUDGET, room)),
        clamp(source, scaled(SOURCE_BUDGET, room)),
        clamp(&palette_list(specs), scaled(PALETTE_BUDGET, room))
    )
}

/// The budgets below were sized for a 200k-token context. A model with less room
/// overflows on a file these numbers happily allow, so they follow the catalog when it
/// knows the model, and stay as written when it does not.
fn budget_scale() -> f64 {
    let provider = provider();
    match crate::catalog::context_of(provider.id, &model()) {
        Some(context) => context as f64 / BUDGET_CONTEXT as f64,
        None => 1.0,
    }
}

fn scaled(budget: usize, room: f64) -> usize {
    let wanted = (budget as f64 * room.clamp(0.1, 1.0)) as usize;
    wanted.max(budget / 8)
}

fn preamble(acting: bool) -> String {
    let role = if acting { ACT } else { EXPLAIN };
    format!("{role}\n\n{GROUND}\n\n<cler_guide>\n{}\n</cler_guide>", guide())
}

fn words(note: &str) -> Value {
    json!({ "type": "string", "description": note })
}

fn counting(note: &str) -> Value {
    json!({ "type": "integer", "minimum": 0, "description": note })
}

fn truth(note: &str) -> Value {
    json!({ "type": "boolean", "description": note })
}

fn listing(items: Value, note: &str) -> Value {
    json!({ "type": "array", "items": items, "description": note })
}

fn shape(required: &[&str], properties: Value) -> Value {
    json!({
        "type": "object",
        "additionalProperties": false,
        "required": required,
        "properties": properties
    })
}

fn site() -> (&'static str, Value, bool) {
    (
        "site",
        counting("index of the flowgraph site in the graph model, normally 0"),
        true,
    )
}

fn variant(command: &str, note: &str, fields: Vec<(&str, Value, bool)>) -> Value {
    let mut properties = serde_json::Map::new();
    properties.insert("command".to_string(), json!({ "const": command }));
    let mut required = vec![Value::from("command")];
    for (name, schema, needed) in fields {
        properties.insert(name.to_string(), schema);
        if needed {
            required.push(Value::from(name));
        }
    }
    json!({
        "type": "object",
        "description": note,
        "additionalProperties": false,
        "required": required,
        "properties": properties
    })
}

fn commands() -> Vec<Value> {
    let port = words("input field on the target block, such as in");
    let indexed = json!({
        "type": ["integer", "null"],
        "minimum": 0,
        "description": "element of an array-valued input port, or null for a plain field"
    });
    vec![
        variant(
            "set_param",
            "Change one constructor argument of an existing block.",
            vec![
                site(),
                ("block", words("variable name of the block"), true),
                (
                    "ctor_arg_index",
                    counting("position of the argument in the constructor call"),
                    true,
                ),
                ("new_text", words("replacement C++ expression"), true),
            ],
        ),
        variant(
            "set_template_arg",
            "Change one template argument of an existing block.",
            vec![
                site(),
                ("block", words("variable name of the block"), true),
                (
                    "template_arg_index",
                    counting("position of the template argument"),
                    true,
                ),
                ("new_text", words("replacement type or value"), true),
            ],
        ),
        variant(
            "set_display_name",
            "Rename the display name a block was constructed with.",
            vec![
                site(),
                ("block", words("variable name of the block"), true),
                ("new_text", words("new display name, without quotes"), true),
            ],
        ),
        variant(
            "set_config",
            "Change one field of the flowgraph's run configuration.",
            vec![
                site(),
                ("path", words("dotted path of the configuration field"), true),
                ("new_value", words("replacement C++ expression"), true),
            ],
        ),
        variant(
            "connect",
            "Wire one block's output into an input port of another.",
            vec![
                site(),
                ("from", words("variable name of the source block"), true),
                ("to", words("variable name of the target block"), true),
                ("port", port, true),
                ("port_index", indexed, false),
            ],
        ),
        variant(
            "disconnect",
            "Remove one wire, named by its index in the site's edge list.",
            vec![
                site(),
                ("edge", counting("index of the edge in the graph model"), true),
            ],
        ),
        variant(
            "add_block",
            "Declare a new block and leave it unwired. It cannot be connected until this proposal has been accepted.",
            vec![
                site(),
                ("type", words("block type, such as ThrottleBlock"), true),
                (
                    "template_args",
                    listing(json!({ "type": "string" }), "template arguments, in order"),
                    false,
                ),
                (
                    "ctor_args",
                    listing(
                        json!({ "type": "string" }),
                        "constructor arguments, in order, display name first",
                    ),
                    false,
                ),
                ("var_name", words("C++ variable name for the block"), true),
            ],
        ),
        variant(
            "remove_from_graph",
            "Unwire a block but keep its declaration.",
            vec![
                site(),
                ("block", words("variable name of the block"), true),
            ],
        ),
        variant(
            "add_to_graph",
            "Give a declared block a runner so it executes.",
            vec![
                site(),
                ("block", words("variable name of the block"), true),
            ],
        ),
        variant(
            "materialize_gui",
            "Create the gui window and render loop for a site that has none; blocks marked is_gui render automatically.",
            vec![site()],
        ),
        variant(
            "delete_block",
            "Unwire a block and delete its declaration.",
            vec![
                site(),
                ("block", words("variable name of the block"), true),
            ],
        ),
        variant(
            "define_block",
            "Add a new block struct to the file, with a stub body.",
            vec![
                site(),
                ("name", words("struct name, ending in Block"), true),
                ("value_type", words("element type the block carries"), true),
                (
                    "inputs",
                    listing(
                        shape(&["name"], json!({ "name": words("input channel name") })),
                        "input channels, in order",
                    ),
                    false,
                ),
                ("outputs", counting("number of output ports"), false),
                (
                    "params",
                    listing(
                        shape(
                            &["name", "cpp_type"],
                            json!({
                                "name": words("parameter name"),
                                "cpp_type": words("C++ type of the parameter"),
                                "default": {
                                    "type": ["string", "null"],
                                    "description": "default value, or null for none"
                                }
                            }),
                        ),
                        "constructor parameters beyond the display name",
                    ),
                    false,
                ),
                ("may_block", truth("whether procedure() can block"), false),
            ],
        ),
    ]
}

fn tool_schema() -> Value {
    json!({
        "type": "object",
        "additionalProperties": false,
        "required": ["rationale", "commands"],
        "properties": {
            "rationale": {
                "type": "string",
                "description": "One sentence, shown to the user above the diff, saying what this change does and why."
            },
            "commands": {
                "type": "array",
                "minItems": 1,
                "description": "Every command of the change, in the order they should be applied.",
                "items": { "anyOf": commands() }
            }
        }
    })
}

pub fn tool() -> Value {
    json!({
        "name": TOOL_NAME,
        "description": TOOL_PURPOSE,
        "input_schema": tool_schema()
    })
}

pub fn openai_tool() -> Value {
    json!({
        "type": "function",
        "function": {
            "name": TOOL_NAME,
            "description": TOOL_PURPOSE,
            "parameters": tool_schema()
        }
    })
}

pub const OAUTH_PREFACE: &str = "You are Claude Code, Anthropic's official CLI for Claude.";

pub fn request(context: &str, question: &str, history: &[Turn], acting: bool, oauth: bool) -> String {
    let mut messages: Vec<Value> = history
        .iter()
        .map(|turn| {
            let role = if turn.role == "assistant" {
                "assistant"
            } else {
                "user"
            };
            json!({ "role": role, "content": turn.text })
        })
        .collect();
    messages.push(json!({ "role": "user", "content": question }));
    match provider().wire {
        Wire::OpenAi => return openai_request(context, acting, messages),
        Wire::Responses => return responses_request(context, acting, messages),
        Wire::Anthropic => {}
    }
    let mut system = Vec::new();
    if oauth {
        system.push(json!({ "type": "text", "text": OAUTH_PREFACE }));
    }
    system.push(json!({ "type": "text", "text": preamble(acting), "cache_control": { "type": "ephemeral" } }));
    system.push(json!({ "type": "text", "text": context }));
    let mut body = json!({
        "model": model(),
        "max_tokens": MAX_TOKENS,
        "stream": true,
        "output_config": { "effort": EFFORT },
        "system": system,
        "messages": messages
    });
    if acting {
        body["tools"] = json!([tool()]);
    }
    body.to_string()
}

fn openai_request(context: &str, acting: bool, turns: Vec<Value>) -> String {
    let mut messages = vec![
        json!({ "role": "system", "content": preamble(acting) }),
        json!({ "role": "system", "content": context }),
    ];
    messages.extend(turns);
    let mut body = json!({
        "model": model(),
        "max_completion_tokens": MAX_TOKENS,
        "stream": true,
        "stream_options": { "include_usage": true },
        "reasoning_effort": OPENAI_EFFORT,
        "messages": messages
    });
    if acting {
        body["tools"] = json!([openai_tool()]);
    }
    body.to_string()
}

// The Responses API refuses store:true and an empty instructions string, and
// answers tool_choice without tools with a 400.
fn responses_request(context: &str, acting: bool, turns: Vec<Value>) -> String {
    let mut body = json!({
        "model": model(),
        "store": false,
        "stream": true,
        "instructions": format!("{}\n\n{context}", preamble(acting)),
        "input": turns,
        "parallel_tool_calls": true,
        "reasoning": { "effort": OPENAI_EFFORT }
    });
    if acting {
        body["tools"] = json!([responses_tool()]);
        body["tool_choice"] = json!("auto");
    }
    body.to_string()
}

fn responses_tool() -> Value {
    json!({
        "type": "function",
        "name": TOOL_NAME,
        "description": TOOL_PURPOSE,
        "parameters": tool_schema(),
        // tool_schema() puts anyOf inside commands.items, which strict mode rejects.
        "strict": false
    })
}

pub fn chunk(wire: Wire, line: &str) -> Option<Chunk> {
    match wire {
        Wire::Anthropic => anthropic_chunk(line),
        Wire::OpenAi => openai_chunk(line),
        Wire::Responses => responses_chunk(line),
    }
}

fn responses_chunk(line: &str) -> Option<Chunk> {
    let payload = line.strip_prefix("data:")?.trim();
    let value: Value = serde_json::from_str(payload).ok()?;
    match value.get("type").and_then(Value::as_str)? {
        "response.output_text.delta" => value
            .get("delta")
            .and_then(Value::as_str)
            .map(|text| Chunk::Text(text.to_string())),
        "response.output_item.added"
            if value.pointer("/item/type") == Some(&json!("function_call")) =>
        {
            Some(Chunk::ToolStart(
                value
                    .pointer("/item/name")
                    .and_then(Value::as_str)
                    .unwrap_or_default()
                    .to_string(),
            ))
        }
        "response.function_call_arguments.delta" => value
            .get("delta")
            .and_then(Value::as_str)
            .map(|piece| Chunk::ToolJson(piece.to_string())),
        // Not output_item.done: that also fires for reasoning and message items,
        // and a second BlockEnd would drop the proposal.
        "response.function_call_arguments.done" => Some(Chunk::BlockEnd),
        // Usage rides this terminal frame, and Chunk::Done would end gather()
        // before it is read; the stream ends at EOF instead.
        "response.completed" => Some(Chunk::Usage(
            counted(&value, "/response/usage/input_tokens"),
            counted(&value, "/response/usage/output_tokens"),
        )),
        // A refusal is its own event here, never output_text. The whole
        // sentence rides the .done frame, so the deltas are left alone: gather()
        // ends the turn on the first failure and would keep only a fragment.
        "response.refusal.done" => Some(Chunk::Failed(refusal(&value))),
        "response.failed" | "response.incomplete" => Some(Chunk::Failed(stalled(&value))),
        // This event is flat: no nested error object to read a type off.
        "error" => Some(Chunk::Failed(describe(Some(&json!({
            "type": value.get("code"),
            "message": value.get("message")
        }))))),
        _ => None,
    }
}

fn refusal(value: &Value) -> String {
    match value
        .get("refusal")
        .and_then(Value::as_str)
        .map(str::trim)
        .filter(|text| !text.is_empty())
    {
        Some(text) => format!("the model declined to answer that: {text}"),
        None => declined(&json!({})),
    }
}

fn stalled(value: &Value) -> String {
    let detail = value
        .pointer("/response/error")
        .filter(|error| !error.is_null())
        .or_else(|| value.pointer("/response/incomplete_details"))
        .and_then(|error| error.get("message").or_else(|| error.get("reason")))
        .and_then(Value::as_str)
        .filter(|text| !text.trim().is_empty())
        .unwrap_or("no detail given");
    format!("{} ended the turn early: {detail}", provider().label)
}

fn openai_chunk(line: &str) -> Option<Chunk> {
    let payload = line.strip_prefix("data:")?.trim();
    if payload == "[DONE]" {
        return Some(Chunk::Done);
    }
    let value: Value = serde_json::from_str(payload).ok()?;
    if value.get("error").is_some() {
        return Some(Chunk::Failed(describe(value.get("error"))));
    }
    let call = value.pointer("/choices/0/delta/tool_calls/0");
    if let Some(name) = call
        .and_then(|call| call.pointer("/function/name"))
        .and_then(Value::as_str)
    {
        return Some(Chunk::ToolStart(name.to_string()));
    }
    if let Some(piece) = call
        .and_then(|call| call.pointer("/function/arguments"))
        .and_then(Value::as_str)
    {
        return Some(Chunk::ToolJson(piece.to_string()));
    }
    if let Some(text) = value
        .pointer("/choices/0/delta/content")
        .and_then(Value::as_str)
        .filter(|text| !text.is_empty())
    {
        return Some(Chunk::Text(text.to_string()));
    }
    match value
        .pointer("/choices/0/finish_reason")
        .and_then(Value::as_str)
    {
        Some("tool_calls") => return Some(Chunk::BlockEnd),
        Some("content_filter") => {
            return Some(Chunk::Failed(declined(&json!({
                "stop_details": { "category": "content_filter" }
            }))))
        }
        _ => {}
    }
    value.get("usage").filter(|usage| !usage.is_null()).map(|usage| {
        Chunk::Usage(
            counted(usage, "/prompt_tokens"),
            counted(usage, "/completion_tokens"),
        )
    })
}

fn anthropic_chunk(line: &str) -> Option<Chunk> {
    let payload = line.strip_prefix("data:")?.trim();
    let value: Value = serde_json::from_str(payload).ok()?;
    match value.get("type").and_then(Value::as_str)? {
        "content_block_start" if value.pointer("/content_block/type") == Some(&json!("tool_use")) => {
            Some(Chunk::ToolStart(
                value
                    .pointer("/content_block/name")
                    .and_then(Value::as_str)
                    .unwrap_or_default()
                    .to_string(),
            ))
        }
        "content_block_delta" if value.pointer("/delta/partial_json").is_some() => value
            .pointer("/delta/partial_json")
            .and_then(Value::as_str)
            .map(|text| Chunk::ToolJson(text.to_string())),
        "content_block_delta" => value
            .pointer("/delta/text")
            .and_then(Value::as_str)
            .map(|text| Chunk::Text(text.to_string())),
        "content_block_stop" => Some(Chunk::BlockEnd),
        "message_start" => Some(Chunk::Usage(
            counted(&value, "/message/usage/input_tokens"),
            counted(&value, "/message/usage/output_tokens"),
        )),
        "message_delta" if stopped(&value) == Some("refusal") => Some(Chunk::Failed(declined(&value))),
        "message_delta" => Some(Chunk::Usage(
            counted(&value, "/usage/input_tokens"),
            counted(&value, "/usage/output_tokens"),
        )),
        "message_stop" => Some(Chunk::Done),
        "error" => Some(Chunk::Failed(describe(value.get("error")))),
        _ => None,
    }
}

fn stopped(value: &Value) -> Option<&str> {
    value.pointer("/delta/stop_reason").and_then(Value::as_str)
}

fn declined(value: &Value) -> String {
    let category = value
        .pointer("/delta/stop_details/category")
        .or_else(|| value.pointer("/stop_details/category"))
        .and_then(Value::as_str)
        .unwrap_or("policy");
    format!("the model declined to answer that ({category}) — ask about the flowgraph itself, or rephrase")
}

fn counted(value: &Value, pointer: &str) -> u64 {
    value.pointer(pointer).and_then(Value::as_u64).unwrap_or(0)
}

pub fn describe(error: Option<&Value>) -> String {
    let kind = error
        .and_then(|value| value.get("type"))
        .and_then(Value::as_str)
        .unwrap_or_default();
    let said = error
        .and_then(|value| value.get("message"))
        .and_then(Value::as_str)
        .filter(|text| !text.trim().is_empty());
    let detail = said.unwrap_or("no detail given");
    let provider = provider();
    let label = provider.label;
    let credential = if oauth_only(&provider) {
        "sign-in"
    } else {
        "API key"
    };
    match kind {
        "authentication_error" if oauth_only(&provider) => {
            format!("{label} rejected the sign-in — sign out and sign in again")
        }
        "authentication_error" => {
            format!(
                "{label} rejected the key — check {} or the key file",
                provider.key_env
            )
        }
        "permission_error" => format!("this {credential} is not allowed to use {}", model()),
        "not_found_error" => format!("{} is not available to this {credential}", model()),
        "rate_limit_error" => format!("rate limited by {label} — wait a moment and ask again"),
        "overloaded_error" => format!("{label} is overloaded right now — try again in a moment"),
        "request_too_large" => {
            "the request was too large for the API — this file may be too big to send".to_string()
        }
        "api_error" => format!("{label} hit an internal error — try again"),
        "invalid_request_error" => format!("{label} refused the request: {detail}"),
        // A typeless error still says something worth reading, when it says anything.
        "" if said.is_some() => format!("{label}: {detail}"),
        "" => format!("{label} returned an error with no type"),
        other => format!("{}: {detail}", other.replace('_', " ")),
    }
}

fn proposed(name: &str, body: &str) -> Result<Proposal, String> {
    if name != TOOL_NAME {
        return Err(format!(
            "the model called a tool this editor does not have ({name}) — ask again"
        ));
    }
    let call: Value = serde_json::from_str(body)
        .map_err(|_| "the model's proposal was not valid JSON — ask again".to_string())?;
    let rationale = call
        .get("rationale")
        .and_then(Value::as_str)
        .filter(|text| !text.trim().is_empty())
        .ok_or_else(|| "the model proposed a change with no rationale — ask again".to_string())?;
    let commands = call
        .get("commands")
        .and_then(Value::as_array)
        .filter(|listed| !listed.is_empty())
        .ok_or_else(|| "the model proposed a change with no commands — ask again".to_string())?;
    serde_json::from_value::<Vec<Command>>(Value::Array(commands.clone())).map_err(|cause| {
        format!("the model proposed a command this editor cannot run: {cause}")
    })?;
    Ok(Proposal {
        rationale: rationale.trim().to_string(),
        commands: commands.clone(),
    })
}

pub fn gather(
    wire: Wire,
    lines: impl Iterator<Item = String>,
    mut spoken: impl FnMut(&str),
) -> Reply {
    let mut reply = Reply::default();
    let mut open: Option<(String, String)> = None;
    for line in lines {
        match chunk(wire, &line) {
            Some(Chunk::Text(text)) => spoken(&text),
            Some(Chunk::ToolStart(name)) => open = Some((name, String::new())),
            Some(Chunk::ToolJson(piece)) => {
                if let Some((_, body)) = open.as_mut() {
                    body.push_str(&piece);
                }
            }
            Some(Chunk::BlockEnd) => {
                let Some((name, body)) = open.take() else {
                    continue;
                };
                if reply.proposal.is_some() || reply.failure.is_some() {
                    reply.dropped += 1;
                    continue;
                }
                match proposed(&name, &body) {
                    Ok(found) => reply.proposal = Some(found),
                    Err(message) => reply.failure = Some(message),
                }
            }
            Some(Chunk::Usage(seen, written)) => {
                reply.input = reply.input.max(seen);
                reply.output = reply.output.max(written);
            }
            Some(Chunk::Failed(message)) => {
                reply.failure = Some(message);
                return reply;
            }
            Some(Chunk::Done) => break,
            None => {}
        }
    }
    reply
}

/// What the provider says it serves today, merged with the vendored catalog. A failure
/// here is not worth an error in the panel — the catalog alone is a usable list, so the
/// picker keeps working offline, on a proxy, or behind an expired key.
pub fn listed_models(config_dir: &Path) -> Vec<crate::catalog::Listed> {
    let provider = provider();
    let live = locate(std::env::var(provider.key_env).ok(), config_dir)
        .ok()
        .and_then(|auth| fetch_models(&provider, &auth))
        .unwrap_or_default();
    crate::catalog::merge(provider.id, live)
}

fn fetch_models(provider: &Provider, auth: &Auth) -> Option<Vec<String>> {
    let client = reqwest::blocking::Client::builder()
        .timeout(CONNECT_TIMEOUT)
        .build()
        .ok()?;
    let asked = client.get(provider.models_endpoint);
    let asked = match (provider.wire, auth) {
        (Wire::Anthropic, Auth::ApiKey(key)) => asked
            .header("anthropic-version", API_VERSION)
            .header("x-api-key", key),
        (Wire::OpenAi, Auth::ApiKey(key)) => asked.header("authorization", format!("Bearer {key}")),
        (Wire::Responses, Auth::ApiKey(_)) => return None,
        (Wire::Responses, Auth::OAuth(token)) => asked
            .header("authorization", format!("Bearer {token}"))
            .header("chatgpt-account-id", account_of(token)?)
            .header("originator", crate::oauth::ORIGINATOR)
            .header("user-agent", USER_AGENT),
        (_, Auth::OAuth(token)) => asked
            .header("anthropic-version", API_VERSION)
            .header("authorization", format!("Bearer {token}"))
            .header("anthropic-beta", "claude-code-20250219,oauth-2025-04-20")
            .header("user-agent", "claude-cli/2.0.0"),
    };
    let answered = asked.send().ok()?;
    if !answered.status().is_success() {
        return None;
    }
    let body: Value = serde_json::from_str(&answered.text().ok()?).ok()?;
    let listed: Vec<String> = body
        .get("data")?
        .as_array()?
        .iter()
        .filter_map(|entry| entry.get("id").and_then(Value::as_str))
        .map(str::to_string)
        .collect();
    (!listed.is_empty()).then_some(listed)
}

pub fn stop(talks: &Talks, path: &str) {
    if let Some(flag) = held(talks).get(path) {
        flag.store(true, Ordering::Relaxed);
    }
}

pub fn ask(
    talks: &Talks,
    docs: &Documents,
    config_dir: &Path,
    path: &str,
    question: &str,
    history: Vec<Turn>,
    selected: Option<String>,
    emit: Emit,
) -> Result<(), String> {
    if question.trim().is_empty() {
        return Err("ask a question first".to_string());
    }
    let auth = locate(env_key(), config_dir)?;
    let state = document::open(docs, path)?;
    let specs = document::palette(docs, path).unwrap_or_default();
    let model_json = serde_json::to_string(&state.model).map_err(|cause| cause.to_string())?;
    let body = request(
        &selection(context(path, &model_json, &state.source, &specs), selected),
        question,
        &history,
        actionable(&state),
        matches!(auth, Auth::OAuth(_)),
    );

    let flag = arm(talks, path);
    let path = path.to_string();
    thread::spawn(move || {
        let reply = stream(&auth, &body, &emit, &path, &flag);
        emit(
            DONE_EVENT,
            json!({
                "path": path,
                "usage": { "input_tokens": reply.input, "output_tokens": reply.output },
                "error": reply.failure
            }),
        );
        if let Some(proposal) = reply.proposal {
            emit(
                PROPOSAL_EVENT,
                json!({
                    "path": path,
                    "rationale": proposal.rationale,
                    "commands": proposal.commands,
                    "dropped": reply.dropped
                }),
            );
        }
    });
    Ok(())
}

pub fn actionable(state: &DocumentState) -> bool {
    !state.model.has_errors && state.model.model.sites.iter().any(|site| site.editable)
}

fn stream(auth: &Auth, body: &str, emit: &Emit, path: &str, flag: &AtomicBool) -> Reply {
    let client = match reqwest::blocking::Client::builder()
        .timeout(None)
        .connect_timeout(CONNECT_TIMEOUT)
        .build()
    {
        Ok(client) => client,
        Err(cause) => return Reply::failed(format!("cannot start an HTTPS client: {cause}")),
    };
    let provider = provider();
    let prepared = client
        .post(endpoint(&provider))
        .header("content-type", "application/json");
    let prepared = match (provider.wire, auth) {
        (Wire::Anthropic, Auth::ApiKey(key)) => prepared
            .header("anthropic-version", API_VERSION)
            .header("x-api-key", key),
        (Wire::OpenAi, Auth::ApiKey(key)) => {
            prepared.header("authorization", format!("Bearer {key}"))
        }
        (Wire::Responses, Auth::ApiKey(_)) => {
            return Reply::failed(
                "signing in with ChatGPT is the only way to use this provider".to_string(),
            )
        }
        (Wire::Responses, Auth::OAuth(token)) => {
            let Some(account) = account_of(token) else {
                return Reply::failed(
                    "this ChatGPT sign-in carries no account — sign out and sign in again"
                        .to_string(),
                );
            };
            prepared
                .header("authorization", format!("Bearer {token}"))
                .header("chatgpt-account-id", account)
                .header("originator", crate::oauth::ORIGINATOR)
                .header("user-agent", USER_AGENT)
                .header("OpenAI-Beta", "responses=experimental")
                .header("accept", "text/event-stream")
        }
        (_, Auth::OAuth(token)) => prepared
            .header("anthropic-version", API_VERSION)
            .header("authorization", format!("Bearer {token}"))
            .header("anthropic-beta", "claude-code-20250219,oauth-2025-04-20")
            .header("user-agent", "claude-cli/2.0.0"),
    };
    let sent = prepared.body(body.to_string()).send();
    let response = match sent {
        Ok(response) => response,
        Err(cause) => return Reply::failed(unreachable(&cause)),
    };
    if !response.status().is_success() {
        let status = response.status().as_u16();
        let text = response.text().unwrap_or_default();
        return Reply::failed(refused(status, &text));
    }

    let lines = BufReader::new(response)
        .lines()
        .map_while(Result::ok)
        .take_while(|_| !flag.load(Ordering::Relaxed));
    gather(provider.wire, lines, |text| {
        emit(DELTA_EVENT, json!({ "path": path, "text": text }));
    })
}

fn unreachable(cause: &reqwest::Error) -> String {
    let label = provider().label;
    if cause.is_timeout() {
        return format!("{label} did not answer in time");
    }
    format!("cannot reach {label}: {cause}")
}

fn refused(status: u16, text: &str) -> String {
    // A stale ChatGPT token answers 401 with whatever body it likes, and none of
    // its error types are the Anthropic ones describe() knows.
    if status == 401 && oauth_only(&provider()) {
        return format!(
            "{} rejected the sign-in — sign out and sign in again",
            provider().label
        );
    }
    match serde_json::from_str::<Value>(text) {
        Ok(value) if value.get("error").is_some() => describe(value.get("error")),
        _ => format!("{} answered HTTP {status}", provider().label),
    }
}

fn held(talks: &Talks) -> MutexGuard<'_, HashMap<String, Arc<AtomicBool>>> {
    talks.0.lock().unwrap_or_else(PoisonError::into_inner)
}

fn arm(talks: &Talks, path: &str) -> Arc<AtomicBool> {
    let flag = Arc::new(AtomicBool::new(false));
    held(talks).insert(path.to_string(), flag.clone());
    flag
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::settings::{self, AppSettings};

    fn temp(tag: &str) -> PathBuf {
        let dir = std::env::temp_dir().join(format!("cler-provider-{tag}-{}", std::process::id()));
        std::fs::remove_dir_all(&dir).ok();
        std::fs::create_dir_all(&dir).expect("temp dir");
        dir
    }

    fn choose(dir: &Path, id: Option<&str>) {
        settings::store(
            dir,
            &AppSettings {
                ai_agent_provider: id.map(str::to_string),
                ..AppSettings::default()
            },
        )
        .expect("store settings");
    }

    #[test]
    fn the_provider_defaults_to_anthropic_and_follows_the_setting() {
        let _guard = settings::test_guard();
        let dir = temp("choice");
        choose(&dir, None);
        assert_eq!(provider().id, ANTHROPIC.id);
        assert_eq!(model(), ANTHROPIC.default_model);

        choose(&dir, Some("openai"));
        assert_eq!(provider().id, OPENAI.id);
        assert_eq!(provider().wire, Wire::OpenAi);
        assert_eq!(model(), OPENAI.default_model);
        assert_eq!(key_path(&dir), dir.join(OPENAI.key_file));

        choose(&dir, Some("openai-codex"));
        assert_eq!(provider().id, OPENAI_CODEX.id);
        assert_eq!(provider().wire, Wire::Responses);
        assert_eq!(model(), OPENAI_CODEX.default_model);

        assert!(settings::store(
            &dir,
            &AppSettings {
                ai_agent_provider: Some("mistral".to_string()),
                ..AppSettings::default()
            }
        )
        .is_err());
        choose(&dir, None);
    }

    #[test]
    fn the_openai_wire_sends_its_own_body_shape_and_never_the_claude_code_preface() {
        let _guard = settings::test_guard();
        let dir = temp("openai-body");
        choose(&dir, Some("openai"));
        let body: Value =
            serde_json::from_str(&request("<graph/>", "add a gain", &[], true, true)).expect("json");
        choose(&dir, None);

        assert_eq!(body["model"], OPENAI.default_model);
        assert!(body.get("system").is_none());
        assert!(body.get("output_config").is_none());
        assert_eq!(body["max_completion_tokens"], MAX_TOKENS);
        assert_eq!(body["stream_options"]["include_usage"], true);
        assert_eq!(body["reasoning_effort"], OPENAI_EFFORT);
        assert_eq!(body["messages"][0]["role"], "system");
        assert_eq!(body["messages"][2]["content"], "add a gain");
        assert_eq!(body["tools"][0]["type"], "function");
        assert_eq!(body["tools"][0]["function"]["name"], TOOL_NAME);
        assert!(body["tools"][0]["function"]["parameters"].is_object());
        assert!(!body.to_string().contains(OAUTH_PREFACE));
    }

    #[test]
    fn an_openai_stream_yields_the_same_text_proposal_and_usage() {
        let _guard = settings::test_guard();
        let dir = temp("openai-stream");
        choose(&dir, Some("openai"));
        let transcript = [
            r#"data: {"choices":[{"delta":{"content":"Renaming it."},"finish_reason":null}]}"#,
            r#"data: {"choices":[{"delta":{"tool_calls":[{"index":0,"id":"call_1","function":{"name":"propose_commands","arguments":""}}]}}]}"#,
            r#"data: {"choices":[{"delta":{"tool_calls":[{"index":0,"function":{"arguments":"{\"rationale\":\"rename\",\"commands\":[{\"command\":\"set_display_name\","}}]}}]}"#,
            r#"data: {"choices":[{"delta":{"tool_calls":[{"index":0,"function":{"arguments":"\"site\":0,\"block\":\"source1\",\"new_text\":\"Chirp\"}]}"}}]}}]}"#,
            r#"data: {"choices":[{"delta":{},"finish_reason":"tool_calls"}]}"#,
            r#"data: {"choices":[],"usage":{"prompt_tokens":4211,"completion_tokens":98}}"#,
            "data: [DONE]",
        ];
        let mut said = String::new();
        let reply = gather(
            Wire::OpenAi,
            transcript.iter().map(|line| line.to_string()),
            |text| said.push_str(text),
        );
        choose(&dir, None);

        assert_eq!(said, "Renaming it.");
        assert_eq!(reply.failure, None);
        assert_eq!((reply.input, reply.output), (4211, 98));
        assert_eq!(
            reply.proposal,
            Some(Proposal {
                rationale: "rename".to_string(),
                commands: vec![json!({
                    "command": "set_display_name",
                    "site": 0,
                    "block": "source1",
                    "new_text": "Chirp"
                })]
            })
        );
    }

    #[test]
    fn the_responses_wire_sends_a_flat_tool_and_none_of_the_chat_body() {
        let _guard = settings::test_guard();
        let turns = vec![json!({ "role": "user", "content": "add a gain" })];
        let body: Value = serde_json::from_str(&responses_request("<graph/>", true, turns.clone()))
            .expect("json");

        assert_eq!(body["store"], false);
        assert!(body["instructions"]
            .as_str()
            .is_some_and(|text| !text.trim().is_empty()));
        assert!(body["instructions"]
            .as_str()
            .is_some_and(|text| text.contains("<graph/>")));
        assert!(body.get("system").is_none());
        assert!(body.get("messages").is_none());
        assert!(body.get("max_completion_tokens").is_none());
        assert_eq!(body["input"][0]["content"], "add a gain");
        assert_eq!(body["parallel_tool_calls"], true);
        assert_eq!(body["reasoning"]["effort"], OPENAI_EFFORT);
        assert_eq!(body["tools"][0]["name"], TOOL_NAME);
        assert_eq!(body["tools"][0]["strict"], false);
        assert!(body["tools"][0].get("function").is_none());
        assert_eq!(body["tool_choice"], "auto");

        let read_only: Value =
            serde_json::from_str(&responses_request("<graph/>", false, turns)).expect("json");
        assert!(read_only.get("tools").is_none());
        assert!(read_only.get("tool_choice").is_none());
    }

    #[test]
    fn a_responses_stream_yields_the_same_text_proposal_and_usage() {
        let transcript = [
            r#"data: {"type":"response.output_text.delta","delta":"Renaming it."}"#,
            r#"data: {"type":"response.output_item.added","item":{"type":"reasoning","id":"rs_1"}}"#,
            r#"data: {"type":"response.output_item.added","item":{"type":"function_call","name":"propose_commands","call_id":"call_1"}}"#,
            r#"data: {"type":"response.function_call_arguments.delta","delta":"{\"rationale\":\"rename\",\"commands\":[{\"command\":\"set_display_name\","}"#,
            r#"data: {"type":"response.function_call_arguments.delta","delta":"\"site\":0,\"block\":\"source1\",\"new_text\":\"Chirp\"}]}"}"#,
            r#"data: {"type":"response.function_call_arguments.done","arguments":"{}"}"#,
            r#"data: {"type":"response.output_item.done","item":{"type":"function_call","name":"propose_commands"}}"#,
            r#"data: {"type":"response.completed","response":{"usage":{"input_tokens":4211,"output_tokens":98}}}"#,
        ];
        let mut said = String::new();
        let reply = gather(
            Wire::Responses,
            transcript.iter().map(|line| line.to_string()),
            |text| said.push_str(text),
        );

        assert_eq!(said, "Renaming it.");
        assert_eq!(reply.failure, None);
        assert_eq!(reply.dropped, 0);
        assert_eq!((reply.input, reply.output), (4211, 98));
        assert_eq!(
            reply.proposal,
            Some(Proposal {
                rationale: "rename".to_string(),
                commands: vec![json!({
                    "command": "set_display_name",
                    "site": 0,
                    "block": "source1",
                    "new_text": "Chirp"
                })]
            })
        );
    }

    #[test]
    fn the_selected_block_rides_along_only_when_there_is_one() {
        let plain = "<graph/>".to_string();
        assert_eq!(selection(plain.clone(), None), plain);
        assert_eq!(selection(plain.clone(), Some("  ".to_string())), plain);
        assert_eq!(
            selection(plain, Some(" gain ".to_string())),
            "<graph/>\n\n<selected_block>gain</selected_block>"
        );
    }

    #[test]
    fn chatgpt_has_no_key_file_no_env_var_and_no_key_sentences() {
        let _guard = settings::test_guard();
        let dir = temp("codex-keyless");
        choose(&dir, Some("openai-codex"));
        let refusal = locate(None, &dir).expect_err("not signed in");
        let stored = store_key("sk-anything", &dir).expect_err("nowhere to store a key");
        let rejected: Value =
            serde_json::from_str(r#"{"type":"authentication_error","message":"nope"}"#)
                .expect("json");
        let sentence = describe(Some(&rejected));
        let expired = refused(401, r#"{"detail":"missing token"}"#);
        choose(&dir, None);

        assert!(refusal.contains("ChatGPT"), "{refusal}");
        assert!(!refusal.contains("  "), "{refusal}");
        assert!(!refusal.contains("export"), "{refusal}");
        assert!(
            !refusal.contains(&dir.display().to_string()),
            "no key path: {refusal}"
        );
        assert!(stored.contains("no API key to store"), "{stored}");
        assert!(sentence.contains("sign in again"), "{sentence}");
        assert!(!sentence.contains("  "), "{sentence}");
        assert!(expired.contains("sign in again"), "{expired}");
    }

    #[test]
    fn a_claude_sign_in_is_never_used_for_another_provider() {
        let _guard = settings::test_guard();
        let dir = temp("oauth-gate");
        std::fs::write(dir.join(crate::oauth::ANTHROPIC_FLOW.token_file), "{}")
            .expect("fake token file");

        choose(&dir, Some("openai"));
        let refusal = locate(None, &dir).expect_err("no openai key");
        assert!(refusal.contains(OPENAI.key_env), "{refusal}");
        assert!(refusal.contains(OPENAI.key_file), "{refusal}");
        assert!(!refusal.contains("sign in"), "{refusal}");

        choose(&dir, Some("openai-codex"));
        let claude_only = locate(None, &dir).expect_err("a Claude token is not a ChatGPT sign-in");
        assert!(claude_only.contains("not signed in"), "{claude_only}");
        assert!(claude_only.contains("ChatGPT"), "{claude_only}");

        std::fs::write(dir.join(crate::oauth::CODEX_FLOW.token_file), "{}")
            .expect("fake token file");
        choose(&dir, Some("openai"));
        let codex_only = locate(None, &dir).expect_err("a ChatGPT token is not an openai key");
        assert!(codex_only.contains(OPENAI.key_env), "{codex_only}");
        assert!(!codex_only.contains("sign in"), "{codex_only}");

        choose(&dir, None);
        let anthropic = locate(None, &dir).expect_err("token file is not a real token");
        assert!(!anthropic.contains(ANTHROPIC.key_file), "{anthropic}");
    }
}

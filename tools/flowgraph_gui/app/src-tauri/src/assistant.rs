use std::collections::HashMap;
use std::io::{BufRead, BufReader};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex, MutexGuard, PoisonError};
use std::thread;
use std::time::Duration;

use cler_graph::palette_types::Port;
use cler_graph::{BlockSpec, Command};
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};

use crate::build::Emit;
use crate::document::{self, DocumentState, Documents};

pub const MODEL: &str = "claude-opus-5";
pub const KEY_ENV: &str = "ANTHROPIC_API_KEY";
pub const KEY_FILE: &str = "anthropic-key";
pub const TOOL_NAME: &str = "propose_commands";

const ENDPOINT: &str = "https://api.anthropic.com/v1/messages";
const API_VERSION: &str = "2023-06-01";
const EFFORT: &str = "xhigh";
const MAX_TOKENS: u32 = 16000;
const CONNECT_TIMEOUT: Duration = Duration::from_secs(20);

const DELTA_EVENT: &str = "assistant-delta";
const DONE_EVENT: &str = "assistant-done";
const PROPOSAL_EVENT: &str = "assistant-proposal";

const GUIDE: &str = include_str!("../../../../../AGENTS.md");
const GUIDE_SECTIONS: [&str; 4] = ["## 1. ", "## 4. ", "## 5. ", "## 6. "];

const MODEL_BUDGET: usize = 16_000;
const SOURCE_BUDGET: usize = 32_000;
const PALETTE_BUDGET: usize = 8_000;

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
    pub model: String,
    pub reason: Option<String>,
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

pub fn key_path(config_dir: &Path) -> PathBuf {
    config_dir.join(KEY_FILE)
}

pub fn locate(env_key: Option<String>, config_dir: &Path) -> Result<String, String> {
    if let Some(key) = env_key.map(|text| text.trim().to_string()).filter(|text| !text.is_empty()) {
        return Ok(key);
    }
    let file = key_path(config_dir);
    let Ok(contents) = std::fs::read_to_string(&file) else {
        return Err(missing(&file));
    };
    guard_permissions(&file)?;
    let key = contents.trim().to_string();
    if key.is_empty() {
        return Err(format!("{} is empty — put the API key in it", file.display()));
    }
    Ok(key)
}

pub fn status(config_dir: &Path) -> Status {
    match locate(std::env::var(KEY_ENV).ok(), config_dir) {
        Ok(_) => Status {
            available: true,
            model: MODEL.to_string(),
            reason: None,
        },
        Err(reason) => Status {
            available: false,
            model: MODEL.to_string(),
            reason: Some(reason),
        },
    }
}

fn missing(file: &Path) -> String {
    format!(
        "no Anthropic API key — export {KEY_ENV} before starting the editor, or write the key into {} and chmod 600 it",
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

pub fn context(path: &str, model_json: &str, source: &str, specs: &[BlockSpec]) -> String {
    format!(
        "<open_file>{path}</open_file>\n\n<graph_model>\n{}\n</graph_model>\n\n<source>\n{}\n</source>\n\n<palette>\n{}\n</palette>",
        clamp(model_json, MODEL_BUDGET),
        clamp(source, SOURCE_BUDGET),
        clamp(&palette_list(specs), PALETTE_BUDGET)
    )
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

pub fn tool() -> Value {
    json!({
        "name": TOOL_NAME,
        "description": TOOL_PURPOSE,
        "input_schema": {
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
        }
    })
}

pub fn request(context: &str, question: &str, history: &[Turn], acting: bool) -> String {
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
    let mut body = json!({
        "model": MODEL,
        "max_tokens": MAX_TOKENS,
        "stream": true,
        "output_config": { "effort": EFFORT },
        "system": [
            { "type": "text", "text": preamble(acting), "cache_control": { "type": "ephemeral" } },
            { "type": "text", "text": context }
        ],
        "messages": messages
    });
    if acting {
        body["tools"] = json!([tool()]);
    }
    body.to_string()
}

pub fn chunk(line: &str) -> Option<Chunk> {
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
    let detail = error
        .and_then(|value| value.get("message"))
        .and_then(Value::as_str)
        .filter(|text| !text.trim().is_empty())
        .unwrap_or("no detail given");
    match kind {
        "authentication_error" => {
            format!("the Anthropic API rejected the key — check {KEY_ENV} or the key file")
        }
        "permission_error" => format!("this API key is not allowed to use {MODEL}"),
        "not_found_error" => format!("{MODEL} is not available to this API key"),
        "rate_limit_error" => {
            "rate limited by the Anthropic API — wait a moment and ask again".to_string()
        }
        "overloaded_error" => {
            "the Anthropic API is overloaded right now — try again in a moment".to_string()
        }
        "request_too_large" => {
            "the request was too large for the API — this file may be too big to send".to_string()
        }
        "api_error" => "the Anthropic API hit an internal error — try again".to_string(),
        "invalid_request_error" => format!("the Anthropic API refused the request: {detail}"),
        "" => "the Anthropic API returned an error with no type".to_string(),
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

pub fn gather(lines: impl Iterator<Item = String>, mut spoken: impl FnMut(&str)) -> Reply {
    let mut reply = Reply::default();
    let mut open: Option<(String, String)> = None;
    for line in lines {
        match chunk(&line) {
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
    emit: Emit,
) -> Result<(), String> {
    if question.trim().is_empty() {
        return Err("ask a question first".to_string());
    }
    let key = locate(std::env::var(KEY_ENV).ok(), config_dir)?;
    let state = document::open(docs, path)?;
    let specs = document::palette(docs, path).unwrap_or_default();
    let model_json = serde_json::to_string(&state.model).map_err(|cause| cause.to_string())?;
    let body = request(
        &context(path, &model_json, &state.source, &specs),
        question,
        &history,
        actionable(&state),
    );

    let flag = arm(talks, path);
    let path = path.to_string();
    thread::spawn(move || {
        let reply = stream(&key, &body, &emit, &path, &flag);
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

fn stream(key: &str, body: &str, emit: &Emit, path: &str, flag: &AtomicBool) -> Reply {
    let client = match reqwest::blocking::Client::builder()
        .timeout(None)
        .connect_timeout(CONNECT_TIMEOUT)
        .build()
    {
        Ok(client) => client,
        Err(cause) => return Reply::failed(format!("cannot start an HTTPS client: {cause}")),
    };
    let sent = client
        .post(ENDPOINT)
        .header("x-api-key", key)
        .header("anthropic-version", API_VERSION)
        .header("content-type", "application/json")
        .body(body.to_string())
        .send();
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
    gather(lines, |text| {
        emit(DELTA_EVENT, json!({ "path": path, "text": text }));
    })
}

fn unreachable(cause: &reqwest::Error) -> String {
    if cause.is_timeout() {
        return "the Anthropic API did not answer in time".to_string();
    }
    format!("cannot reach the Anthropic API: {cause}")
}

fn refused(status: u16, text: &str) -> String {
    match serde_json::from_str::<Value>(text) {
        Ok(value) if value.get("error").is_some() => describe(value.get("error")),
        _ => format!("the Anthropic API answered HTTP {status}"),
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

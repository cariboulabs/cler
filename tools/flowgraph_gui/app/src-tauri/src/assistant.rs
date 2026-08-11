use std::collections::HashMap;
use std::io::{BufRead, BufReader};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex, MutexGuard, PoisonError};
use std::thread;
use std::time::Duration;

use cler_graph::palette_types::Port;
use cler_graph::BlockSpec;
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};

use crate::build::Emit;
use crate::document::{self, Documents};

pub const MODEL: &str = "claude-opus-5";
pub const KEY_ENV: &str = "ANTHROPIC_API_KEY";
pub const KEY_FILE: &str = "anthropic-key";

const ENDPOINT: &str = "https://api.anthropic.com/v1/messages";
const API_VERSION: &str = "2023-06-01";
const EFFORT: &str = "medium";
const MAX_TOKENS: u32 = 16000;
const CONNECT_TIMEOUT: Duration = Duration::from_secs(20);

const DELTA_EVENT: &str = "assistant-delta";
const DONE_EVENT: &str = "assistant-done";

const GUIDE: &str = include_str!("../../../../../AGENTS.md");
const GUIDE_SECTIONS: [&str; 4] = ["## 1. ", "## 4. ", "## 5. ", "## 6. "];

const MODEL_BUDGET: usize = 16_000;
const SOURCE_BUDGET: usize = 32_000;
const PALETTE_BUDGET: usize = 8_000;

const SYSTEM: &str = "\
You are the cler flowgraph assistant, embedded in the cler flowgraph editor.

You explain; you do not act. You cannot edit the graph, run commands, or write \
files, and nothing you say is applied. When the user asks for a change, say \
exactly what you would do — which blocks, which wires, which parameters — and \
tell them that applying it arrives in the next stage of this editor.

Answer from the material you are given: the parsed graph model, the source of \
the open file, the block palette, and the cler guide below. Say plainly when \
the answer is not in that material instead of guessing.

Be concise. A sentence or two for a simple question; short paragraphs or a \
short list for a hard one. Cite a block by its display name and its variable, \
like Chirp (chirp), and an edge as source -> target.port. Use plain text, \
**bold**, `code` and - lists only.";

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
    Usage(u64, u64),
    Failed(String),
    Done,
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

fn preamble() -> String {
    format!("{SYSTEM}\n\n<cler_guide>\n{}\n</cler_guide>", guide())
}

pub fn request(context: &str, question: &str, history: &[Turn]) -> String {
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
    json!({
        "model": MODEL,
        "max_tokens": MAX_TOKENS,
        "stream": true,
        "output_config": { "effort": EFFORT },
        "system": [
            { "type": "text", "text": preamble(), "cache_control": { "type": "ephemeral" } },
            { "type": "text", "text": context }
        ],
        "messages": messages
    })
    .to_string()
}

pub fn chunk(line: &str) -> Option<Chunk> {
    let payload = line.strip_prefix("data:")?.trim();
    let value: Value = serde_json::from_str(payload).ok()?;
    match value.get("type").and_then(Value::as_str)? {
        "content_block_delta" => value
            .pointer("/delta/text")
            .and_then(Value::as_str)
            .map(|text| Chunk::Text(text.to_string())),
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
    );

    let flag = arm(talks, path);
    let path = path.to_string();
    thread::spawn(move || {
        let (input, output, failure) = stream(&key, &body, &emit, &path, &flag);
        emit(
            DONE_EVENT,
            json!({
                "path": path,
                "usage": { "input_tokens": input, "output_tokens": output },
                "error": failure
            }),
        );
    });
    Ok(())
}

fn stream(
    key: &str,
    body: &str,
    emit: &Emit,
    path: &str,
    flag: &AtomicBool,
) -> (u64, u64, Option<String>) {
    let client = match reqwest::blocking::Client::builder()
        .timeout(None)
        .connect_timeout(CONNECT_TIMEOUT)
        .build()
    {
        Ok(client) => client,
        Err(cause) => return (0, 0, Some(format!("cannot start an HTTPS client: {cause}"))),
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
        Err(cause) => return (0, 0, Some(unreachable(&cause))),
    };
    if !response.status().is_success() {
        let status = response.status().as_u16();
        let text = response.text().unwrap_or_default();
        return (0, 0, Some(refused(status, &text)));
    }

    let mut input = 0;
    let mut output = 0;
    for line in BufReader::new(response).lines().map_while(Result::ok) {
        if flag.load(Ordering::Relaxed) {
            break;
        }
        match chunk(&line) {
            Some(Chunk::Text(text)) => emit(DELTA_EVENT, json!({ "path": path, "text": text })),
            Some(Chunk::Usage(seen, written)) => {
                input = input.max(seen);
                output = output.max(written);
            }
            Some(Chunk::Failed(message)) => return (input, output, Some(message)),
            Some(Chunk::Done) => break,
            None => {}
        }
    }
    (input, output, None)
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

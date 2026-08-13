use std::io::{BufRead, BufReader, Write};
use std::net::{TcpListener, TcpStream};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex, MutexGuard, PoisonError};
use std::thread;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use base64::engine::general_purpose::URL_SAFE_NO_PAD;
use base64::Engine;
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use sha2::{Digest, Sha256};

use crate::ai_agent::{self, Status};
use crate::build::Emit;

pub const CLIENT_ID: &str = "9d1c250a-e61b-44d9-88ed-5944d1962f5e";
pub const TOKEN_FILE: &str = "anthropic-oauth.json";
pub const AUTH_CHANGED_EVENT: &str = "ai-agent-auth-changed";

const AUTHORIZE_ENDPOINT: &str = "https://claude.ai/oauth/authorize";
const TOKEN_ENDPOINT: &str = "https://platform.claude.com/v1/oauth/token";
const SCOPES: &str = "org:create_api_key user:profile user:inference user:sessions:claude_code user:mcp_servers user:file_upload";
const REDIRECT_URI: &str = "http://localhost:53692/callback";
const BIND_ADDR: &str = "127.0.0.1:53692";
const LOGIN_WINDOW: Duration = Duration::from_secs(300);
const POLL: Duration = Duration::from_millis(100);
const REFRESH_MARGIN_MS: u64 = 5 * 60 * 1000;

const DONE_PAGE: &str = "<!doctype html><html><head><meta charset=\"utf-8\"><title>cler</title></head>\
<body style=\"margin:0;min-height:100vh;display:grid;place-items:center;background:#140f10;\
font-family:system-ui,sans-serif\"><div style=\"text-align:center;padding:48px 56px;\
background:#1d1618;border:1px solid #412d31;border-radius:12px\">\
<div style=\"font-size:40px;color:#d11f33\">&#10003;</div>\
<h1 style=\"margin:16px 0 8px;font-size:20px;font-weight:600;color:#f0e9ea\">Signed in</h1>\
<p style=\"margin:0;font-size:14px;color:#a8979a\">You can close this tab and return to cler.</p>\
</div></body></html>";
const FAIL_PAGE: &str = "<!doctype html><html><head><meta charset=\"utf-8\"><title>cler</title></head>\
<body style=\"margin:0;min-height:100vh;display:grid;place-items:center;background:#140f10;\
font-family:system-ui,sans-serif\"><div style=\"text-align:center;padding:48px 56px;\
background:#1d1618;border:1px solid #412d31;border-radius:12px\">\
<div style=\"font-size:40px;color:#a8979a\">&#10007;</div>\
<h1 style=\"margin:16px 0 8px;font-size:20px;font-weight:600;color:#f0e9ea\">Sign-in failed</h1>\
<p style=\"margin:0;font-size:14px;color:#a8979a\">Return to cler and try again.</p>\
</div></body></html>";

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Tokens {
    pub access: String,
    pub refresh: String,
    pub expires_unix_ms: u64,
}

struct Pending {
    verifier: String,
    cancel: Arc<AtomicBool>,
}

static PENDING: Mutex<Option<Pending>> = Mutex::new(None);

fn pending() -> MutexGuard<'static, Option<Pending>> {
    PENDING.lock().unwrap_or_else(PoisonError::into_inner)
}

pub fn token_path(config_dir: &Path) -> PathBuf {
    config_dir.join(TOKEN_FILE)
}

pub fn load(config_dir: &Path) -> Option<Tokens> {
    let text = std::fs::read_to_string(token_path(config_dir)).ok()?;
    serde_json::from_str(&text).ok()
}

pub fn store(config_dir: &Path, tokens: &Tokens) -> Result<(), String> {
    std::fs::create_dir_all(config_dir)
        .map_err(|cause| format!("cannot create {}: {cause}", config_dir.display()))?;
    let file = token_path(config_dir);
    let text = serde_json::to_string(tokens).map_err(|cause| cause.to_string())?;
    std::fs::write(&file, text)
        .map_err(|cause| format!("cannot write {}: {cause}", file.display()))?;
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        std::fs::set_permissions(&file, std::fs::Permissions::from_mode(0o600))
            .map_err(|cause| format!("cannot chmod {}: {cause}", file.display()))?;
    }
    Ok(())
}

pub fn now_ms() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|since| since.as_millis() as u64)
        .unwrap_or(0)
}

pub fn fresh(tokens: &Tokens, now_ms: u64) -> bool {
    now_ms + REFRESH_MARGIN_MS < tokens.expires_unix_ms
}

pub fn access_via(
    config_dir: &Path,
    now: u64,
    renew: impl FnOnce(&str) -> Result<Tokens, String>,
) -> Result<String, String> {
    let tokens = load(config_dir).ok_or_else(|| {
        format!("not signed in with Claude — no {}", token_path(config_dir).display())
    })?;
    if fresh(&tokens, now) {
        return Ok(tokens.access);
    }
    let renewed = renew(&tokens.refresh)?;
    store(config_dir, &renewed)?;
    Ok(renewed.access)
}

pub fn access(config_dir: &Path) -> Result<String, String> {
    access_via(config_dir, now_ms(), refresh_http)
}

pub fn signed_in(config_dir: &Path) -> bool {
    token_path(config_dir).is_file()
}

pub fn challenge_of(verifier: &str) -> String {
    URL_SAFE_NO_PAD.encode(Sha256::digest(verifier.as_bytes()))
}

pub fn pkce() -> Result<(String, String), String> {
    let mut bytes = [0u8; 32];
    getrandom::fill(&mut bytes).map_err(|cause| format!("cannot gather randomness: {cause}"))?;
    let verifier = URL_SAFE_NO_PAD.encode(bytes);
    let challenge = challenge_of(&verifier);
    Ok((verifier, challenge))
}

fn encode(text: &str) -> String {
    let mut out = String::new();
    for byte in text.bytes() {
        match byte {
            b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9' | b'-' | b'.' | b'_' | b'~' => {
                out.push(byte as char)
            }
            _ => out.push_str(&format!("%{byte:02X}")),
        }
    }
    out
}

fn decode(text: &str) -> String {
    let mut out = Vec::new();
    let bytes = text.as_bytes();
    let mut at = 0;
    while at < bytes.len() {
        match bytes[at] {
            b'%' if at + 2 < bytes.len() => {
                match u8::from_str_radix(&text[at + 1..at + 3], 16) {
                    Ok(byte) => {
                        out.push(byte);
                        at += 3;
                    }
                    Err(_) => {
                        out.push(b'%');
                        at += 1;
                    }
                }
            }
            b'+' => {
                out.push(b' ');
                at += 1;
            }
            byte => {
                out.push(byte);
                at += 1;
            }
        }
    }
    String::from_utf8_lossy(&out).into_owned()
}

pub fn authorize_url(challenge: &str, state: &str) -> String {
    format!(
        "{AUTHORIZE_ENDPOINT}?code=true&client_id={}&response_type=code&redirect_uri={}&scope={}&code_challenge={}&code_challenge_method=S256&state={}",
        encode(CLIENT_ID),
        encode(REDIRECT_URI),
        encode(SCOPES),
        encode(challenge),
        encode(state)
    )
}

fn query_pairs(query: &str) -> Vec<(String, String)> {
    query
        .split('&')
        .filter(|pair| !pair.is_empty())
        .map(|pair| match pair.split_once('=') {
            Some((name, value)) => (decode(name), decode(value)),
            None => (decode(pair), String::new()),
        })
        .collect()
}

fn found(pairs: &[(String, String)], name: &str) -> Option<String> {
    pairs
        .iter()
        .find(|(key, _)| key == name)
        .map(|(_, value)| value.clone())
}

pub fn parse_callback(query: &str) -> Result<(String, String), String> {
    let pairs = query_pairs(query);
    if let Some(error) = found(&pairs, "error") {
        return Err(format!("the sign-in was refused: {error}"));
    }
    let code = found(&pairs, "code").filter(|code| !code.is_empty());
    let state = found(&pairs, "state").filter(|state| !state.is_empty());
    match (code, state) {
        (Some(code), Some(state)) => Ok((code, state)),
        _ => Err("the callback carried no code and state".to_string()),
    }
}

pub fn parse_manual(input: &str) -> Result<(String, Option<String>), String> {
    let trimmed = input.trim();
    if trimmed.is_empty() {
        return Err("paste the code from the browser first".to_string());
    }
    if let Some((_, query)) = trimmed.split_once('?') {
        let query = query.split('#').next().unwrap_or(query);
        let (code, state) = parse_callback(query)?;
        return Ok((code, Some(state)));
    }
    if let Some((code, state)) = trimmed.split_once('#') {
        if code.is_empty() {
            return Err("the pasted code is empty".to_string());
        }
        return Ok((code.to_string(), Some(state.to_string())));
    }
    Ok((trimmed.to_string(), None))
}

fn token_request(body: Value) -> Result<Tokens, String> {
    let client = reqwest::blocking::Client::builder()
        .timeout(Duration::from_secs(30))
        .build()
        .map_err(|cause| format!("cannot start an HTTPS client: {cause}"))?;
    let response = client
        .post(TOKEN_ENDPOINT)
        .header("content-type", "application/json")
        .body(body.to_string())
        .send()
        .map_err(|cause| format!("cannot reach the Claude token endpoint: {cause}"))?;
    let status = response.status().as_u16();
    let text = response.text().unwrap_or_default();
    if status != 200 {
        return Err(format!("the Claude token endpoint answered HTTP {status}: {text}"));
    }
    let value: Value = serde_json::from_str(&text)
        .map_err(|_| "the Claude token endpoint answered with something other than JSON".to_string())?;
    let access = value
        .get("access_token")
        .and_then(Value::as_str)
        .ok_or_else(|| "the token answer carried no access_token".to_string())?;
    let refresh = value
        .get("refresh_token")
        .and_then(Value::as_str)
        .unwrap_or_default();
    let expires_in = value.get("expires_in").and_then(Value::as_u64).unwrap_or(0);
    Ok(Tokens {
        access: access.to_string(),
        refresh: refresh.to_string(),
        expires_unix_ms: now_ms() + expires_in * 1000,
    })
}

fn exchange(code: &str, state: &str, verifier: &str) -> Result<Tokens, String> {
    token_request(json!({
        "grant_type": "authorization_code",
        "client_id": CLIENT_ID,
        "code": code,
        "state": state,
        "redirect_uri": REDIRECT_URI,
        "code_verifier": verifier
    }))
}

fn refresh_http(refresh: &str) -> Result<Tokens, String> {
    let mut renewed = token_request(json!({
        "grant_type": "refresh_token",
        "client_id": CLIENT_ID,
        "refresh_token": refresh
    }))?;
    if renewed.refresh.is_empty() {
        renewed.refresh = refresh.to_string();
    }
    Ok(renewed)
}

fn respond(stream: &mut TcpStream, status: &str, page: &str) {
    let _ = write!(
        stream,
        "HTTP/1.1 {status}\r\ncontent-type: text/html\r\ncontent-length: {}\r\nconnection: close\r\n\r\n{page}",
        page.len()
    );
}

fn request_query(stream: &TcpStream) -> Option<String> {
    let mut line = String::new();
    BufReader::new(stream).read_line(&mut line).ok()?;
    let path = line.split_whitespace().nth(1)?;
    let (route, query) = path.split_once('?')?;
    if route != "/callback" {
        return None;
    }
    Some(query.to_string())
}

fn bind_listener() -> Result<TcpListener, String> {
    let deadline = Instant::now() + Duration::from_secs(2);
    loop {
        match TcpListener::bind(BIND_ADDR) {
            Ok(listener) => return Ok(listener),
            Err(cause) if Instant::now() < deadline => {
                let _ = cause;
                thread::sleep(POLL);
            }
            Err(cause) => return Err(format!("cannot listen on {BIND_ADDR}: {cause}")),
        }
    }
}

fn complete(config_dir: &Path, code: &str, state: &str, verifier: &str) -> Result<(), String> {
    if state != verifier {
        return Err("the sign-in state does not match this login attempt".to_string());
    }
    let tokens = exchange(code, state, verifier)?;
    store(config_dir, &tokens)
}

fn wait_for_callback(
    listener: TcpListener,
    cancel: Arc<AtomicBool>,
    verifier: String,
    config_dir: PathBuf,
    emit: Emit,
) {
    let _ = listener.set_nonblocking(true);
    let deadline = Instant::now() + LOGIN_WINDOW;
    while !cancel.load(Ordering::Relaxed) && Instant::now() < deadline {
        let mut stream = match listener.accept() {
            Ok((stream, _)) => stream,
            Err(_) => {
                thread::sleep(POLL);
                continue;
            }
        };
        let Some(query) = request_query(&stream) else {
            respond(&mut stream, "400 Bad Request", FAIL_PAGE);
            continue;
        };
        let finished = parse_callback(&query)
            .and_then(|(code, state)| complete(&config_dir, &code, &state, &verifier));
        match finished {
            Ok(()) => {
                respond(&mut stream, "200 OK", DONE_PAGE);
                let mut slot = pending();
                if slot.as_ref().is_some_and(|login| login.verifier == verifier) {
                    *slot = None;
                }
                drop(slot);
                emit(AUTH_CHANGED_EVENT, json!(ai_agent::status(&config_dir)));
                return;
            }
            Err(_) => {
                respond(&mut stream, "400 Bad Request", FAIL_PAGE);
                return;
            }
        }
    }
}

pub fn start_login(config_dir: &Path, emit: Emit) -> Result<String, String> {
    {
        let mut slot = pending();
        if let Some(previous) = slot.take() {
            previous.cancel.store(true, Ordering::Relaxed);
        }
    }
    let listener = bind_listener()?;
    let (verifier, challenge) = pkce()?;
    let cancel = Arc::new(AtomicBool::new(false));
    *pending() = Some(Pending {
        verifier: verifier.clone(),
        cancel: cancel.clone(),
    });
    let url = authorize_url(&challenge, &verifier);
    let config_dir = config_dir.to_path_buf();
    thread::spawn(move || wait_for_callback(listener, cancel, verifier, config_dir, emit));
    Ok(url)
}

pub fn finish_login(input: &str, config_dir: &Path) -> Result<Status, String> {
    let verifier = {
        let slot = pending();
        slot.as_ref()
            .map(|login| login.verifier.clone())
            .ok_or_else(|| "no sign-in is in progress — start one first".to_string())?
    };
    let (code, state) = parse_manual(input)?;
    if state.as_deref().is_some_and(|state| state != verifier) {
        return Err("the pasted state does not match this login attempt".to_string());
    }
    let tokens = exchange(&code, &verifier, &verifier)?;
    store(config_dir, &tokens)?;
    let mut slot = pending();
    if let Some(login) = slot.take() {
        login.cancel.store(true, Ordering::Relaxed);
    }
    drop(slot);
    Ok(ai_agent::status(config_dir))
}

pub fn logout(config_dir: &Path) -> Status {
    std::fs::remove_file(token_path(config_dir)).ok();
    ai_agent::status(config_dir)
}

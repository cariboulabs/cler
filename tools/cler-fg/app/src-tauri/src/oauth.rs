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

pub const AUTH_CHANGED_EVENT: &str = "ai-agent-auth-changed";

pub struct Flow {
    pub client_id: &'static str,
    pub authorize: &'static str,
    pub token: &'static str,
    pub scopes: &'static str,
    pub redirect_uri: &'static str,
    pub bind_addr: &'static str,
    pub route: &'static str,
    pub token_file: &'static str,
    pub extra: &'static [(&'static str, &'static str)],
    pub form: bool,
    // Anthropic's token body carries state, and its state is the verifier.
    pub state_is_verifier: bool,
    // Anthropic may answer a refresh without a new refresh_token, meaning the
    // old one still stands; endpoints that always rotate must instead fail
    // loudly, since the spent token would otherwise loop forever.
    pub carry_refresh: bool,
}

// Statics, not consts: the flow travels as &'static Flow and const promotion
// would hand out a fresh address per use site.
pub static ANTHROPIC_FLOW: Flow = Flow {
    client_id: "9d1c250a-e61b-44d9-88ed-5944d1962f5e",
    authorize: "https://claude.ai/oauth/authorize",
    token: "https://platform.claude.com/v1/oauth/token",
    scopes: "org:create_api_key user:profile user:inference user:sessions:claude_code user:mcp_servers user:file_upload",
    redirect_uri: "http://localhost:53692/callback",
    bind_addr: "127.0.0.1:53692",
    route: "/callback",
    token_file: "anthropic-oauth.json",
    extra: &[("code", "true")],
    form: false,
    state_is_verifier: true,
    carry_refresh: true,
};

// Dead until a provider claims the openai-codex id.
pub static CODEX_FLOW: Flow = Flow {
    client_id: "app_EMoamEEZ73f0CkXaXp7hrann",
    authorize: "https://auth.openai.com/oauth/authorize",
    token: "https://auth.openai.com/oauth/token",
    scopes: "openid profile email offline_access",
    redirect_uri: "http://localhost:1455/auth/callback",
    bind_addr: "127.0.0.1:1455",
    route: "/auth/callback",
    token_file: "openai-codex-oauth.json",
    extra: &[
        ("id_token_add_organizations", "true"),
        ("codex_cli_simplified_flow", "true"),
        ("originator", ORIGINATOR),
    ],
    form: true,
    state_is_verifier: false,
    carry_refresh: false,
};

pub const ORIGINATOR: &str = "cler";

pub fn flow_for(provider_id: &str) -> &'static Flow {
    match provider_id {
        "openai-codex" => &CODEX_FLOW,
        _ => &ANTHROPIC_FLOW,
    }
}

fn flow() -> &'static Flow {
    flow_for(ai_agent::provider().id)
}

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
    flow: &'static Flow,
    verifier: String,
    state: String,
    cancel: Arc<AtomicBool>,
}

static PENDING: Mutex<Option<Pending>> = Mutex::new(None);

fn pending() -> MutexGuard<'static, Option<Pending>> {
    PENDING.lock().unwrap_or_else(PoisonError::into_inner)
}

pub fn token_path(flow: &Flow, config_dir: &Path) -> PathBuf {
    config_dir.join(flow.token_file)
}

pub fn load(flow: &Flow, config_dir: &Path) -> Option<Tokens> {
    let text = std::fs::read_to_string(token_path(flow, config_dir)).ok()?;
    serde_json::from_str(&text).ok()
}

// A refresh token is single use: a truncated file is a permanent brick, so the
// new tokens land in a sibling file and reach the real path by rename only.
pub fn store(flow: &Flow, config_dir: &Path, tokens: &Tokens) -> Result<(), String> {
    std::fs::create_dir_all(config_dir)
        .map_err(|cause| format!("cannot create {}: {cause}", config_dir.display()))?;
    let file = token_path(flow, config_dir);
    let mut name = file.file_name().unwrap_or_default().to_os_string();
    name.push(".tmp");
    let temp = file.with_file_name(name);
    let text = serde_json::to_string(tokens).map_err(|cause| cause.to_string())?;

    let written = (|| -> std::io::Result<()> {
        let mut options = std::fs::OpenOptions::new();
        options.write(true).create(true).truncate(true);
        #[cfg(unix)]
        {
            use std::os::unix::fs::OpenOptionsExt;
            options.mode(0o600);
        }
        let mut handle = options.open(&temp)?;
        handle.write_all(text.as_bytes())?;
        handle.sync_all()
    })();
    if let Err(cause) = written {
        std::fs::remove_file(&temp).ok();
        return Err(format!("cannot write {}: {cause}", temp.display()));
    }
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        if let Err(cause) =
            std::fs::set_permissions(&temp, std::fs::Permissions::from_mode(0o600))
        {
            std::fs::remove_file(&temp).ok();
            return Err(format!("cannot chmod {}: {cause}", temp.display()));
        }
    }
    std::fs::rename(&temp, &file).map_err(|cause| {
        std::fs::remove_file(&temp).ok();
        format!("cannot replace {}: {cause}", file.display())
    })
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

// ponytail: one process-wide lock, not one per token file — a login refreshes
// once every few hours, so contention is not a thing worth modelling.
static RENEWING: Mutex<()> = Mutex::new(());

pub fn access_via(
    flow: &Flow,
    config_dir: &Path,
    now: u64,
    renew: impl FnOnce(&str) -> Result<Tokens, String>,
) -> Result<String, String> {
    let missing = || {
        format!("not signed in — no {}", token_path(flow, config_dir).display())
    };
    let tokens = load(flow, config_dir).ok_or_else(missing)?;
    if fresh(&tokens, now) {
        return Ok(tokens.access);
    }
    let _turn = RENEWING.lock().unwrap_or_else(PoisonError::into_inner);
    // A sibling thread may have rotated while we waited; its refresh token is
    // the only usable one left, so re-read rather than renew from ours.
    let tokens = load(flow, config_dir).ok_or_else(missing)?;
    if fresh(&tokens, now) {
        return Ok(tokens.access);
    }
    let renewed = renew(&tokens.refresh)?;
    store(flow, config_dir, &renewed)?;
    Ok(renewed.access)
}

pub fn access(config_dir: &Path) -> Result<String, String> {
    let flow = flow();
    access_via(flow, config_dir, now_ms(), |refresh| refresh_http(flow, refresh))
}

pub fn signed_in(config_dir: &Path) -> bool {
    token_path(flow(), config_dir).is_file()
}

pub fn challenge_of(verifier: &str) -> String {
    URL_SAFE_NO_PAD.encode(Sha256::digest(verifier.as_bytes()))
}

pub fn random_token() -> Result<String, String> {
    let mut bytes = [0u8; 32];
    getrandom::fill(&mut bytes).map_err(|cause| format!("cannot gather randomness: {cause}"))?;
    Ok(URL_SAFE_NO_PAD.encode(bytes))
}

pub fn pkce() -> Result<(String, String), String> {
    let verifier = random_token()?;
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

pub fn authorize_url(flow: &Flow, challenge: &str, state: &str) -> String {
    let mut extra = String::new();
    for (name, value) in flow.extra {
        extra.push_str(&format!("{}={}&", encode(name), encode(value)));
    }
    format!(
        "{}?{extra}client_id={}&response_type=code&redirect_uri={}&scope={}&code_challenge={}&code_challenge_method=S256&state={}",
        flow.authorize,
        encode(flow.client_id),
        encode(flow.redirect_uri),
        encode(flow.scopes),
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

pub fn body_of(flow: &Flow, fields: &[(&str, &str)]) -> String {
    if !flow.form {
        let mut object = serde_json::Map::new();
        for (name, value) in fields {
            object.insert(name.to_string(), json!(value));
        }
        return Value::Object(object).to_string();
    }
    fields
        .iter()
        .map(|(name, value)| format!("{}={}", encode(name), encode(value)))
        .collect::<Vec<_>>()
        .join("&")
}

pub fn exchange_body(flow: &Flow, code: &str, state: &str, verifier: &str) -> String {
    let mut fields = vec![
        ("grant_type", "authorization_code"),
        ("client_id", flow.client_id),
        ("code", code),
        ("redirect_uri", flow.redirect_uri),
        ("code_verifier", verifier),
    ];
    if flow.state_is_verifier {
        fields.insert(3, ("state", state));
    }
    body_of(flow, &fields)
}

fn token_request(flow: &Flow, body: String) -> Result<Tokens, String> {
    let client = reqwest::blocking::Client::builder()
        .timeout(Duration::from_secs(30))
        .build()
        .map_err(|cause| format!("cannot start an HTTPS client: {cause}"))?;
    let content_type = if flow.form {
        "application/x-www-form-urlencoded"
    } else {
        "application/json"
    };
    let response = client
        .post(flow.token)
        .header("content-type", content_type)
        .body(body)
        .send()
        .map_err(|cause| format!("cannot reach {}: {cause}", flow.token))?;
    let status = response.status().as_u16();
    let text = response.text().unwrap_or_default();
    if status != 200 {
        return Err(format!("{} answered HTTP {status}: {text}", flow.token));
    }
    let value: Value = serde_json::from_str(&text)
        .map_err(|_| format!("{} answered with something other than JSON", flow.token))?;
    tokens_of(flow, &value)
}

pub fn tokens_of(flow: &Flow, value: &Value) -> Result<Tokens, String> {
    let access = value
        .get("access_token")
        .and_then(Value::as_str)
        .ok_or_else(|| "the token answer carried no access_token".to_string())?;
    let refresh = value
        .get("refresh_token")
        .and_then(Value::as_str)
        .unwrap_or_default();
    // A missing expires_in costs one round trip: the token reads as stale and is
    // renewed on the next call. A missing refresh_token on a rotating flow costs
    // the login, since the one that bought this answer is already spent.
    let expires_in = value.get("expires_in").and_then(Value::as_u64);
    if !flow.carry_refresh && refresh.is_empty() {
        return Err(format!(
            "{} answered without a refresh_token — sign in again",
            flow.token
        ));
    }
    Ok(Tokens {
        access: access.to_string(),
        refresh: refresh.to_string(),
        expires_unix_ms: now_ms() + expires_in.unwrap_or(0) * 1000,
    })
}

fn exchange(flow: &Flow, code: &str, state: &str, verifier: &str) -> Result<Tokens, String> {
    token_request(flow, exchange_body(flow, code, state, verifier))
}

fn refresh_http(flow: &Flow, refresh: &str) -> Result<Tokens, String> {
    let body = body_of(
        flow,
        &[
            ("grant_type", "refresh_token"),
            ("client_id", flow.client_id),
            ("refresh_token", refresh),
        ],
    );
    let mut renewed = token_request(flow, body)?;
    if flow.carry_refresh && renewed.refresh.is_empty() {
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

fn request_query(flow: &Flow, stream: &TcpStream) -> Option<String> {
    // The listener is non-blocking and some platforms hand that down to the
    // accepted socket; a peer that connects and says nothing would otherwise
    // hold the login thread, and the port, until cler quits.
    let _ = stream.set_nonblocking(false);
    let _ = stream.set_read_timeout(Some(Duration::from_secs(2)));
    let mut line = String::new();
    BufReader::new(stream).read_line(&mut line).ok()?;
    let path = line.split_whitespace().nth(1)?;
    let (route, query) = path.split_once('?')?;
    if route != flow.route {
        return None;
    }
    Some(query.to_string())
}

fn bind_listener(flow: &Flow) -> Result<TcpListener, String> {
    let deadline = Instant::now() + Duration::from_secs(2);
    loop {
        match TcpListener::bind(flow.bind_addr) {
            Ok(listener) => return Ok(listener),
            Err(cause) if Instant::now() < deadline => {
                let _ = cause;
                thread::sleep(POLL);
            }
            // The port is fixed by the redirect registered with the provider,
            // so there is nothing to fall back to.
            Err(cause) => {
                return Err(format!(
                    "cannot listen on {}: {cause} — the sign-in needs that exact port; close whatever holds it (another cler window, or an earlier sign-in) and try again",
                    flow.bind_addr
                ))
            }
        }
    }
}

pub fn complete(
    flow: &Flow,
    config_dir: &Path,
    code: &str,
    state: &str,
    expected: &str,
    verifier: &str,
) -> Result<(), String> {
    if state != expected {
        return Err("the sign-in state does not match this login attempt".to_string());
    }
    let tokens = exchange(flow, code, state, verifier)?;
    store(flow, config_dir, &tokens)
}

fn wait_for_callback(
    flow: &'static Flow,
    listener: TcpListener,
    cancel: Arc<AtomicBool>,
    verifier: String,
    state: String,
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
        let Some(query) = request_query(flow, &stream) else {
            respond(&mut stream, "400 Bad Request", FAIL_PAGE);
            continue;
        };
        let finished = match parse_callback(&query) {
            // A tab left over from an earlier attempt must not end this one.
            Ok((_, seen)) if seen != state => {
                respond(&mut stream, "400 Bad Request", FAIL_PAGE);
                continue;
            }
            Ok((code, seen)) => complete(flow, &config_dir, &code, &seen, &state, &verifier),
            Err(message) => Err(message),
        };
        match finished {
            Ok(()) => {
                respond(&mut stream, "200 OK", DONE_PAGE);
                let mut slot = pending();
                if slot.as_ref().is_some_and(|login| login.state == state) {
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
    let flow = flow();
    let listener = bind_listener(flow)?;
    let (verifier, challenge) = pkce()?;
    // Only a flow whose state is defined to be the verifier may send it
    // through the browser.
    let state = if flow.state_is_verifier {
        verifier.clone()
    } else {
        random_token()?
    };
    let cancel = Arc::new(AtomicBool::new(false));
    *pending() = Some(Pending {
        flow,
        verifier: verifier.clone(),
        state: state.clone(),
        cancel: cancel.clone(),
    });
    let url = authorize_url(flow, &challenge, &state);
    let config_dir = config_dir.to_path_buf();
    thread::spawn(move || {
        wait_for_callback(flow, listener, cancel, verifier, state, config_dir, emit)
    });
    Ok(url)
}

// Manual paste is Anthropic-only: no UI reaches it for any other flow.
pub fn finish_login(input: &str, config_dir: &Path) -> Result<Status, String> {
    let flow = &ANTHROPIC_FLOW;
    let verifier = {
        let slot = pending();
        let login = slot
            .as_ref()
            .ok_or_else(|| "no sign-in is in progress — start one first".to_string())?;
        if !std::ptr::eq(login.flow, flow) {
            return Err("the sign-in in progress is not a Claude one".to_string());
        }
        login.verifier.clone()
    };
    let (code, state) = parse_manual(input)?;
    if state.as_deref().is_some_and(|state| state != verifier) {
        return Err("the pasted state does not match this login attempt".to_string());
    }
    let tokens = exchange(flow, &code, &verifier, &verifier)?;
    store(flow, config_dir, &tokens)?;
    let mut slot = pending();
    if let Some(login) = slot.take() {
        login.cancel.store(true, Ordering::Relaxed);
    }
    drop(slot);
    Ok(ai_agent::status(config_dir))
}

pub fn logout(config_dir: &Path) -> Status {
    std::fs::remove_file(token_path(flow(), config_dir)).ok();
    ai_agent::status(config_dir)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Read;
    use std::sync::mpsc;

    // Answers on an ephemeral port, never the flow's own: a running cler or a
    // codex CLI holds the real ones.
    fn serving() -> (u16, Arc<AtomicBool>) {
        let listener = TcpListener::bind("127.0.0.1:0").expect("ephemeral port");
        let port = listener.local_addr().expect("port").port();
        let cancel = Arc::new(AtomicBool::new(false));
        let flag = cancel.clone();
        let emit: Emit = Arc::new(|_, _| {});
        thread::spawn(move || {
            wait_for_callback(
                &ANTHROPIC_FLOW,
                listener,
                flag,
                "verifier".to_string(),
                "the-live-state".to_string(),
                std::env::temp_dir(),
                emit,
            )
        });
        (port, cancel)
    }

    fn ask(port: u16, request: Option<&str>) -> Option<String> {
        let (sent, heard) = mpsc::channel();
        let request = request.map(str::to_string);
        thread::spawn(move || {
            let Ok(mut stream) = TcpStream::connect(("127.0.0.1", port)) else {
                return;
            };
            match request {
                // A peer that says nothing: the wedge this guards against.
                None => thread::sleep(Duration::from_secs(60)),
                Some(text) => {
                    let _ = stream.write_all(text.as_bytes());
                    let mut answer = String::new();
                    let _ = stream.read_to_string(&mut answer);
                    let _ = sent.send(answer);
                }
            }
        });
        heard.recv_timeout(Duration::from_secs(5)).ok()
    }

    #[test]
    fn a_silent_peer_and_a_stale_tab_leave_the_listener_serving() {
        let (port, cancel) = serving();
        ask(port, None);
        thread::sleep(POLL * 2);

        let stale = ask(
            port,
            Some("GET /callback?code=abc&state=an-older-state HTTP/1.1\r\n\r\n"),
        )
        .expect("a stale callback is answered, not swallowed");
        assert!(stale.starts_with("HTTP/1.1 400"), "{stale}");

        let after = ask(port, Some("GET /nope HTTP/1.1\r\n\r\n"))
            .expect("the listener still serves after a foreign callback");
        assert!(after.starts_with("HTTP/1.1 400"), "{after}");
        cancel.store(true, Ordering::Relaxed);
    }
}

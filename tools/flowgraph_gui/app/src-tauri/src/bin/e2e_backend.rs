use std::io::{BufRead, BufReader, Read, Write};
use std::net::{TcpListener, TcpStream};
use std::path::Path;
use std::sync::{Arc, Mutex, MutexGuard, PoisonError};

use cler_flowgraph_gui::assistant;
use cler_flowgraph_gui::build::{self, Emit, Jobs, RecordArtifact};
use cler_flowgraph_gui::document::{self, Documents};
use cler_flowgraph_gui::provenance::ArtifactRecord;
use notify::{EventKind, RecommendedWatcher, RecursiveMode, Watcher};
use serde::Serialize;
use serde_json::{json, Value};

type Shared = Arc<Documents>;
type Events = Arc<Mutex<Vec<Value>>>;
type FileWatcher = Arc<Mutex<RecommendedWatcher>>;

enum Reply {
    Value(Value),
    Refused(String),
    Loud(String),
}

fn main() {
    let port: u16 = std::env::args()
        .nth(1)
        .and_then(|text| text.parse().ok())
        .expect("usage: e2e_backend <port>");

    let docs: Shared = Arc::new(Documents::default());
    let events: Events = Arc::new(Mutex::new(Vec::new()));
    let jobs = Jobs::default();
    let watcher: FileWatcher = Arc::new(Mutex::new(
        build_watcher(docs.clone(), events.clone()).expect("file watcher"),
    ));

    let listener = TcpListener::bind(("127.0.0.1", port)).expect("bind 127.0.0.1");
    println!("e2e_backend listening on http://127.0.0.1:{port}");

    for stream in listener.incoming().flatten() {
        let docs = docs.clone();
        let events = events.clone();
        let watcher = watcher.clone();
        let jobs = jobs.clone();
        std::thread::spawn(move || {
            serve(stream, &docs, &watcher, &events, &jobs);
        });
    }
}

fn serve(
    mut stream: TcpStream,
    docs: &Shared,
    watcher: &FileWatcher,
    events: &Events,
    jobs: &Jobs,
) {
    let Some((method, target, body)) = request(&stream) else {
        return;
    };
    let (status, payload) = route(&method, &target, &body, docs, watcher, events, jobs);
    respond(&mut stream, status, &payload);
}

fn request(stream: &TcpStream) -> Option<(String, String, Vec<u8>)> {
    let mut reader = BufReader::new(stream.try_clone().ok()?);
    let mut start = String::new();
    reader.read_line(&mut start).ok()?;
    let mut words = start.split_whitespace();
    let method = words.next()?.to_string();
    let target = words.next()?.to_string();

    let mut length = 0usize;
    loop {
        let mut line = String::new();
        if reader.read_line(&mut line).ok()? == 0 {
            break;
        }
        let header = line.trim_end();
        if header.is_empty() {
            break;
        }
        let lower = header.to_ascii_lowercase();
        if let Some(rest) = lower.strip_prefix("content-length:") {
            length = rest.trim().parse().unwrap_or(0);
        }
    }

    let mut body = vec![0u8; length];
    reader.read_exact(&mut body).ok()?;
    Some((method, target, body))
}

fn route(
    method: &str,
    target: &str,
    body: &[u8],
    docs: &Shared,
    watcher: &FileWatcher,
    events: &Events,
    jobs: &Jobs,
) -> (u16, Value) {
    if method == "OPTIONS" {
        return (204, Value::Null);
    }
    match target {
        "/health" => (200, json!({ "ok": true })),
        "/events/poll" => (200, json!({ "ok": drain(events) })),
        "/invoke" => invoke(body, docs, watcher, events, jobs),
        _ => (
            404,
            json!({ "loud": format!("no route for {method} {target}") }),
        ),
    }
}

fn invoke(
    body: &[u8],
    docs: &Shared,
    watcher: &FileWatcher,
    events: &Events,
    jobs: &Jobs,
) -> (u16, Value) {
    let parsed: Value = match serde_json::from_slice(body) {
        Ok(value) => value,
        Err(cause) => return (500, json!({ "loud": format!("bad /invoke body: {cause}") })),
    };
    let Some(cmd) = parsed.get("cmd").and_then(Value::as_str) else {
        return (500, json!({ "loud": "/invoke needs a cmd" }));
    };
    let args = parsed.get("args").cloned().unwrap_or(Value::Null);
    match dispatch(cmd, &args, docs, watcher, events, jobs) {
        Reply::Value(value) => (200, json!({ "ok": value })),
        Reply::Refused(message) => (200, json!({ "err": message })),
        Reply::Loud(message) => (500, json!({ "loud": message })),
    }
}

fn dispatch(
    cmd: &str,
    args: &Value,
    docs: &Shared,
    watcher: &FileWatcher,
    events: &Events,
    jobs: &Jobs,
) -> Reply {
    if cmd == "assistant_status" {
        return carry(assistant::Status {
            available: false,
            model: assistant::MODEL.to_string(),
            reason: assistant::locate(None, Path::new("/nonexistent/cler-e2e")).err(),
        });
    }
    let path = match args.get("path").and_then(Value::as_str) {
        Some(text) => text.to_string(),
        None => return Reply::Loud(format!("{cmd} needs a path argument")),
    };
    match cmd {
        "open_document" => match document::canonical(&path).and_then(|target| {
            watch_parent(watcher, &target)?;
            document::open(docs, &path)
        }) {
            Ok(state) => carry(state),
            Err(message) => Reply::Refused(message),
        },
        "close_document" => {
            if let Some(dir) = document::close(docs, &path) {
                held(watcher).unwatch(&dir).ok();
            }
            Reply::Value(Value::Null)
        }
        "apply_commands" => {
            let base = args
                .get("base_revision")
                .or_else(|| args.get("baseRevision"))
                .and_then(Value::as_u64);
            let commands = args.get("commands").and_then(Value::as_array).cloned();
            match (base, commands) {
                (Some(base), Some(commands)) => {
                    outcome(document::apply(docs, &path, base, commands))
                }
                _ => Reply::Loud("apply_commands needs baseRevision and commands".to_string()),
            }
        }
        "preview_commands" => {
            let base = args
                .get("base_revision")
                .or_else(|| args.get("baseRevision"))
                .and_then(Value::as_u64);
            let commands = args.get("commands").and_then(Value::as_array).cloned();
            match (base, commands) {
                (Some(base), Some(commands)) => {
                    outcome(document::preview(docs, &path, base, commands))
                }
                _ => Reply::Loud("preview_commands needs baseRevision and commands".to_string()),
            }
        }
        "undo" => outcome(document::undo(docs, &path)),
        "redo" => outcome(document::redo(docs, &path)),
        "save_document" => outcome(document::save(docs, &path)),
        "save_cache" => outcome(document::store_cache(
            docs,
            &path,
            args.get("ui").cloned().unwrap_or(Value::Null),
        )),
        "reload_document" => outcome(document::reload(docs, &path)),
        "parse_file" => outcome(document::parse_file(docs, &path)),
        "palette" => outcome(document::palette(docs, &path)),
        "check_document" => outcome(
            document::snapshot_draft(docs, &path)
                .and_then(|draft| build::check_draft(jobs, &path, &draft.path, emitter(events))),
        ),
        "find_target" => outcome(
            document::draft_state(docs, &path)
                .and_then(|draft| build::find_draft_target(jobs, &path, &draft)),
        ),
        "build_target" => outcome(document::snapshot_draft(docs, &path).and_then(|draft| {
            build::build_draft(
                jobs,
                &path,
                &draft,
                artifact_recorder(docs, path.clone()),
                emitter(events),
            )
        })),
        "run_target" => {
            let run_args: Vec<String> = args
                .get("args")
                .and_then(Value::as_array)
                .map(|list| {
                    list.iter()
                        .filter_map(Value::as_str)
                        .map(str::to_string)
                        .collect()
                })
                .unwrap_or_default();
            outcome(document::draft_state(docs, &path).and_then(|draft| {
                build::start_draft(jobs, &path, &draft, &run_args, emitter(events))
            }))
        }
        "stop_target" => outcome(build::stop(jobs, &path)),
        _ => Reply::Loud(format!("unknown command: {cmd}")),
    }
}

fn emitter(events: &Events) -> Emit {
    let events = events.clone();
    Arc::new(move |event: &str, payload: Value| {
        events
            .lock()
            .unwrap_or_else(PoisonError::into_inner)
            .push(json!({ "event": event, "payload": payload }));
    })
}

fn artifact_recorder(docs: &Shared, path: String) -> RecordArtifact {
    let docs = docs.clone();
    Arc::new(move |name: String, record: ArtifactRecord| {
        document::record_artifact(&docs, &path, name, record)
    })
}

fn outcome<T: Serialize>(result: Result<T, String>) -> Reply {
    match result {
        Ok(value) => carry(value),
        Err(message) => Reply::Refused(message),
    }
}

fn carry<T: Serialize>(value: T) -> Reply {
    match serde_json::to_value(value) {
        Ok(json) => Reply::Value(json),
        Err(cause) => Reply::Loud(format!("cannot serialize reply: {cause}")),
    }
}

fn drain(events: &Events) -> Vec<Value> {
    std::mem::take(&mut *events.lock().unwrap_or_else(PoisonError::into_inner))
}

fn held(watcher: &FileWatcher) -> MutexGuard<'_, RecommendedWatcher> {
    watcher.lock().unwrap_or_else(PoisonError::into_inner)
}

fn watch_parent(watcher: &FileWatcher, target: &Path) -> Result<(), String> {
    let dir = target
        .parent()
        .ok_or_else(|| format!("{} has no parent directory", target.display()))?;
    held(watcher)
        .watch(dir, RecursiveMode::NonRecursive)
        .map_err(|cause| format!("cannot watch {}: {cause}", dir.display()))
}

fn build_watcher(docs: Shared, events: Events) -> notify::Result<RecommendedWatcher> {
    notify::recommended_watcher(move |event: notify::Result<notify::Event>| {
        let Ok(event) = event else {
            return;
        };
        if !matches!(
            event.kind,
            EventKind::Create(_) | EventKind::Modify(_) | EventKind::Remove(_)
        ) {
            return;
        }
        for path in event.paths {
            if document::note_disk_event(&docs, &path) {
                events
                    .lock()
                    .unwrap_or_else(PoisonError::into_inner)
                    .push(json!({
                        "event": "document-changed-externally",
                        "payload": { "path": path.display().to_string() },
                    }));
            }
        }
    })
}

fn respond(stream: &mut TcpStream, status: u16, payload: &Value) {
    let body = if payload.is_null() {
        Vec::new()
    } else {
        serde_json::to_vec(payload).unwrap_or_default()
    };
    let head = format!(
        "HTTP/1.1 {status}\r\n\
         Content-Type: application/json\r\n\
         Content-Length: {}\r\n\
         Access-Control-Allow-Origin: *\r\n\
         Access-Control-Allow-Headers: *\r\n\
         Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n\
         Connection: close\r\n\r\n",
        body.len()
    );
    stream.write_all(head.as_bytes()).ok();
    stream.write_all(&body).ok();
    stream.flush().ok();
}

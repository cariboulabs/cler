use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicUsize, Ordering};

use cler_flowgraph_gui::assistant;
use cler_flowgraph_gui::document::{self, Documents};
use serde_json::{json, Value};

static COUNTER: AtomicUsize = AtomicUsize::new(0);

fn corpus(name: &str) -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("../../../../desktop_examples")
        .join(name)
}

fn temp_copy(name: &str) -> PathBuf {
    let unique = COUNTER.fetch_add(1, Ordering::Relaxed);
    let dir =
        std::env::temp_dir().join(format!("cler-gui-session-{}-{unique}", std::process::id()));
    std::fs::create_dir_all(&dir).expect("temp directory");
    let target = dir.join(name);
    std::fs::copy(corpus(name), &target).expect("corpus copy");
    target
}

fn text(path: &Path) -> String {
    std::fs::read_to_string(path).expect("readable document")
}

fn as_str(path: &Path) -> &str {
    path.to_str().expect("utf-8 path")
}

fn set_param(block: &str, index: usize, new_text: &str) -> Vec<Value> {
    vec![json!({
        "command": "set_param",
        "site": 0,
        "block": block,
        "ctor_arg_index": index,
        "new_text": new_text,
    })]
}

#[test]
fn apply_writes_the_file_and_undo_restores_the_bytes() {
    let path = temp_copy("hello_world.cpp");
    let original = text(&path);
    let docs = Documents::default();

    let opened = document::open(&docs, as_str(&path)).expect("open");
    assert_eq!(opened.revision, 0);
    assert!(!opened.can_undo);
    assert!(!opened.can_redo);
    assert!(!opened.external_change);
    assert_eq!(
        opened.path,
        path.canonicalize()
            .expect("canonical")
            .display()
            .to_string()
    );
    assert_eq!(opened.model.model.sites.len(), 1);

    let applied = document::apply(&docs, as_str(&path), 0, set_param("source1", 1, "4.25f"))
        .expect("set_param applies");
    assert_eq!(applied.revision, 1);
    assert!(applied.can_undo);
    assert!(!applied.can_redo);
    assert_ne!(applied.model.sha256, opened.model.sha256);

    let written = text(&path);
    assert_ne!(written, original);
    assert!(written.contains("4.25f"));

    let undone = document::undo(&docs, as_str(&path)).expect("undo");
    assert!(!undone.can_undo);
    assert!(undone.can_redo);
    assert_eq!(text(&path), original);
    assert_eq!(undone.model.sha256, opened.model.sha256);

    let redone = document::redo(&docs, as_str(&path)).expect("redo");
    assert!(redone.can_undo);
    assert!(!redone.can_redo);
    assert_eq!(text(&path), written);
}

#[test]
fn a_self_write_is_not_an_external_change() {
    let path = temp_copy("hello_world.cpp");
    let docs = Documents::default();
    document::open(&docs, as_str(&path)).expect("open");
    document::apply(&docs, as_str(&path), 0, set_param("source1", 1, "7.5f")).expect("apply");

    assert!(!document::note_disk_event(
        &docs,
        &path.canonicalize().expect("canonical")
    ));
}

#[test]
fn an_external_edit_refuses_the_next_write_and_is_reported() {
    let path = temp_copy("hello_world.cpp");
    let docs = Documents::default();
    document::open(&docs, as_str(&path)).expect("open");

    let hijacked = format!("{}\n// edited elsewhere\n", text(&path));
    std::fs::write(&path, &hijacked).expect("external write");

    let canonical = path.canonicalize().expect("canonical");
    assert!(document::note_disk_event(&docs, &canonical));
    assert!(!document::note_disk_event(&docs, &canonical));

    let refusal = document::apply(&docs, as_str(&path), 0, set_param("source1", 1, "9.5f"))
        .expect_err("the write is refused");
    assert!(refusal.contains("changed on disk"), "{refusal}");
    assert_eq!(text(&path), hijacked);

    let undo_refusal = document::undo(&docs, as_str(&path)).expect_err("undo is refused too");
    assert!(undo_refusal.contains("changed on disk"), "{undo_refusal}");

    let reported = document::open(&docs, as_str(&path)).expect("existing session");
    assert!(reported.external_change);
    assert_eq!(reported.revision, 0);
}

#[test]
fn reload_takes_the_disk_bytes_and_drops_the_undo_history() {
    let path = temp_copy("hello_world.cpp");
    let docs = Documents::default();
    document::open(&docs, as_str(&path)).expect("open");
    let applied =
        document::apply(&docs, as_str(&path), 0, set_param("source1", 1, "5.5f")).expect("apply");
    assert!(applied.can_undo);

    let outside = format!("{}\n// reloaded from disk\n", text(&path));
    std::fs::write(&path, &outside).expect("external write");

    let reloaded = document::reload(&docs, as_str(&path)).expect("reload");
    assert!(!reloaded.can_undo);
    assert!(!reloaded.can_redo);
    assert!(!reloaded.external_change);
    assert!(reloaded.revision > applied.revision);

    let refusal = document::undo(&docs, as_str(&path)).expect_err("the undo stack is gone");
    assert!(refusal.contains("nothing_to_undo"), "{refusal}");
    assert_eq!(text(&path), outside);
}

#[test]
fn a_refused_command_leaves_the_file_untouched() {
    let path = temp_copy("adsb_receiver.cpp");
    let original = text(&path);
    let docs = Documents::default();
    document::open(&docs, as_str(&path)).expect("open");

    let refusal = document::apply(&docs, as_str(&path), 0, set_param("source", 0, "\"Nope\""))
        .expect_err("the optional/emplace source is read-only");
    assert!(refusal.contains("not_editable"), "{refusal}");
    assert!(
        refusal.contains("optional_emplace_declaration"),
        "{refusal}"
    );
    assert_eq!(text(&path), original);

    let unchanged = document::open(&docs, as_str(&path)).expect("existing session");
    assert_eq!(unchanged.revision, 0);
    assert!(!unchanged.can_undo);
}

#[test]
fn close_drops_the_session() {
    let path = temp_copy("hello_world.cpp");
    let docs = Documents::default();
    document::open(&docs, as_str(&path)).expect("open");
    document::close(&docs, as_str(&path));

    let missing = document::undo(&docs, as_str(&path)).expect_err("the session is gone");
    assert!(missing.contains("no open document"), "{missing}");
}

#[test]
fn every_state_carries_the_session_source() {
    let path = temp_copy("hello_world.cpp");
    let original = text(&path);
    let docs = Documents::default();

    let opened = document::open(&docs, as_str(&path)).expect("open");
    assert_eq!(opened.source, original);

    let applied = document::apply(&docs, as_str(&path), 0, set_param("source1", 1, "4.25f"))
        .expect("set_param applies");
    assert_ne!(applied.source, original);
    assert!(applied.source.contains("4.25f"));
    assert_eq!(applied.source, text(&path));

    let undone = document::undo(&docs, as_str(&path)).expect("undo");
    assert_eq!(undone.source, original);

    let outside = format!("{original}\n// from elsewhere\n");
    std::fs::write(&path, &outside).expect("external write");
    let reloaded = document::reload(&docs, as_str(&path)).expect("reload");
    assert_eq!(reloaded.source, outside);
}

#[test]
fn the_serialised_state_names_the_source_beside_the_model() {
    let path = temp_copy("hello_world.cpp");
    let docs = Documents::default();
    let opened = document::open(&docs, as_str(&path)).expect("open");
    let value: Value = serde_json::to_value(&opened).expect("serialisable state");

    assert_eq!(value["source"].as_str(), Some(text(&path).as_str()));
    assert!(value["model"]["sites"].is_array());
    assert!(value["canUndo"].is_boolean());
}

#[test]
fn parse_file_reports_the_crate_model_with_a_digest() {
    let path = temp_copy("hello_world.cpp");
    let docs = Documents::default();
    let json = document::parse_file(&docs, as_str(&path)).expect("parse_file");
    let value: Value = serde_json::from_str(&json).expect("json");
    assert_eq!(value["version"], "0.3.0");
    assert_eq!(value["hasErrors"], false);
    assert_eq!(value["has_errors"], false);
    assert_eq!(value["sha256"].as_str().map(str::len), Some(64));
    assert_eq!(value["sites"].as_array().map(Vec::len), Some(1));
}

#[test]
fn preview_shows_the_diff_it_would_write_and_writes_nothing() {
    let path = temp_copy("hello_world.cpp");
    let original = text(&path);
    let docs = Documents::default();
    document::open(&docs, as_str(&path)).expect("open");

    let preview = document::preview(&docs, as_str(&path), 0, set_param("source1", 1, "4.25f"))
        .expect("preview plans");

    assert_eq!(preview.summary.splices, 1);
    assert!(preview.diff.starts_with("@@ -"), "{}", preview.diff);
    assert!(
        preview
            .diff
            .lines()
            .any(|line| line.starts_with('-') && line.contains("1.0f, 1.0f, SPS")),
        "{}",
        preview.diff
    );
    assert!(
        preview
            .diff
            .lines()
            .any(|line| line.starts_with('+') && line.contains("4.25f, 1.0f, SPS")),
        "{}",
        preview.diff
    );
    assert!(
        preview
            .diff
            .lines()
            .any(|line| line.starts_with(' ') && line.contains("const size_t SPS")),
        "the diff carries context lines: {}",
        preview.diff
    );

    assert_eq!(text(&path), original, "preview wrote to the file");
    let reopened = document::open(&docs, as_str(&path)).expect("reopen");
    assert_eq!(reopened.revision, 0, "preview committed a revision");
    assert!(!reopened.can_undo);
}

#[test]
fn preview_of_two_commands_reports_both_splices_in_one_diff() {
    let path = temp_copy("hello_world.cpp");
    let docs = Documents::default();
    document::open(&docs, as_str(&path)).expect("open");

    let preview = document::preview(
        &docs,
        as_str(&path),
        0,
        vec![
            json!({
                "command": "set_display_name",
                "site": 0,
                "block": "source1",
                "new_text": "Chirp",
            }),
            json!({
                "command": "set_display_name",
                "site": 0,
                "block": "throttle",
                "new_text": "Governor",
            }),
        ],
    )
    .expect("preview plans");

    assert_eq!(preview.summary.splices, 2);
    let added: Vec<&str> = preview
        .diff
        .lines()
        .filter(|line| line.starts_with('+'))
        .collect();
    assert!(added.iter().any(|line| line.contains("\"Chirp\"")), "{added:?}");
    assert!(
        added.iter().any(|line| line.contains("\"Governor\"")),
        "{added:?}"
    );
}

#[test]
fn preview_refuses_with_the_same_reasons_apply_would_give() {
    let path = temp_copy("hello_world.cpp");
    let original = text(&path);
    let docs = Documents::default();
    document::open(&docs, as_str(&path)).expect("open");

    let stale = document::preview(&docs, as_str(&path), 7, set_param("source1", 1, "4.25f"))
        .expect_err("a stale base is refused");
    assert!(stale.contains("revision_mismatch"), "{stale}");

    let missing = document::preview(&docs, as_str(&path), 0, set_param("ghost", 1, "4.25f"))
        .expect_err("an unknown block is refused");
    assert!(missing.contains("unknown_block"), "{missing}");

    let used = document::preview(
        &docs,
        as_str(&path),
        0,
        vec![json!({ "command": "delete_block", "site": 0, "block": "plot" })],
    )
    .expect_err("a block used outside the graph is refused");
    assert!(used.contains("references_outside_graph"), "{used}");
    assert!(used.contains("spans"), "the refusal lists where: {used}");

    let unknown = document::preview(
        &docs,
        as_str(&path),
        0,
        vec![json!({ "command": "rewire", "site": 0 })],
    )
    .expect_err("a command the editor does not have is refused");
    assert!(unknown.contains("rewire"), "{unknown}");

    assert_eq!(text(&path), original, "a refused preview touched the file");
}

#[test]
fn preview_renders_an_inserted_declaration_as_a_pure_addition() {
    let path = temp_copy("hello_world.cpp");
    let docs = Documents::default();
    document::open(&docs, as_str(&path)).expect("open");

    let preview = document::preview(
        &docs,
        as_str(&path),
        0,
        vec![json!({
            "command": "add_block",
            "site": 0,
            "type": "ThrottleBlock",
            "template_args": ["float"],
            "ctor_args": ["\"Second\"", "SPS"],
            "var_name": "throttle2",
        })],
    )
    .expect("preview plans");

    let added: Vec<&str> = preview
        .diff
        .lines()
        .filter(|line| line.starts_with('+'))
        .collect();
    let removed: Vec<&str> = preview
        .diff
        .lines()
        .filter(|line| line.starts_with('-'))
        .collect();
    assert_eq!(
        added,
        vec!["+    ThrottleBlock<float> throttle2(\"Second\", SPS);"]
    );
    assert!(
        removed.is_empty(),
        "an insertion should not read as a rewrite: {removed:?}"
    );
    assert!(
        preview.diff.contains(" auto flowgraph = cler::make_desktop_flowgraph("),
        "the line the block was inserted before stays context: {}",
        preview.diff
    );
}

#[test]
fn a_file_no_command_could_edit_never_reaches_the_model_with_a_tool() {
    let path = temp_copy("hello_world.cpp");
    let docs = Documents::default();
    let sound = document::open(&docs, as_str(&path)).expect("open");
    assert!(
        assistant::actionable(&sound),
        "a parsed, editable file can be proposed against"
    );

    let broken = path.with_file_name("broken.cpp");
    std::fs::write(&broken, "int main() { this is not c++ (\n").expect("write");
    let state = document::open(&docs, as_str(&broken)).expect("open");

    assert!(
        !assistant::actionable(&state),
        "a file with parse errors refuses every command, so the tool is withheld"
    );
    assert!(
        !assistant::request("<ctx/>", "fix it", &[], assistant::actionable(&state))
            .contains(assistant::TOOL_NAME),
        "a withheld tool must not reach the request body"
    );
}

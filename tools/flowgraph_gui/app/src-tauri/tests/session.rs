use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicUsize, Ordering};

use cler_flowgraph_gui::ai_agent;
use cler_flowgraph_gui::document::{self, Documents, NodeMove, Point};
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

#[test]
fn bundled_example_paths_resolve_from_the_application_directory() {
    let target = document::canonical("desktop_examples/hello_world.cpp").expect("example path");
    assert_eq!(
        target,
        corpus("hello_world.cpp").canonicalize().expect("corpus")
    );
    let conflict =
        document::canonical("tools/flowgraph_gui/cler-graph/tests/data/type_conflict.cpp")
            .expect("fixture path");
    assert!(conflict.ends_with("cler-graph/tests/data/type_conflict.cpp"));
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
fn edits_stay_in_the_working_copy_until_save() {
    let path = temp_copy("hello_world.cpp");
    let original = text(&path);
    let docs = Documents::default();

    let opened = document::open(&docs, as_str(&path)).expect("open");
    assert_eq!(opened.revision, 0);
    assert!(!opened.can_undo);
    assert!(!opened.can_redo);
    assert!(!opened.dirty);
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
    assert!(applied.dirty);
    assert_ne!(applied.model.sha256, opened.model.sha256);

    let working = document::working_path(&docs, as_str(&path)).expect("working copy");
    let drafted = text(&working);
    assert_eq!(text(&path), original);
    assert!(drafted.contains("4.25f"));
    assert!(working.starts_with(std::env::temp_dir()));

    let undone = document::undo(&docs, as_str(&path)).expect("undo");
    assert!(!undone.can_undo);
    assert!(undone.can_redo);
    assert!(!undone.dirty);
    assert_eq!(text(&path), original);
    assert_eq!(text(&working), original);
    assert_eq!(undone.model.sha256, opened.model.sha256);

    let redone = document::redo(&docs, as_str(&path)).expect("redo");
    assert!(redone.can_undo);
    assert!(!redone.can_redo);
    assert!(redone.dirty);
    assert_eq!(text(&path), original);
    assert_eq!(text(&working), drafted);

    let saved = document::save(&docs, as_str(&path)).expect("save");
    assert!(!saved.dirty);
    assert_eq!(text(&path), drafted);
}

#[test]
fn a_self_write_is_not_an_external_change() {
    let path = temp_copy("hello_world.cpp");
    let docs = Documents::default();
    document::open(&docs, as_str(&path)).expect("open");
    document::apply(&docs, as_str(&path), 0, set_param("source1", 1, "7.5f")).expect("apply");
    document::save(&docs, as_str(&path)).expect("save");

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
fn close_keeps_the_temporary_draft_for_recovery() {
    let path = temp_copy("hello_world.cpp");
    let original = text(&path);
    let docs = Documents::default();
    document::open(&docs, as_str(&path)).expect("open");
    document::apply(&docs, as_str(&path), 0, set_param("source1", 1, "6.25f")).expect("draft");
    let working = document::working_path(&docs, as_str(&path)).expect("working copy");
    assert!(working.exists());
    document::close(&docs, as_str(&path));
    assert!(working.exists());

    let missing = document::undo(&docs, as_str(&path)).expect_err("the session is gone");
    assert!(missing.contains("no open document"), "{missing}");
    let recovered = document::open(&docs, as_str(&path)).expect("recover");
    assert!(recovered.dirty);
    assert!(recovered.source.contains("6.25f"));
    assert_eq!(text(&path), original);
}

#[test]
fn cfgc_updates_preserve_other_namespaces_and_unknown_fields() {
    let path = temp_copy("hello_world.cpp");
    let docs = Documents::default();
    document::open(&docs, as_str(&path)).expect("open");
    document::store_cache(&docs, as_str(&path), json!({"version": 1, "views": {}})).expect("cache");
    let working = document::working_path(&docs, as_str(&path)).expect("working copy");
    let cache = working.with_extension("cfgc");
    let mut value: Value = serde_json::from_str(&text(&cache)).expect("cfgc json");
    value["build"] = json!({"futureCompiler": "kept"});
    value["futureSection"] = json!({"also": "kept"});
    std::fs::write(&cache, serde_json::to_string_pretty(&value).expect("json")).expect("extend");

    document::close(&docs, as_str(&path));
    document::open(&docs, as_str(&path)).expect("reopen extended cache");
    document::store_cache(
        &docs,
        as_str(&path),
        json!({"version": 1, "activeView": "main"}),
    )
    .expect("update ui only");
    let updated: Value = serde_json::from_str(&text(&cache)).expect("updated cfgc");
    assert_eq!(updated["build"]["futureCompiler"], "kept");
    assert_eq!(updated["futureSection"]["also"], "kept");
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
    assert_eq!(text(&path), original);
    let working = document::working_path(&docs, as_str(&path)).expect("working copy");
    assert_eq!(applied.source, text(&working));

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
    assert_eq!(value["dirty"], false);
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
        ai_agent::actionable(&sound),
        "a parsed, editable file can be proposed against"
    );

    let broken = path.with_file_name("broken.cpp");
    std::fs::write(&broken, "int main() { this is not c++ (\n").expect("write");
    let state = document::open(&docs, as_str(&broken)).expect("open");

    assert!(
        !ai_agent::actionable(&state),
        "a file with parse errors refuses every command, so the tool is withheld"
    );
    assert!(
        !ai_agent::request("<ctx/>", "fix it", &[], ai_agent::actionable(&state), false)
            .contains(ai_agent::TOOL_NAME),
        "a withheld tool must not reach the request body"
    );
}

fn movement(node: &str, from: (f64, f64), to: (f64, f64)) -> NodeMove {
    NodeMove {
        node: node.to_string(),
        from: Point {
            x: from.0,
            y: from.1,
        },
        to: Point { x: to.0, y: to.1 },
    }
}

#[test]
fn source_and_position_actions_share_one_chronological_history() {
    let path = temp_copy("hello_world.cpp");
    let original = text(&path);
    let docs = Documents::default();
    let p = as_str(&path);
    document::open(&docs, p).expect("open");
    document::store_cache(
        &docs,
        p,
        json!({
            "views": {
                "main": {
                    "positions": {
                        "source1": { "x": 10.0, "y": 20.0 },
                        "source2": { "x": 30.0, "y": 40.0 }
                    }
                }
            }
        }),
    )
    .expect("initial positions");

    let edited =
        document::apply(&docs, p, 0, set_param("source1", 1, "4.25f")).expect("source action");
    let moved = document::move_nodes(
        &docs,
        p,
        "main".to_string(),
        vec![
            movement("source1", (10.0, 20.0), (110.0, 120.0)),
            movement("source2", (30.0, 40.0), (130.0, 140.0)),
        ],
    )
    .expect("position action");
    assert_eq!(moved.revision, edited.revision);
    assert!(moved.dirty);
    assert_eq!(
        moved.cache["views"]["main"]["positions"]["source1"]["x"],
        110.0
    );

    let position_undone = document::undo(&docs, p).expect("undo grouped movement");
    assert_eq!(position_undone.revision, edited.revision);
    assert!(position_undone.source.contains("4.25f"));
    assert_eq!(
        position_undone.cache["views"]["main"]["positions"]["source1"]["x"],
        10.0
    );
    assert_eq!(
        position_undone.cache["views"]["main"]["positions"]["source2"]["x"],
        30.0
    );

    let source_undone = document::undo(&docs, p).expect("undo source edit");
    assert_eq!(source_undone.source, original);
    assert!(!source_undone.dirty);
    let source_redone = document::redo(&docs, p).expect("redo source edit");
    assert!(source_redone.source.contains("4.25f"));
    let position_redone = document::redo(&docs, p).expect("redo grouped movement");
    assert_eq!(position_redone.revision, source_redone.revision);
    assert_eq!(
        position_redone.cache["views"]["main"]["positions"]["source2"]["x"],
        130.0
    );
}

#[test]
fn a_position_action_after_undo_discards_the_redo_branch() {
    let path = temp_copy("hello_world.cpp");
    let docs = Documents::default();
    let p = as_str(&path);
    document::open(&docs, p).expect("open");
    document::move_nodes(
        &docs,
        p,
        "main".to_string(),
        vec![movement("source1", (0.0, 0.0), (10.0, 10.0))],
    )
    .expect("move");
    let undone = document::undo(&docs, p).expect("undo");
    assert!(undone.can_redo);
    let branched = document::move_nodes(
        &docs,
        p,
        "main".to_string(),
        vec![movement("source2", (0.0, 0.0), (20.0, 20.0))],
    )
    .expect("branch");
    assert!(!branched.can_redo);
    assert!(document::redo(&docs, p)
        .expect_err("redo was discarded")
        .contains("nothing_to_redo"));
}

#[test]
fn disk_drift_allows_position_undo_before_refusing_source_undo() {
    let path = temp_copy("hello_world.cpp");
    let docs = Documents::default();
    let p = as_str(&path);
    document::open(&docs, p).expect("open");
    document::apply(&docs, p, 0, set_param("source1", 1, "4.25f")).expect("source action");
    document::move_nodes(
        &docs,
        p,
        "main".to_string(),
        vec![movement("source1", (0.0, 0.0), (10.0, 10.0))],
    )
    .expect("position action");

    std::fs::write(&path, "external\n").expect("external edit");
    assert!(document::note_disk_event(&docs, &path));
    let undone = document::undo(&docs, p).expect("position undo");
    assert!(undone.can_undo);
    assert!(document::undo(&docs, p)
        .expect_err("source undo must refuse disk drift")
        .contains("changed on disk"));
}

#[test]
fn nothing_but_a_source_change_may_overwrite_an_unparsed_draft() {
    let path = temp_copy("hello_world.cpp");
    let docs = Documents::default();
    let p = as_str(&path);
    let opened = document::open(&docs, p).expect("open");
    let working = document::working_path(&docs, p).expect("working copy");
    document::move_nodes(
        &docs,
        p,
        "main".to_string(),
        vec![movement("source1", (0.0, 0.0), (10.0, 10.0))],
    )
    .expect("position action");

    let typed = format!("{}\nint unfinished( {{\n", opened.source);
    let outcome = document::edit(&docs, p, opened.revision, typed.clone()).expect("typed text");
    assert!(outcome.unparsed);
    assert_eq!(text(&working), typed);

    document::undo(&docs, p).expect("undo the movement");
    assert_eq!(
        text(&working),
        typed,
        "a position undo must leave the typed draft alone"
    );
    document::apply(&docs, p, opened.revision, Vec::new()).expect("no-op transaction");
    assert_eq!(
        text(&working),
        typed,
        "a transaction that splices nothing must not rewrite the draft"
    );
}

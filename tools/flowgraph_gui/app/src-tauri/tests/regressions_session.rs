use std::os::unix::fs::PermissionsExt;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Arc;

use cler_flowgraph_gui::document::{self, DocumentState, Documents};
use serde_json::{json, Value};

static COUNTER: AtomicUsize = AtomicUsize::new(0);

fn corpus(name: &str) -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("../../../../desktop_examples")
        .join(name)
}

fn scratch() -> PathBuf {
    let unique = COUNTER.fetch_add(1, Ordering::Relaxed);
    let dir = std::env::temp_dir().join(format!(
        "cler-gui-regressions-{}-{unique}",
        std::process::id()
    ));
    std::fs::create_dir_all(&dir).expect("temp directory");
    dir
}

fn temp_copy(name: &str) -> PathBuf {
    let target = scratch().join(name);
    std::fs::copy(corpus(name), &target).expect("corpus copy");
    target
}

fn text(path: &Path) -> String {
    std::fs::read_to_string(path).expect("readable document")
}

fn as_str(path: &Path) -> &str {
    path.to_str().expect("utf-8 path")
}

fn digest(bytes: &str) -> String {
    format!(
        "{:x}",
        <sha2::Sha256 as sha2::Digest>::digest(bytes.as_bytes())
    )
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

fn chmod(path: &Path, mode: u32) {
    std::fs::set_permissions(path, std::fs::Permissions::from_mode(mode)).expect("chmod");
}

fn siblings(path: &Path) -> Vec<String> {
    let parent = path.parent().expect("parent directory");
    let mut out: Vec<String> = std::fs::read_dir(parent)
        .expect("readdir")
        .map(|entry| {
            entry
                .expect("entry")
                .file_name()
                .to_string_lossy()
                .to_string()
        })
        .collect();
    out.sort();
    out
}

fn edge_labels(state: &DocumentState) -> Vec<String> {
    state.model.model.sites[0]
        .edges
        .iter()
        .map(|edge| format!("{}->{}", edge.from, edge.to))
        .collect()
}

fn state_of(docs: &Documents, path: &Path) -> DocumentState {
    document::open(docs, as_str(path)).expect("session state")
}

fn poison(docs: &Arc<Documents>) {
    let hijack = Arc::clone(docs);
    let previous = std::panic::take_hook();
    std::panic::set_hook(Box::new(|_| {}));
    let _ = std::thread::spawn(move || {
        let _guard = hijack.lock().expect("lock");
        panic!("anything at all, while the store lock is held");
    })
    .join();
    std::panic::set_hook(previous);
}

#[test]
fn a_failed_save_leaves_the_draft_and_disk_distinct() {
    let path = temp_copy("hello_world.cpp");
    let original = text(&path);
    let docs = Documents::default();

    document::open(&docs, as_str(&path)).expect("open");

    chmod(&path, 0o444);
    let drafted = document::apply(&docs, as_str(&path), 0, set_param("source1", 1, "4.25f"))
        .expect("the draft does not write the source");
    let refusal = document::save(&docs, as_str(&path))
        .expect_err("saving a read-only file is refused");
    assert!(refusal.contains("cannot write"), "{refusal}");
    assert_eq!(text(&path), original);

    let after = state_of(&docs, &path);
    assert_eq!(after.revision, drafted.revision, "save adds no revision");
    assert!(after.can_undo);
    assert!(after.dirty);
    assert_ne!(after.model.sha256, digest(&original));
    assert_eq!(
        siblings(&path),
        vec!["hello_world.cpp"],
        "the failed write leaves no stale temp sibling"
    );

    chmod(&path, 0o644);
}

#[test]
fn a_failed_save_preserves_the_draft_for_the_next_edit() {
    let path = temp_copy("hello_world.cpp");
    let docs = Documents::default();
    document::open(&docs, as_str(&path)).expect("open");

    chmod(&path, 0o444);
    let first = document::apply(&docs, as_str(&path), 0, set_param("source1", 1, "111.0f"))
        .expect("draft");
    document::save(&docs, as_str(&path)).expect_err("save refused");
    assert!(!text(&path).contains("111.0f"));

    chmod(&path, 0o644);
    let ok = document::apply(
        &docs,
        as_str(&path),
        first.revision,
        set_param("source2", 1, "222.0f"),
    )
    .expect("second edit extends the draft");
    assert_eq!(ok.revision, 2);
    document::save(&docs, as_str(&path)).expect("save both edits");

    let on_disk = text(&path);
    assert!(on_disk.contains("222.0f"), "the requested edit");
    assert!(on_disk.contains("111.0f"), "the earlier draft survives: {on_disk}");
}

#[test]
fn close_after_a_failed_save_recovers_the_temporary_draft() {
    let path = temp_copy("hello_world.cpp");
    let original = text(&path);
    let docs = Documents::default();
    document::open(&docs, as_str(&path)).expect("open");

    chmod(&path, 0o444);
    document::apply(&docs, as_str(&path), 0, set_param("source1", 1, "9.75f"))
        .expect("draft");
    document::save(&docs, as_str(&path)).expect_err("save refused");
    chmod(&path, 0o644);

    document::close(&docs, as_str(&path));
    let reopened = document::open(&docs, as_str(&path)).expect("reopen");
    assert!(reopened.dirty);
    assert!(reopened.source.contains("9.75f"));
    assert_eq!(text(&path), original, "the source file stays untouched");
}

#[test]
fn a_read_only_directory_refuses_save_without_losing_the_draft() {
    let path = temp_copy("hello_world.cpp");
    let original = text(&path);
    let dir = path.parent().expect("parent").to_path_buf();
    let docs = Documents::default();
    document::open(&docs, as_str(&path)).expect("open");

    chmod(&dir, 0o555);
    let mut revision = 0;
    for value in ["1.5f", "2.5f", "3.5f"] {
        let drafted = document::apply(
            &docs,
            as_str(&path),
            revision,
            set_param("source1", 1, value),
        )
        .expect("draft");
        revision = drafted.revision;
        let refusal = document::save(&docs, as_str(&path)).expect_err("every save fails");
        assert!(refusal.contains("cannot write"), "{refusal}");
    }
    chmod(&dir, 0o755);

    assert_eq!(text(&path), original, "disk never moves");
    let state = state_of(&docs, &path);
    assert_eq!(state.revision, revision, "save adds no phantom revisions");
    assert!(state.can_undo);
    assert!(state.dirty);
    assert!(state.model.model.sites[0]
        .blocks
        .iter()
        .any(|b| b.ctor_args.iter().any(|a| a.text == "3.5f")));
    assert_eq!(siblings(&path), vec!["hello_world.cpp"]);
}

#[test]
fn a_symlinked_temp_sibling_cannot_redirect_the_write() {
    let path = temp_copy("hello_world.cpp");
    let victim = path.with_file_name("victim.txt");
    std::fs::write(&victim, "precious user data\n").expect("victim");

    let docs = Documents::default();
    document::open(&docs, as_str(&path)).expect("open");

    let planted = path.with_file_name(".hello_world.cpp.cler-gui.tmp");
    std::os::unix::fs::symlink(&victim, &planted).expect("plant a symlink at the old temp name");

    document::apply(&docs, as_str(&path), 0, set_param("source1", 1, "6.5f"))
        .expect("draft");
    document::save(&docs, as_str(&path)).expect("the save succeeds");

    assert_eq!(
        text(&victim),
        "precious user data\n",
        "the unrelated file is untouched"
    );
    assert!(text(&path).contains("6.5f"), "the document has the edit");
    assert!(
        !std::fs::symlink_metadata(&path)
            .expect("meta")
            .file_type()
            .is_symlink(),
        "the document is still a regular file"
    );
    assert!(
        !siblings(&path)
            .iter()
            .any(|name| name.ends_with(".cler-gui.tmp") && name != ".hello_world.cpp.cler-gui.tmp"),
        "no temp file is left behind"
    );
}

#[test]
fn a_no_op_transaction_does_not_inflate_the_history() {
    let path = temp_copy("hello_world.cpp");
    let docs = Documents::default();
    let p = as_str(&path);
    document::open(&docs, p).expect("open");
    document::apply(&docs, p, 0, set_param("source1", 1, "3.0f")).expect("apply");
    let before = document::undo(&docs, p).expect("undo");
    assert!(!before.can_undo && before.can_redo);

    let idempotent = document::apply(&docs, p, before.revision, set_param("source1", 1, "1.0f"))
        .expect("no-op applies");
    assert_eq!(idempotent.revision, before.revision, "revision unchanged");
    assert!(!idempotent.can_undo, "history not inflated");
    assert!(idempotent.can_redo, "and the redo branch survives");
    let redone = document::redo(&docs, p).expect("redo still available, as advertised");

    let empty = document::apply(&docs, p, redone.revision, vec![]).expect("empty transaction");
    assert!(!empty.can_redo);
    assert!(empty.can_undo);
}

#[test]
fn a_refused_transaction_does_not_touch_the_history() {
    let path = temp_copy("hello_world.cpp");
    let original = text(&path);
    let docs = Documents::default();
    let p = as_str(&path);
    document::open(&docs, p).expect("open");
    let applied = document::apply(&docs, p, 0, set_param("source1", 1, "3.0f")).expect("apply");

    for commands in [
        set_param("nosuchblock", 0, "1.0f"),
        set_param("source1", 99, "1.0f"),
        set_param("source1", 1, "1.0f +"),
    ] {
        document::apply(&docs, p, applied.revision, commands).expect_err("refused");
    }

    let state = state_of(&docs, &path);
    assert_eq!(state.revision, 1);
    assert!(state.can_undo && !state.can_redo);
    document::undo(&docs, p).expect("undo still works");
    assert_eq!(text(&path), original);
}

#[test]
fn the_history_flags_track_the_session_through_interleavings() {
    let path = temp_copy("hello_world.cpp");
    let docs = Documents::default();
    let p = as_str(&path);
    let mut state = document::open(&docs, p).expect("open");

    let probe = |state: &DocumentState, docs: &Documents, p: &str| {
        if !state.can_undo {
            let err = document::undo(docs, p).expect_err("the flag says no undo");
            assert!(err.contains("nothing_to_undo"), "{err}");
        }
        if !state.can_redo {
            let err = document::redo(docs, p).expect_err("the flag says no redo");
            assert!(err.contains("nothing_to_redo"), "{err}");
        }
    };

    probe(&state, &docs, p);
    state =
        document::apply(&docs, p, state.revision, set_param("source1", 1, "2.0f")).expect("apply");
    probe(&state, &docs, p);
    state =
        document::apply(&docs, p, state.revision, set_param("source2", 1, "21.0f")).expect("apply");
    probe(&state, &docs, p);
    state = document::undo(&docs, p).expect("undo");
    probe(&state, &docs, p);
    state =
        document::apply(&docs, p, state.revision, set_param("source2", 1, "22.0f")).expect("apply");
    assert!(!state.can_redo);
    probe(&state, &docs, p);
    document::undo(&docs, p).expect("undo");
    state = document::undo(&docs, p).expect("undo");
    assert!(!state.can_undo);
    probe(&state, &docs, p);
    state = document::reload(&docs, p).expect("reload of identical bytes");
    assert!(!state.can_undo && state.can_redo);
    probe(&state, &docs, p);

    std::fs::write(&path, format!("{}// elsewhere\n", text(&path))).expect("external");
    state = document::reload(&docs, p).expect("reload of new bytes");
    assert!(!state.can_undo && !state.can_redo);
    probe(&state, &docs, p);
}

#[test]
fn undo_after_a_failed_save_returns_to_the_last_saved_bytes() {
    let path = temp_copy("hello_world.cpp");
    let original = text(&path);
    let docs = Documents::default();
    let p = as_str(&path);
    document::open(&docs, p).expect("open");

    let good = document::apply(&docs, p, 0, set_param("source1", 1, "5.0f")).expect("v1");
    assert_eq!(good.revision, 1);
    document::save(&docs, p).expect("save v1");
    let v1 = text(&path);

    chmod(&path, 0o444);
    document::apply(&docs, p, good.revision, set_param("source2", 1, "50.0f"))
        .expect("draft v2");
    document::save(&docs, p).expect_err("save refused");
    chmod(&path, 0o644);

    let state = state_of(&docs, &path);
    assert!(state.can_undo);
    assert!(!state.can_redo);
    assert!(state.dirty);
    assert!(state.source.contains("50.0f"));

    let undone = document::undo(&docs, p).expect("undo");
    assert_eq!(text(&path), v1);
    assert_eq!(undone.source, v1);
    assert!(!undone.dirty);
    assert!(undone.can_undo);
    assert!(undone.can_redo);
    let redone = document::redo(&docs, p).expect("redo");
    assert_eq!(text(&path), v1);
    assert!(redone.dirty);
    assert!(redone.source.contains("50.0f"));
    assert!(!original.contains("5.0f"));
}

#[test]
fn a_reload_that_changes_nothing_keeps_the_undo_history() {
    let path = temp_copy("hello_world.cpp");
    let docs = Documents::default();
    let p = as_str(&path);
    document::open(&docs, p).expect("open");
    document::apply(&docs, p, 0, set_param("source1", 1, "2.0f")).expect("apply");
    let second = document::apply(&docs, p, 1, set_param("source2", 1, "3.0f")).expect("apply");
    document::save(&docs, p).expect("save");
    let before = text(&path);

    let reloaded = document::reload(&docs, p).expect("reload of identical bytes");
    assert_eq!(text(&path), before, "nothing changed on disk");
    assert_eq!(reloaded.revision, second.revision, "not even a revision");
    assert!(reloaded.can_undo, "two edits of history survive");
    document::undo(&docs, p).expect("history intact");
    document::undo(&docs, p).expect("history intact");
}

#[test]
fn a_stale_base_revision_is_rejected_instead_of_cutting_the_wrong_wire() {
    let path = temp_copy("hello_world.cpp");
    let docs = Documents::default();
    let p = as_str(&path);

    let opened = document::open(&docs, p).expect("open");
    assert_eq!(
        edge_labels(&opened),
        vec![
            "source1->adder",
            "source2->adder",
            "adder->throttle",
            "throttle->plot"
        ]
    );
    let stale_gesture = vec![json!({"command": "disconnect", "site": 0, "edge": 1})];

    let outside = text(&path).replace("        cler::BlockRunner(&source1, &adder.in[0]),\n", "");
    std::fs::write(&path, &outside).expect("external edit");
    let reloaded = document::reload(&docs, p).expect("reload");
    assert!(reloaded.revision > opened.revision);

    let refusal = document::apply(&docs, p, opened.revision, stale_gesture.clone())
        .expect_err("the stale gesture is rejected");
    assert!(refusal.contains("revision_mismatch"), "{refusal}");
    assert_eq!(text(&path), outside, "the file is untouched");
    assert_eq!(
        edge_labels(&state_of(&docs, &path)),
        vec!["source2->adder", "adder->throttle", "throttle->plot"]
    );

    let applied =
        document::apply(&docs, p, reloaded.revision, stale_gesture).expect("recomputed gesture");
    assert_eq!(
        edge_labels(&applied),
        vec!["source2->adder", "throttle->plot"],
        "against the right revision the same index cuts the intended wire"
    );
}

#[test]
fn a_command_cannot_carry_its_own_version_or_base_revision() {
    let path = temp_copy("hello_world.cpp");
    let original = text(&path);
    let docs = Documents::default();
    document::open(&docs, as_str(&path)).expect("open");

    for extra in [json!({"version": "0.0.1"}), json!({"base_revision": 99})] {
        let mut command = json!({
            "command": "set_param", "site": 0, "block": "source1",
            "ctor_arg_index": 1, "new_text": "8.0f",
        });
        for (key, value) in extra.as_object().expect("object") {
            command
                .as_object_mut()
                .expect("object")
                .insert(key.clone(), value.clone());
        }
        let err = document::apply(&docs, as_str(&path), 0, vec![command]).expect_err("rejected");
        assert!(err.contains("unknown field"), "{err}");
    }
    assert_eq!(text(&path), original);
    assert_eq!(state_of(&docs, &path).revision, 0);
}

#[test]
fn the_external_change_flag_clears_when_the_bytes_come_back() {
    let path = temp_copy("hello_world.cpp");
    let docs = Documents::default();
    let p = as_str(&path);
    document::open(&docs, p).expect("open");
    let ours = text(&path);
    let canonical = path.canonicalize().expect("canonical");

    std::fs::write(&path, format!("{ours}// tourist\n")).expect("external edit");
    assert!(document::note_disk_event(&docs, &canonical), "reported");
    assert!(state_of(&docs, &path).external_change);

    std::fs::write(&path, &ours).expect("external undo, byte for byte");
    assert!(
        !document::note_disk_event(&docs, &canonical),
        "no event for bytes that came back"
    );

    let state = state_of(&docs, &path);
    assert!(!state.external_change, "the conflict is resolved");
    document::apply(&docs, p, state.revision, set_param("source1", 1, "4.0f"))
        .expect("and editing is allowed again");
}

#[test]
fn a_deleted_document_is_reported_as_an_external_change() {
    let path = temp_copy("hello_world.cpp");
    let docs = Documents::default();
    document::open(&docs, as_str(&path)).expect("open");
    let canonical = path.canonicalize().expect("canonical");

    std::fs::remove_file(&path).expect("external delete");
    assert!(
        document::note_disk_event(&docs, &canonical),
        "the deletion is reported"
    );
    assert!(
        !document::note_disk_event(&docs, &canonical),
        "and reported only once"
    );
    assert!(
        document::open(&docs, as_str(&canonical))
            .expect("the session survives")
            .external_change
    );

    let err = document::apply(
        &docs,
        as_str(&canonical),
        0,
        set_param("source1", 1, "1.5f"),
    )
    .expect_err("editing a deleted file fails");
    assert!(err.contains("cannot read"), "{err}");
}

#[test]
fn editing_one_document_does_not_flag_its_neighbour() {
    let dir = scratch();
    let a = dir.join("hello_world.cpp");
    let b = dir.join("fm_receiver.cpp");
    std::fs::copy(corpus("hello_world.cpp"), &a).expect("copy a");
    std::fs::copy(corpus("fm_receiver.cpp"), &b).expect("copy b");

    let docs = Documents::default();
    document::open(&docs, as_str(&a)).expect("open a");
    document::open(&docs, as_str(&b)).expect("open b");

    document::apply(&docs, as_str(&a), 0, set_param("source1", 1, "12.0f")).expect("edit a");
    for path in [&a, &b] {
        let canonical = path.canonicalize().expect("canonical");
        assert!(!document::note_disk_event(&docs, &canonical));
    }
    assert!(!state_of(&docs, &b).external_change);
    assert!(!document::note_disk_event(
        &docs,
        &dir.join(".hello_world.cpp.0123456789abcdef.cler-gui.tmp")
    ));
}

#[test]
fn replacing_the_document_with_a_symlink_keeps_the_session_reachable() {
    let path = temp_copy("hello_world.cpp");
    let docs = Documents::default();
    let p = as_str(&path);
    document::open(&docs, p).expect("open");
    let edited = document::apply(&docs, p, 0, set_param("source1", 1, "13.0f")).expect("edit");

    let moved = path.with_file_name("real_hello_world.cpp");
    std::fs::rename(&path, &moved).expect("move aside");
    std::os::unix::fs::symlink(&moved, &path).expect("symlink in its place");

    let same = document::open(&docs, p).expect("the same session, not a second one");
    assert_eq!(same.revision, edited.revision);
    assert!(same.can_undo);

    let applied =
        document::apply(&docs, p, same.revision, set_param("source1", 1, "14.0f")).expect("edit");
    assert!(applied.can_undo);
    document::undo(&docs, p).expect("undo");
    document::reload(&docs, p).expect("reload");

    document::close(&docs, p);
    let err = document::undo(&docs, p).expect_err("close removed the session");
    assert!(err.contains("no open document"), "{err}");

    let second = document::open(&docs, p).expect("a fresh session on the current bytes");
    assert_eq!(second.revision, 0);
    assert!(!second.can_undo);
}

#[test]
fn two_path_spellings_share_one_session() {
    let path = temp_copy("hello_world.cpp");
    let dir = path.parent().expect("parent");
    let spelling_a = path.display().to_string();
    let spelling_b = dir.join("./sub/../hello_world.cpp").display().to_string();
    std::fs::create_dir_all(dir.join("sub")).expect("sub");

    let docs = Documents::default();
    let first = document::open(&docs, &spelling_a).expect("open a");
    let second = document::open(&docs, &spelling_b).expect("open b");
    assert_eq!(first.path, second.path);

    document::apply(&docs, &spelling_a, 0, set_param("source1", 1, "15.0f")).expect("edit via a");
    let via_b = document::open(&docs, &spelling_b).expect("open b again");
    assert_eq!(via_b.revision, 1);
    assert!(via_b.can_undo);
    document::undo(&docs, &spelling_b).expect("undo via b");
}

#[test]
fn reopening_a_document_revalidates_against_disk() {
    let path = temp_copy("hello_world.cpp");
    let docs = Documents::default();
    let p = as_str(&path);
    let opened = document::open(&docs, p).expect("open");

    std::fs::write(&path, format!("{}// missed\n", text(&path))).expect("external");

    let again = document::open(&docs, p).expect("reopen");
    assert!(again.external_change, "the UI is told the document drifted");
    assert_eq!(
        again.model.sha256, opened.model.sha256,
        "the session bytes are still the session's until the user reloads"
    );

    let reloaded = document::reload(&docs, p).expect("reload");
    assert!(!reloaded.external_change);
    assert_eq!(reloaded.model.sha256, digest(&text(&path)));
}

#[test]
fn a_poisoned_lock_does_not_brick_the_commands() {
    let path = temp_copy("hello_world.cpp");
    let docs = Arc::new(Documents::default());
    document::open(&docs, as_str(&path)).expect("open");
    poison(&docs);

    let state = document::open(&docs, as_str(&path)).expect("open still works");
    document::apply(
        &docs,
        as_str(&path),
        state.revision,
        set_param("source1", 1, "1.5f"),
    )
    .expect("apply still works");
    document::undo(&docs, as_str(&path)).expect("undo still works");
    document::redo(&docs, as_str(&path)).expect("redo still works");
    document::reload(&docs, as_str(&path)).expect("reload still works");
    document::parse_file(&docs, as_str(&path)).expect("parse_file still works");
    document::close(&docs, as_str(&path));
}

#[test]
fn a_poisoned_lock_does_not_deafen_the_watcher() {
    let path = temp_copy("hello_world.cpp");
    let docs = Arc::new(Documents::default());
    document::open(&docs, as_str(&path)).expect("open");
    let canonical = path.canonicalize().expect("canonical");
    poison(&docs);

    std::fs::write(&path, "// hostile takeover\n").expect("external write");
    assert!(
        document::note_disk_event(&docs, &canonical),
        "the external edit is still reported"
    );
}

#[test]
fn concurrent_applies_against_one_base_revision_admit_exactly_one() {
    let dir = scratch();
    let a = dir.join("hello_world.cpp");
    let b = dir.join("fm_receiver.cpp");
    std::fs::copy(corpus("hello_world.cpp"), &a).expect("copy a");
    std::fs::copy(corpus("fm_receiver.cpp"), &b).expect("copy b");

    let docs = Arc::new(Documents::default());
    document::open(&docs, as_str(&a)).expect("open a");
    document::open(&docs, as_str(&b)).expect("open b");

    let mut threads = Vec::new();
    for index in 0..8 {
        let docs = Arc::clone(&docs);
        let a = a.clone();
        threads.push(std::thread::spawn(move || {
            document::apply(
                &docs,
                a.to_str().expect("utf-8 path"),
                0,
                set_param("source1", 1, &format!("{}.0f", index + 30)),
            )
            .is_ok()
        }));
    }
    let ok = threads
        .into_iter()
        .map(|t| t.join().expect("no panic"))
        .filter(|ok| *ok)
        .count();

    assert_eq!(ok, 1, "the revision guard admits one writer");
    let state = state_of(&docs, &a);
    assert_eq!(state.revision, 1);
    assert!(!state.model.has_errors);
    assert!(!document::note_disk_event(
        &docs,
        &a.canonicalize().expect("canonical")
    ));
}

#[test]
fn an_external_write_racing_the_rename_is_never_swallowed() {
    let path = temp_copy("hello_world.cpp");
    let pristine = text(&path);
    let docs = Arc::new(Documents::default());
    let p = as_str(&path);
    document::open(&docs, p).expect("open");

    const MARKER: &str = "// an edit made in another editor\n";
    let mut clobbered = 0usize;
    let mut attempts = 0usize;

    for round in 0..600u64 {
        std::fs::write(&path, &pristine).expect("reset");
        let state = document::reload(&docs, p).expect("resync");

        let writer_path = path.clone();
        let hostile = format!("{pristine}{MARKER}");
        let spin = round * 40;
        let writer = std::thread::spawn(move || {
            for _ in 0..spin {
                std::hint::spin_loop();
            }
            std::fs::write(&writer_path, &hostile).expect("external write");
        });

        let applied = document::apply(&docs, p, state.revision, set_param("source1", 1, "77.0f"));
        writer.join().expect("writer");
        attempts += 1;

        let after = text(&path);
        if applied.is_ok() && !after.contains(MARKER) && after.contains("77.0f") {
            clobbered += 1;
        }
    }

    assert_eq!(
        clobbered, 0,
        "an external write was swallowed in {attempts} races"
    );
}

#[test]
fn input_abuse_is_rejected_without_touching_the_file() {
    let path = temp_copy("hello_world.cpp");
    let original = text(&path);
    let docs = Documents::default();
    let p = as_str(&path);
    document::open(&docs, p).expect("open");

    let empty = document::apply(&docs, p, 0, vec![]).expect("empty is a no-op");
    assert_eq!(empty.revision, 0);
    assert!(!empty.can_undo && !empty.can_redo);

    let rejects = vec![
        json!({"command": "teleport_block", "site": 0}),
        json!({"command": "set_param", "site": "zero", "block": "source1",
               "ctor_arg_index": 1, "new_text": "1.0f"}),
        json!({"command": "set_param", "site": -1, "block": "source1",
               "ctor_arg_index": 1, "new_text": "1.0f"}),
        json!({"command": "set_param", "site": 0, "block": "source1",
               "ctor_arg_index": 1}),
        json!("set_param"),
        json!(null),
        json!([1, 2, 3]),
    ];
    for command in rejects {
        let err = document::apply(&docs, p, 0, vec![command.clone()]).expect_err("rejected");
        assert!(!err.is_empty(), "{command}");
    }

    let mut nested = json!(1);
    for _ in 0..120 {
        nested = json!([nested]);
    }
    document::apply(&docs, p, 0, vec![nested]).expect_err("nested rubbish rejected");
    let deep_text = format!("{}1{}", "[".repeat(500), "]".repeat(500));
    serde_json::from_str::<Vec<Value>>(&deep_text).expect_err("recursion limit");

    let many: Vec<Value> = (0..10_000)
        .map(|_| set_param("source1", 1, "1.5f").remove(0))
        .collect();
    let err = document::apply(&docs, p, 0, many).expect_err("overlapping splices");
    assert!(err.contains("overlapping_splices"), "{err}");

    let unknown: Vec<Value> = (0..10_000)
        .map(|_| json!({"command": "disconnect", "site": 0, "edge": 99}))
        .collect();
    document::apply(&docs, p, 0, unknown).expect_err("index out of range");

    assert_eq!(text(&path), original);
    let state = state_of(&docs, &path);
    assert_eq!(state.revision, 0);
    assert!(!state.can_undo);
    assert_eq!(siblings(&path), vec!["hello_world.cpp"]);
}

#[test]
fn lifecycle_misuse_is_graceful() {
    let path = temp_copy("hello_world.cpp");
    let docs = Documents::default();
    let p = as_str(&path);

    let err = document::reload(&docs, p).expect_err("no session");
    assert!(err.contains("no open document"), "{err}");

    document::open(&docs, p).expect("open");
    document::close(&docs, p);
    for outcome in [
        document::apply(&docs, p, 0, set_param("source1", 1, "1.0f")).map(|_| ()),
        document::undo(&docs, p).map(|_| ()),
        document::redo(&docs, p).map(|_| ()),
        document::reload(&docs, p).map(|_| ()),
    ] {
        assert!(outcome.expect_err("gone").contains("no open document"));
    }
    assert!(
        document::close(&docs, p).is_none(),
        "closing twice unwatches nothing"
    );

    let dir = path.parent().expect("parent").display().to_string();
    assert!(document::open(&docs, &dir)
        .expect_err("directory")
        .contains("cannot read"));

    assert!(document::open(&docs, "/nope/nothing/here.cpp")
        .expect_err("missing")
        .contains("cannot resolve"));

    let binary = path.with_file_name("binary.cpp");
    std::fs::write(&binary, [0xff, 0xfe, 0x00, 0x01, 0x80]).expect("binary");
    assert!(document::open(&docs, as_str(&binary))
        .expect_err("not utf-8")
        .contains("cannot read"));

    let empty = path.with_file_name("empty.cpp");
    std::fs::write(&empty, "").expect("empty file");
    let opened = document::open(&docs, as_str(&empty)).expect("empty parses");
    assert!(opened.model.model.sites.is_empty());
    assert!(
        document::apply(&docs, as_str(&empty), 0, set_param("x", 0, "1"))
            .expect_err("no site")
            .contains("unknown_site")
    );
}

#[test]
fn close_hands_the_document_back_to_disk_by_design() {
    let path = temp_copy("hello_world.cpp");
    let docs = Documents::default();
    let p = as_str(&path);
    document::open(&docs, p).expect("open");
    let hijacked = format!("{}// elsewhere\n", text(&path));
    std::fs::write(&path, &hijacked).expect("external");
    assert!(document::note_disk_event(
        &docs,
        &path.canonicalize().expect("canonical")
    ));

    assert!(
        document::close(&docs, p).is_some(),
        "the last document in the directory releases the watch"
    );
    let reopened = document::open(&docs, p).expect("reopen");
    assert!(
        !reopened.external_change,
        "after close the disk is the document; reopening baselines on it"
    );
    assert!(!reopened.can_undo);
    assert_eq!(reopened.model.sha256, digest(&hijacked));
    document::apply(
        &docs,
        p,
        reopened.revision,
        set_param("source1", 1, "16.0f"),
    )
    .expect("edit allowed");
    assert!(text(&path).contains("elsewhere"));
}

#[test]
fn parse_file_serves_the_open_session() {
    let path = temp_copy("hello_world.cpp");
    let docs = Documents::default();
    let p = as_str(&path);
    let opened = document::open(&docs, p).expect("open");

    std::fs::write(&path, format!("{}// elsewhere\n", text(&path))).expect("external");

    let served: Value =
        serde_json::from_str(&document::parse_file(&docs, p).expect("parse_file")).expect("json");
    assert_eq!(
        served["sha256"].as_str(),
        Some(opened.model.sha256.as_str()),
        "one answer for one path: the session's"
    );

    document::close(&docs, p);
    let from_disk: Value =
        serde_json::from_str(&document::parse_file(&docs, p).expect("parse_file")).expect("json");
    assert_eq!(
        from_disk["sha256"].as_str(),
        Some(digest(&text(&path)).as_str()),
        "with no session open, the disk is the document"
    );
}

use std::path::{Path, PathBuf};
use std::process::Output;

use cler_graph::model::{Reason, Site};
use cler_graph::{ApplyError, Command, DocumentSession, Transaction, SCHEMA_VERSION};

const CORPUS: &str = "../../../desktop_examples";

fn source(name: &str) -> String {
    let path = format!("{CORPUS}/{name}");
    std::fs::read_to_string(&path).unwrap_or_else(|e| panic!("{path}: {e}"))
}

fn session(name: &str) -> DocumentSession {
    DocumentSession::load(source(name)).unwrap_or_else(|e| panic!("{name}: {e}"))
}

fn loaded(text: &str) -> DocumentSession {
    DocumentSession::load(text).expect("fixture parses")
}

fn tx(base_revision: u64, commands: Vec<Command>) -> Transaction {
    Transaction {
        version: SCHEMA_VERSION.to_string(),
        base_revision,
        commands,
    }
}

fn json(base_revision: u64, commands: Vec<Command>) -> String {
    serde_json::to_string(&tx(base_revision, commands)).expect("transaction serializes")
}

fn only_site(session: &DocumentSession) -> Site {
    let mut model = session.parse();
    assert!(!model.sites.is_empty(), "expected a graph site");
    model.sites.remove(0)
}

fn set_param(block: &str, index: usize, text: &str) -> Command {
    Command::SetParam {
        site: 0,
        block: block.to_string(),
        ctor_arg_index: index,
        new_text: text.to_string(),
    }
}

fn throttle(var: &str, name: &str) -> Command {
    Command::AddBlock {
        site: 0,
        type_name: "ThrottleBlock".to_string(),
        template_args: vec!["float".to_string()],
        ctor_args: vec![format!("\"{name}\""), "1000".to_string()],
        var_name: var.to_string(),
    }
}

#[test]
fn s1_delete_block_removes_the_wire_that_feeds_the_block() {
    let mut session = session("hello_world.cpp");
    session
        .apply(tx(
            0,
            vec![Command::DeleteBlock {
                site: 0,
                block: "throttle".to_string(),
            }],
        ))
        .expect("throttle is only used by the graph");
    let after = session.source();
    for survivor in ["&throttle.in", "&throttle", "> throttle("] {
        assert!(
            !after.contains(survivor),
            "{survivor} survived delete_block:\n{after}"
        );
    }
    assert!(only_site(&session).block("throttle").is_none());
    assert!(!session.has_errors());
}

#[test]
fn s1_delete_block_removes_a_pure_sink_and_its_wire() {
    let fixture = r#"#include "cler.hpp"
int main() {
    SourceBlock source("Source");
    SinkBlock sink("Sink");

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &sink.in),
        cler::BlockRunner(&sink)
    );
    flowgraph.run();
}
"#;
    let mut session = loaded(fixture);
    session
        .apply(tx(
            0,
            vec![Command::DeleteBlock {
                site: 0,
                block: "sink".to_string(),
            }],
        ))
        .expect("the sink and its wire go together");
    assert!(!session.source().contains("sink"));
    assert!(!session.has_errors());
}

#[test]
fn s14_delete_block_sees_a_reference_inside_a_macro_body() {
    let fixture = r#"#include "cler.hpp"
#define DRAW_IT plot.render()
int main() {
    SourceBlock source("Source");
    PlotBlock plot("Plot");

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &plot.in),
        cler::BlockRunner(&plot)
    );
    flowgraph.run();
    DRAW_IT;
}
"#;
    let mut session = loaded(fixture);
    let before = session.source().to_string();
    let error = session
        .apply(tx(
            0,
            vec![Command::DeleteBlock {
                site: 0,
                block: "plot".to_string(),
            }],
        ))
        .expect_err("the macro body still uses plot");
    assert!(matches!(error, ApplyError::ReferencesOutsideGraph { .. }));
    assert_eq!(session.source(), before);
}

#[test]
fn s15_delete_block_ignores_a_same_name_in_another_function() {
    let fixture = r#"#include "cler.hpp"
void other() {
    int sink = 3;
    sink += 1;
}
int main() {
    SourceBlock source("Source");
    SinkBlock sink("Sink");

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source),
        cler::BlockRunner(&sink)
    );
    flowgraph.run();
}
"#;
    let mut session = loaded(fixture);
    session
        .apply(tx(
            0,
            vec![Command::DeleteBlock {
                site: 0,
                block: "sink".to_string(),
            }],
        ))
        .expect("an unrelated local in another function is not a reference");
    assert!(session.source().contains("int sink = 3;"));
}

#[test]
fn s14_comments_and_strings_do_not_block_deletion() {
    let fixture = r#"#include "cler.hpp"
int main() {
    SourceBlock source("Source");
    SinkBlock sink("Sink"); // sink is the last stage
    const char* note = "sink";

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source),
        cler::BlockRunner(&sink)
    );
    flowgraph.run();
    (void)note;
}
"#;
    let mut session = loaded(fixture);
    session
        .apply(tx(
            0,
            vec![Command::DeleteBlock {
                site: 0,
                block: "sink".to_string(),
            }],
        ))
        .expect("comments and strings are not references");
    assert!(session.source().contains("const char* note = \"sink\";"));
}

#[test]
fn s14_a_pointer_alias_blocks_deletion() {
    let fixture = r#"#include "cler.hpp"
int main() {
    SourceBlock source("Source");
    PlotBlock plot("Plot");
    auto* p = &plot;

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source),
        cler::BlockRunner(&plot)
    );
    flowgraph.run();
    p->render();
}
"#;
    let mut session = loaded(fixture);
    let error = session
        .apply(tx(
            0,
            vec![Command::DeleteBlock {
                site: 0,
                block: "plot".to_string(),
            }],
        ))
        .expect_err("the alias still points at plot");
    assert!(matches!(error, ApplyError::ReferencesOutsideGraph { .. }));
}

#[test]
fn s2_set_param_text_cannot_escape_its_argument_slot() {
    let mut session = session("hello_world.cpp");
    let before = session.source().to_string();
    let error = session
        .apply(tx(
            0,
            vec![set_param("source1", 1, "1.0f); volatile int pwned = (0")],
        ))
        .expect_err("the text is not one expression");
    assert!(matches!(error, ApplyError::InvalidExpression { .. }));
    assert_eq!(session.source(), before);
    assert_eq!(session.revision(), 0);
}

#[test]
fn s2_set_config_value_cannot_inject_a_statement() {
    let mut session = session("fm_receiver.cpp");
    let before = session.source().to_string();
    let error = session
        .apply(tx(
            0,
            vec![Command::SetConfig {
                site: 0,
                path: "collect_detailed_stats".to_string(),
                new_value: "false; std::system(\"id\")".to_string(),
            }],
        ))
        .expect_err("the value is not one expression");
    assert!(matches!(error, ApplyError::InvalidExpression { .. }));
    assert_eq!(session.source(), before);
}

#[test]
fn s11_set_config_path_must_be_a_member_path() {
    let mut session = session("fm_receiver.cpp");
    let before = session.source().to_string();
    let error = session
        .apply(tx(
            0,
            vec![Command::SetConfig {
                site: 0,
                path: "x = 1; std::abort(); //".to_string(),
                new_value: "0".to_string(),
            }],
        ))
        .expect_err("the path is not a member path");
    assert!(matches!(error, ApplyError::InvalidIdentifier { .. }));
    assert_eq!(session.source(), before);
}

#[test]
fn s2_add_block_type_name_must_be_a_qualified_type() {
    let mut session = session("hello_world.cpp");
    let before = session.source().to_string();
    let error = session
        .apply(tx(
            0,
            vec![Command::AddBlock {
                site: 0,
                type_name: "int pwned = 1; ThrottleBlock".to_string(),
                template_args: vec!["float".to_string()],
                ctor_args: vec!["\"T\"".to_string(), "1000".to_string()],
                var_name: "t2".to_string(),
            }],
        ))
        .expect_err("the type name is not a qualified type");
    assert!(matches!(error, ApplyError::InvalidIdentifier { .. }));
    assert_eq!(session.source(), before);
}

#[test]
fn s2_add_block_arguments_must_be_single_expressions() {
    let hostile = [
        vec!["1000); std::abort(); (0".to_string()],
        vec!["\"T\"".to_string(), "1, 2".to_string()],
        vec!["".to_string()],
    ];
    for ctor_args in hostile {
        let mut session = session("hello_world.cpp");
        let before = session.source().to_string();
        let error = session
            .apply(tx(
                0,
                vec![Command::AddBlock {
                    site: 0,
                    type_name: "ThrottleBlock".to_string(),
                    template_args: vec!["float".to_string()],
                    ctor_args: ctor_args.clone(),
                    var_name: "t2".to_string(),
                }],
            ))
            .unwrap_err();
        assert!(
            matches!(error, ApplyError::InvalidExpression { .. }),
            "{ctor_args:?} produced {error}"
        );
        assert_eq!(session.source(), before);
    }

    let mut session = session("hello_world.cpp");
    let error = session
        .apply(tx(
            0,
            vec![Command::AddBlock {
                site: 0,
                type_name: "ThrottleBlock".to_string(),
                template_args: vec!["float, int".to_string()],
                ctor_args: vec!["\"T\"".to_string(), "1000".to_string()],
                var_name: "t2".to_string(),
            }],
        ))
        .expect_err("a template argument list is not one argument");
    assert!(matches!(error, ApplyError::InvalidExpression { .. }));
}

#[test]
fn s11_add_block_var_name_cannot_shadow_a_non_block_local() {
    let mut session = session("hello_world.cpp");
    let before = session.source().to_string();
    let error = session
        .apply(tx(0, vec![throttle("gui", "T")]))
        .expect_err("gui is already a local in that scope");
    assert!(matches!(error, ApplyError::DuplicateVariable { .. }));
    assert_eq!(session.source(), before);
}

#[test]
fn s11_add_block_var_name_cannot_be_a_keyword() {
    for word in ["new", "class", "int", "this", "operator", "template"] {
        let mut session = session("hello_world.cpp");
        let before = session.source().to_string();
        let error = session.apply(tx(0, vec![throttle(word, "T")])).unwrap_err();
        assert!(
            matches!(
                error,
                ApplyError::ReservedIdentifier { .. } | ApplyError::DuplicateVariable { .. }
            ),
            "{word} produced {error}"
        );
        assert_eq!(session.source(), before);
    }
}

#[test]
fn s12_display_name_round_trips_through_the_model() {
    for name in [
        "plain",
        "with \"quote\"",
        "back\\slash",
        "trailing backslash \\",
        "both \\\" together",
        "apostrophe ' and question ?",
        "unicode 振幅",
    ] {
        let mut session = session("hello_world.cpp");
        session
            .apply(tx(
                0,
                vec![Command::SetDisplayName {
                    site: 0,
                    block: "adder".to_string(),
                    new_text: name.to_string(),
                }],
            ))
            .unwrap_or_else(|e| panic!("set_display_name({name:?}) rejected: {e}"));
        let site = only_site(&session);
        let seen = site
            .block("adder")
            .expect("adder survives")
            .display_name
            .clone();
        assert_eq!(seen.as_deref(), Some(name), "{name:?} did not round trip");
        assert!(!session.has_errors());
    }
}

#[test]
fn s12_display_name_with_a_control_character_is_rejected() {
    for name in ["line1\nline2", "tab\there", "null\0here"] {
        let mut session = session("hello_world.cpp");
        let before = session.source().to_string();
        let error = session
            .apply(tx(
                0,
                vec![Command::SetDisplayName {
                    site: 0,
                    block: "adder".to_string(),
                    new_text: name.to_string(),
                }],
            ))
            .unwrap_err();
        assert!(matches!(error, ApplyError::InvalidExpression { .. }));
        assert_eq!(session.source(), before);
    }
}

#[test]
fn s3_a_pre_existing_parse_error_refuses_every_edit() {
    let fixture = r#"#include "cler.hpp"
void legacy() {
    int x = @@@;
}
int main() {
    SourceBlock source("Source");
    SinkBlock sink("Sink");

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &sink.in),
        cler::BlockRunner(&sink)
    );
    flowgraph.run();
}
"#;
    let mut session = loaded(fixture);
    assert!(
        session.has_errors(),
        "fixture must start with a parse error"
    );
    let before = session.source().to_string();
    let error = session
        .apply(tx(0, vec![set_param("source", 0, "\"Renamed\"")]))
        .expect_err("an imperfect file is not editable");
    assert!(matches!(error, ApplyError::FileHasErrors { .. }));
    assert_eq!(session.source(), before);
}

#[test]
fn s3_no_corpus_file_starts_with_a_parse_error() {
    let mut broken = Vec::new();
    for entry in std::fs::read_dir(CORPUS).expect("corpus") {
        let path = entry.expect("entry").path();
        if path.extension().and_then(|e| e.to_str()) != Some("cpp") {
            continue;
        }
        if DocumentSession::open(&path).expect("open").has_errors() {
            broken.push(path.display().to_string());
        }
    }
    broken.sort();
    assert!(broken.is_empty(), "files with parse errors: {broken:?}");
}

#[test]
fn s3_a_reload_of_a_half_written_file_refuses_edits() {
    let mut session = session("hello_world.cpp");
    let half_saved = format!(
        "{}\nvoid interrupted_write() {{\n",
        source("hello_world.cpp")
    );
    session
        .reload(half_saved)
        .expect("reload accepts any bytes");
    assert!(session.has_errors());
    let revision = session.revision();
    let before = session.source().to_string();
    let error = session
        .apply(tx(revision, vec![set_param("source1", 1, "9.0f")]))
        .expect_err("a half written file is not editable");
    assert!(matches!(error, ApplyError::FileHasErrors { .. }));
    assert_eq!(session.source(), before);
}

#[test]
fn s3_a_read_only_site_refuses_edits() {
    let fixture = r#"#include "cler.hpp"
int main(int argc, char** argv) {
    if (argc > 1) {
        SourceBlock source("First");
        SinkBlock sink("FirstSink");
        auto flowgraph = cler::make_desktop_flowgraph(
            cler::BlockRunner(&source, &sink.in),
            cler::BlockRunner(&sink)
        );
        flowgraph.run();
    } else {
        SourceBlock source("Second");
        SinkBlock sink("SecondSink");
        auto flowgraph = cler::make_desktop_flowgraph(
            cler::BlockRunner(&source, &sink.in),
            cler::BlockRunner(&sink)
        );
        flowgraph.run();
    }
}
"#;
    let mut session = loaded(fixture);
    assert_eq!(session.parse().sites.len(), 2);
    let before = session.source().to_string();
    for site in 0..2 {
        let error = session
            .apply(tx(
                0,
                vec![Command::SetParam {
                    site,
                    block: "source".to_string(),
                    ctor_arg_index: 0,
                    new_text: "\"PATCHED\"".to_string(),
                }],
            ))
            .expect_err("two sites in one function share local names");
        let ApplyError::NotEditable { reason, .. } = error else {
            panic!("expected a read-only refusal");
        };
        assert_eq!(reason, Some(Reason::MultiSiteFunction));
    }
    assert_eq!(session.source(), before);
}

#[test]
fn s9_two_add_blocks_in_one_transaction_apply() {
    let mut session = session("hello_world.cpp");
    session
        .apply(tx(0, vec![throttle("t1", "T1"), throttle("t2", "T2")]))
        .expect("two staged declarations are one gesture");
    let site = only_site(&session);
    assert!(site.block("t1").is_some() && site.block("t2").is_some());
    assert!(session.source().contains(
        "    ThrottleBlock<float> t1(\"T1\", 1000);\n    ThrottleBlock<float> t2(\"T2\", 1000);\n"
    ));
}

#[test]
fn s9_two_new_config_paths_in_one_transaction_apply() {
    let mut session = session("fm_receiver.cpp");
    session
        .apply(tx(
            0,
            vec![
                Command::SetConfig {
                    site: 0,
                    path: "adaptive_sleep".to_string(),
                    new_value: "true".to_string(),
                },
                Command::SetConfig {
                    site: 0,
                    path: "scheduler".to_string(),
                    new_value: "cler::SchedulerType::ThreadPerBlock".to_string(),
                },
            ],
        ))
        .expect("two new config assignments are one gesture");
    let site = only_site(&session);
    let config = site.config.as_ref().expect("config");
    let paths: Vec<&str> = config.assignments.iter().map(|a| a.path.as_str()).collect();
    assert!(paths.contains(&"adaptive_sleep") && paths.contains(&"scheduler"));
    assert!(session
        .source()
        .contains("config.adaptive_sleep = true;\n    config.scheduler = cler::SchedulerType::ThreadPerBlock;"));
}

#[test]
fn s9_nested_splices_are_rejected() {
    let mut session = session("hello_world.cpp");
    let before = session.source().to_string();
    let error = session
        .apply(tx(
            0,
            vec![
                Command::DeleteBlock {
                    site: 0,
                    block: "source1".to_string(),
                },
                set_param("source1", 1, "9.0f"),
            ],
        ))
        .expect_err("one splice contains the other");
    assert!(matches!(error, ApplyError::OverlappingSplices { .. }));
    assert_eq!(session.source(), before);
    assert_eq!(session.revision(), 0);
}

#[test]
fn s9_a_zero_width_insert_inside_a_removed_runner_is_rejected() {
    let mut session = session("hello_world.cpp");
    let before = session.source().to_string();
    let error = session
        .apply(tx(
            0,
            vec![
                Command::RemoveFromGraph {
                    site: 0,
                    block: "adder".to_string(),
                },
                Command::Connect {
                    site: 0,
                    from: "adder".to_string(),
                    to: "plot".to_string(),
                    port: "in".to_string(),
                    port_index: Some(0),
                },
            ],
        ))
        .expect_err("the insert lands inside a removed range");
    assert!(matches!(error, ApplyError::OverlappingSplices { .. }));
    assert_eq!(session.source(), before);
}

#[test]
fn s9_connect_and_disconnect_on_one_runner_compose() {
    let mut session = session("hello_world.cpp");
    let site = only_site(&session);
    let edge = site
        .edges
        .iter()
        .position(|e| e.from == "throttle" && e.to == "plot")
        .expect("throttle -> plot edge");
    session
        .apply(tx(
            0,
            vec![
                Command::Disconnect { site: 0, edge },
                Command::Connect {
                    site: 0,
                    from: "throttle".to_string(),
                    to: "adder".to_string(),
                    port: "in".to_string(),
                    port_index: Some(0),
                },
            ],
        ))
        .expect("adjacent splices on one runner compose");
    assert!(session
        .source()
        .contains("cler::BlockRunner(&throttle, &adder.in[0]),"));
    assert!(!session.has_errors());
}

#[test]
fn s8_an_empty_transaction_keeps_the_revision() {
    let mut session = session("hello_world.cpp");
    session
        .apply(tx(0, vec![set_param("source1", 1, "7.5f")]))
        .expect("edit");
    let revision = session.revision();
    let bytes = session.source().to_string();

    let outcome = session.apply(tx(revision, Vec::new())).expect("empty tx");
    assert_eq!(outcome.revision, revision);
    assert_eq!(session.revision(), revision, "a revision was consumed");
    assert_eq!(session.source(), bytes);
}

#[test]
fn s8_an_identity_set_param_is_not_an_edit() {
    let mut session = session("hello_world.cpp");
    let bytes = session.source().to_string();
    session
        .apply(tx(0, vec![set_param("source1", 1, "1.0f")]))
        .expect("no-op set_param");
    assert_eq!(session.source(), bytes);
    assert_eq!(
        session.revision(),
        0,
        "an identity edit consumed a revision"
    );
}

#[test]
fn s16_a_transaction_without_a_version_is_rejected() {
    let json = r#"{"base_revision":0,"commands":[]}"#;
    let parsed: Result<Transaction, _> = serde_json::from_str(json);
    assert!(
        parsed.is_err(),
        "a version-less transaction was stamped as current"
    );
}

#[test]
fn s16_a_transaction_with_duplicate_fields_is_rejected() {
    let json = r#"{"version":"0.3.0","base_revision":0,"base_revision":99,"commands":[]}"#;
    let parsed: Result<Transaction, _> = serde_json::from_str(json);
    assert!(parsed.is_err(), "duplicate base_revision was accepted");
}

#[test]
fn s17_crlf_sources_keep_their_line_endings() {
    let crlf = source("hello_world.cpp").replace('\n', "\r\n");
    let mut session = loaded(&crlf);
    session
        .apply(tx(0, vec![throttle("t2", "T")]))
        .expect("add_block");
    let after = session.source();
    let bare = after.matches('\n').count() - after.matches("\r\n").count();
    assert_eq!(bare, 0, "add_block introduced {bare} LF-only line endings");

    let mut config = loaded(&source("fm_receiver.cpp").replace('\n', "\r\n"));
    config
        .apply(tx(
            0,
            vec![Command::SetConfig {
                site: 0,
                path: "adaptive_sleep".to_string(),
                new_value: "true".to_string(),
            }],
        ))
        .expect("set_config");
    let after = config.source();
    let bare = after.matches('\n').count() - after.matches("\r\n").count();
    assert_eq!(bare, 0, "set_config introduced {bare} LF-only line endings");
}

#[test]
fn s17_multibyte_text_does_not_shift_splices() {
    let annotated = source("hello_world.cpp")
        .replace("//amplitude, frequency", "//amplitude (振幅 🎛), frequency");
    let mut session = loaded(&annotated);
    session
        .apply(tx(0, vec![set_param("source1", 1, "4.25f")]))
        .expect("set_param");
    assert!(session
        .source()
        .contains("source1(\"CWSource\", 4.25f, 1.0f, SPS); //amplitude (振幅 🎛), frequency"));
}

fn scratch(tag: &str) -> PathBuf {
    let dir = std::env::temp_dir().join(format!("cler-graph-reg-{}-{tag}", std::process::id()));
    std::fs::remove_dir_all(&dir).ok();
    std::fs::create_dir_all(&dir).expect("scratch dir");
    dir
}

fn cli(dir: &Path, args: &[&str]) -> Output {
    std::process::Command::new(env!("CARGO_BIN_EXE_cler-graph"))
        .current_dir(dir)
        .args(args)
        .output()
        .expect("cli runs")
}

fn git(dir: &Path, args: &[&str]) -> Output {
    std::process::Command::new("git")
        .current_dir(dir)
        .args(args)
        .output()
        .expect("git runs")
}

fn sha256sum(dir: &Path, name: &str) -> String {
    let out = std::process::Command::new("sha256sum")
        .current_dir(dir)
        .arg(name)
        .output()
        .expect("sha256sum runs");
    assert!(out.status.success(), "sha256sum failed");
    String::from_utf8_lossy(&out.stdout)
        .split_whitespace()
        .next()
        .expect("a digest")
        .to_string()
}

fn reported_sha256(dir: &Path, name: &str) -> String {
    let out = cli(dir, &["parse", name]);
    assert!(
        out.status.success(),
        "{}",
        String::from_utf8_lossy(&out.stderr)
    );
    let model: serde_json::Value =
        serde_json::from_slice(&out.stdout).expect("parse output is json");
    model["sha256"]
        .as_str()
        .expect("parse reports a sha256")
        .to_string()
}

fn seeded(tag: &str, name: &str, contents: &str) -> (PathBuf, PathBuf) {
    let dir = scratch(tag);
    let file = dir.join(name);
    std::fs::write(&file, contents).expect("seed");
    (dir, file)
}

#[test]
fn s5_the_reported_digest_is_the_sha256sum_of_the_bytes() {
    let dir = scratch("digest");
    for name in ["hello_world.cpp", "frequency_shift.cpp", "spike.cpp"] {
        std::fs::write(dir.join(name), source(name)).expect("seed");
        assert_eq!(
            reported_sha256(&dir, name),
            sha256sum(&dir, name),
            "{name}: the reported digest disagrees with sha256sum"
        );
    }
}

#[test]
fn s5_apply_needs_a_revision_guard() {
    let (dir, file) = seeded("noguard", "hello_world.cpp", &source("hello_world.cpp"));
    let plan = json(0, vec![set_param("source1", 1, "4.25f")]);
    let out = cli(&dir, &["apply", "hello_world.cpp", "--transaction", &plan]);
    assert!(!out.status.success(), "apply ran without a revision guard");
    assert_eq!(
        std::fs::read_to_string(&file).expect("read"),
        source("hello_world.cpp")
    );
}

#[test]
fn s5_apply_rejects_both_guard_flags_together() {
    let (dir, _) = seeded("bothflags", "hello_world.cpp", &source("hello_world.cpp"));
    let plan = json(0, vec![set_param("source1", 1, "4.25f")]);
    let digest = sha256sum(&dir, "hello_world.cpp");
    let out = cli(
        &dir,
        &[
            "apply",
            "hello_world.cpp",
            "--transaction",
            &plan,
            "--expect-sha256",
            &digest,
            "--unguarded",
        ],
    );
    assert!(!out.status.success(), "both guard flags were accepted");
}

#[test]
fn s5_apply_refuses_bytes_that_changed_on_disk() {
    let (dir, file) = seeded("stale", "hello_world.cpp", &source("hello_world.cpp"));
    let plan = json(0, vec![set_param("source1", 1, "4.25f")]);
    let digest = sha256sum(&dir, "hello_world.cpp");

    let edited = source("hello_world.cpp").replace(
        "SourceCWBlock<float> source1(\"CWSource\", 1.0f, 1.0f, SPS);",
        "SourceCWBlock<float> source1(\"CWSource\", 3.0f, 9.0f, SPS);",
    );
    std::fs::write(&file, &edited).expect("external edit");

    let out = cli(
        &dir,
        &[
            "apply",
            "hello_world.cpp",
            "--transaction",
            &plan,
            "--expect-sha256",
            &digest,
        ],
    );
    assert!(!out.status.success(), "a stale transaction landed");
    assert!(String::from_utf8_lossy(&out.stderr).contains("changed on disk"));
    assert_eq!(std::fs::read_to_string(&file).expect("read"), edited);
}

#[test]
fn s5_apply_accepts_the_digest_the_parse_command_reported() {
    let (dir, file) = seeded("guarded", "hello_world.cpp", &source("hello_world.cpp"));
    let digest = reported_sha256(&dir, "hello_world.cpp");
    let plan = json(0, vec![set_param("source1", 1, "4.25f")]);
    let out = cli(
        &dir,
        &[
            "apply",
            "hello_world.cpp",
            "--transaction",
            &plan,
            "--expect-sha256",
            &digest,
        ],
    );
    assert!(
        out.status.success(),
        "{}",
        String::from_utf8_lossy(&out.stderr)
    );
    assert!(std::fs::read_to_string(&file)
        .expect("read")
        .contains("\"CWSource\", 4.25f"));
}

#[cfg(unix)]
#[test]
fn s6_apply_writes_through_a_symlink_to_its_target() {
    let (dir, real) = seeded("symlink", "real.cpp", &source("hello_world.cpp"));
    let link = dir.join("link.cpp");
    std::os::unix::fs::symlink(&real, &link).expect("symlink");
    let plan = json(0, vec![set_param("source1", 1, "4.25f")]);

    let out = cli(
        &dir,
        &["apply", "link.cpp", "--transaction", &plan, "--unguarded"],
    );
    assert!(
        out.status.success(),
        "{}",
        String::from_utf8_lossy(&out.stderr)
    );
    assert!(
        std::fs::symlink_metadata(&link)
            .expect("link metadata")
            .file_type()
            .is_symlink(),
        "the symlink was replaced by a regular file"
    );
    assert!(std::fs::read_to_string(&real)
        .expect("target readable")
        .contains("4.25f"));
}

#[cfg(unix)]
#[test]
fn s7_apply_refuses_a_read_only_file() {
    use std::os::unix::fs::PermissionsExt;
    let (dir, file) = seeded("readonly", "hello_world.cpp", &source("hello_world.cpp"));
    std::fs::set_permissions(&file, std::fs::Permissions::from_mode(0o444)).expect("chmod");
    let plan = json(0, vec![set_param("source1", 1, "4.25f")]);

    let out = cli(
        &dir,
        &[
            "apply",
            "hello_world.cpp",
            "--transaction",
            &plan,
            "--unguarded",
        ],
    );
    assert!(!out.status.success(), "a 0444 file was rewritten");
    let mode = std::fs::metadata(&file)
        .expect("metadata")
        .permissions()
        .mode()
        & 0o777;
    assert_eq!(mode, 0o444, "the mode changed");
    assert_eq!(
        std::fs::read_to_string(&file).expect("read"),
        source("hello_world.cpp")
    );
    assert!(!dir.join(".hello_world.cpp.cler-graph.tmp").exists());
}

#[cfg(unix)]
#[test]
fn s7_apply_preserves_the_file_mode() {
    use std::os::unix::fs::PermissionsExt;
    let (dir, file) = seeded("mode", "hello_world.cpp", &source("hello_world.cpp"));
    std::fs::set_permissions(&file, std::fs::Permissions::from_mode(0o640)).expect("chmod");
    let plan = json(0, vec![set_param("source1", 1, "4.25f")]);

    let out = cli(
        &dir,
        &[
            "apply",
            "hello_world.cpp",
            "--transaction",
            &plan,
            "--unguarded",
        ],
    );
    assert!(
        out.status.success(),
        "{}",
        String::from_utf8_lossy(&out.stderr)
    );
    let mode = std::fs::metadata(&file)
        .expect("metadata")
        .permissions()
        .mode()
        & 0o777;
    assert_eq!(mode, 0o640, "the mode was not preserved");
    assert!(std::fs::read_to_string(&file)
        .expect("read")
        .contains("4.25f"));
}

#[test]
fn s6_dry_run_writes_nothing() {
    let original = source("hello_world.cpp");
    let (dir, file) = seeded("dryrun", "hello_world.cpp", &original);
    let plan = json(0, vec![set_param("source1", 1, "4.25f")]);

    let out = cli(
        &dir,
        &[
            "apply",
            "hello_world.cpp",
            "--transaction",
            &plan,
            "--unguarded",
            "--dry-run",
        ],
    );
    assert!(out.status.success());
    assert_eq!(std::fs::read_to_string(&file).expect("read"), original);
    let leftovers: Vec<String> = std::fs::read_dir(&dir)
        .expect("dir")
        .filter_map(|entry| {
            entry
                .ok()
                .map(|e| e.file_name().to_string_lossy().to_string())
        })
        .filter(|name| name != "hello_world.cpp")
        .collect();
    assert!(leftovers.is_empty(), "dry run left {leftovers:?}");
}

fn patch_of(dir: &Path, name: &str, transaction: &str) -> String {
    let out = cli(
        dir,
        &[
            "apply",
            name,
            "--transaction",
            transaction,
            "--unguarded",
            "--dry-run",
        ],
    );
    assert!(
        out.status.success(),
        "{name}: {}",
        String::from_utf8_lossy(&out.stderr)
    );
    String::from_utf8_lossy(&out.stdout).to_string()
}

fn diff_must_reproduce(tag: &str, name: &str, seed: &str, transaction: &str) {
    let (dir, file) = seeded(tag, name, seed);
    git(&dir, &["init", "-q"]);
    let patch = patch_of(&dir, name, transaction);
    std::fs::write(dir.join("p.diff"), &patch).expect("patch");

    let applied = git(&dir, &["apply", "--verbose", "p.diff"]);
    assert!(
        applied.status.success(),
        "{tag}: git apply rejected the diff:\n{}\n--- patch ---\n{patch}",
        String::from_utf8_lossy(&applied.stderr)
    );

    let mut session = loaded(seed);
    session
        .apply(serde_json::from_str(transaction).expect("transaction"))
        .expect("engine applies");
    assert_eq!(
        std::fs::read_to_string(&file).expect("patched"),
        session.source(),
        "{tag}: the diff and the engine disagree\n--- patch ---\n{patch}"
    );
}

const TINY: &str = "SourceBlock source(\"S\", 1.0f);\nauto flowgraph = cler::make_desktop_flowgraph(cler::BlockRunner(&source));\n";

fn tiny_set_param() -> String {
    json(0, vec![set_param("source", 1, "4.25f")])
}

#[test]
fn s13_diff_reproduces_a_single_splice() {
    diff_must_reproduce(
        "d1",
        "hello_world.cpp",
        &source("hello_world.cpp"),
        &json(0, vec![set_param("source1", 1, "4.25f")]),
    );
}

#[test]
fn s13_diff_reproduces_a_two_splice_delete() {
    diff_must_reproduce(
        "d2",
        "hello_world.cpp",
        &source("hello_world.cpp"),
        &json(
            0,
            vec![Command::DeleteBlock {
                site: 0,
                block: "source1".to_string(),
            }],
        ),
    );
}

#[test]
fn s13_diff_reproduces_a_multi_line_insert() {
    diff_must_reproduce(
        "d3",
        "hello_world.cpp",
        &source("hello_world.cpp"),
        &json(0, vec![throttle("t2", "T")]),
    );
}

#[test]
fn s13_diff_reproduces_two_splices_on_one_line() {
    diff_must_reproduce(
        "d4",
        "hello_world.cpp",
        &source("hello_world.cpp"),
        &json(
            0,
            vec![
                set_param("source1", 1, "11.0f"),
                set_param("source1", 3, "2000"),
            ],
        ),
    );
}

#[test]
fn s13_diff_reproduces_a_far_apart_multi_splice() {
    diff_must_reproduce(
        "d5",
        "hello_world.cpp",
        &source("hello_world.cpp"),
        &json(
            0,
            vec![
                Command::SetDisplayName {
                    site: 0,
                    block: "source1".to_string(),
                    new_text: "Renamed".to_string(),
                },
                Command::Connect {
                    site: 0,
                    from: "plot".to_string(),
                    to: "adder".to_string(),
                    port: "in".to_string(),
                    port_index: Some(1),
                },
            ],
        ),
    );
}

#[test]
fn s13_diff_reproduces_an_edit_on_a_file_without_a_trailing_newline() {
    let seed = source("hello_world.cpp").trim_end().to_string();
    diff_must_reproduce(
        "d6",
        "hello_world.cpp",
        &seed,
        &json(0, vec![set_param("source1", 1, "4.25f")]),
    );
}

#[test]
fn s13_diff_reproduces_an_edit_on_the_first_line_block() {
    diff_must_reproduce("d7", "tiny.cpp", TINY, &tiny_set_param());
}

#[test]
fn s13_diff_reproduces_an_edit_on_the_last_line() {
    let one_line = TINY.replace('\n', " ");
    diff_must_reproduce("d8", "tiny.cpp", one_line.trim_end(), &tiny_set_param());
    diff_must_reproduce("d9", "tiny.cpp", &one_line, &tiny_set_param());
}

#[test]
fn s13_diff_applies_however_close_to_eof_the_edit_is() {
    let mut broken = Vec::new();
    for (label, seed) in [
        ("0 lines after the edit", TINY.to_string()),
        ("1 line after the edit", format!("{TINY}// tail\n")),
        (
            "2 lines after the edit",
            format!("{TINY}// tail\n// tail\n"),
        ),
        (
            "3 lines after the edit",
            format!("{TINY}// tail\n// tail\n// tail\n"),
        ),
        ("no trailing newline", TINY.trim_end().to_string()),
        (
            "no trailing newline after tails",
            format!("{TINY}// tail\n// tail"),
        ),
    ] {
        let (dir, _) = seeded(&format!("eof{}", label.len()), "t.cpp", &seed);
        git(&dir, &["init", "-q"]);
        let patch = patch_of(&dir, "t.cpp", &tiny_set_param());
        std::fs::write(dir.join("p.diff"), &patch).expect("patch");
        if !git(&dir, &["apply", "--check", "p.diff"]).status.success() {
            broken.push((label, patch));
        }
    }
    assert!(
        broken.is_empty(),
        "diffs git refuses: {:?}\nfirst patch:\n{}",
        broken.iter().map(|(l, _)| *l).collect::<Vec<_>>(),
        broken.first().map(|(_, p)| p.as_str()).unwrap_or("")
    );
}

fn renameable_block(site: &Site) -> Option<String> {
    site.blocks
        .iter()
        .find(|block| {
            block.editable
                && block
                    .ctor_args
                    .first()
                    .is_some_and(|arg| arg.text.starts_with('"'))
        })
        .map(|block| block.var.clone())
}

#[test]
fn s13_diff_applies_for_every_corpus_file() {
    let dir = scratch("corpus");
    git(&dir, &["init", "-q"]);
    let mut checked = 0;
    let mut broken = Vec::new();
    let mut files: Vec<PathBuf> = std::fs::read_dir(CORPUS)
        .expect("corpus")
        .filter_map(|entry| entry.ok().map(|e| e.path()))
        .filter(|path| path.extension().and_then(|e| e.to_str()) == Some("cpp"))
        .collect();
    files.sort();

    for path in files {
        let name = path
            .file_name()
            .expect("name")
            .to_string_lossy()
            .to_string();
        let text = std::fs::read_to_string(&path).expect("read");
        let session = loaded(&text);
        let model = session.parse();
        let Some(site) = model.sites.first() else {
            continue;
        };
        let Some(block) = renameable_block(site) else {
            continue;
        };
        std::fs::write(dir.join(&name), &text).expect("seed");
        let plan = json(
            0,
            vec![Command::SetDisplayName {
                site: 0,
                block,
                new_text: "Regression Renamed".to_string(),
            }],
        );
        let patch = patch_of(&dir, &name, &plan);
        std::fs::write(dir.join("p.diff"), &patch).expect("patch");
        if !git(&dir, &["apply", "--check", "p.diff"]).status.success() {
            broken.push(name.clone());
        }
        std::fs::remove_file(dir.join(&name)).expect("clean");
        checked += 1;
    }
    assert!(broken.is_empty(), "git refuses the diff for {broken:?}");
    assert!(checked >= 19, "only {checked} corpus files were checked");
}

#[test]
fn a_large_file_round_trips_through_the_cli() {
    let mut text = String::from("#include \"cler.hpp\"\nint main() {\n");
    for index in 0..4000 {
        text.push_str(&format!(
            "    ThrottleBlock<float> b{index}(\"B{index}\", 1000);\n"
        ));
    }
    text.push_str("    auto flowgraph = cler::make_desktop_flowgraph(\n");
    for index in 0..4000 {
        text.push_str(&format!(
            "        cler::BlockRunner(&b{index}, &b{}.in){}\n",
            (index + 1) % 4000,
            if index == 3999 { "" } else { "," }
        ));
    }
    text.push_str("    );\n    flowgraph.run();\n}\n");
    let (dir, file) = seeded("large", "large.cpp", &text);

    let plan = json(0, vec![set_param("b2000", 1, "2000")]);
    let out = cli(
        &dir,
        &["apply", "large.cpp", "--transaction", &plan, "--unguarded"],
    );
    assert!(
        out.status.success(),
        "{}",
        String::from_utf8_lossy(&out.stderr)
    );
    let after = std::fs::read_to_string(&file).expect("read");
    assert_eq!(after.len(), text.len());
    assert!(after.contains("b2000(\"B2000\", 2000)"));
}

use std::time::{Duration, Instant};

use cler_graph::model::{Site, Span};
use cler_graph::{ApplyError, Command, DocumentSession, Splice, Transaction, SCHEMA_VERSION};

const CORPUS: &str = "../../../desktop_examples";

fn source(name: &str) -> String {
    let path = format!("{CORPUS}/{name}");
    std::fs::read_to_string(&path).unwrap_or_else(|e| panic!("{path}: {e}"))
}

fn session(name: &str) -> DocumentSession {
    DocumentSession::load(source(name)).unwrap_or_else(|e| panic!("{name}: {e}"))
}

fn transaction(base_revision: u64, commands: Vec<Command>) -> Transaction {
    Transaction {
        version: SCHEMA_VERSION.to_string(),
        base_revision,
        commands,
    }
}

fn common_prefix(before: &[u8], after: &[u8]) -> usize {
    before.iter().zip(after).take_while(|(a, b)| a == b).count()
}

fn assert_unchanged(old: &[u8], new: Option<&[u8]>, at: usize) {
    let Some(new) = new else {
        panic!("the patched document is too short to hold the bytes from offset {at}");
    };
    if old == new {
        return;
    }
    let differs = common_prefix(old, new);
    panic!(
        "byte {} outside the patched ranges changed: {:?} became {:?}",
        at + differs,
        old.get(differs).copied().map(char::from),
        new.get(differs).copied().map(char::from)
    );
}

fn assert_untouched_bytes(before: &str, after: &str, splices: &[Splice]) {
    let (old, new) = (before.as_bytes(), after.as_bytes());
    if old == new {
        return;
    }
    let first = splices
        .first()
        .expect("a changed document reports splices")
        .start;
    let prefix = common_prefix(old, new);
    assert!(
        prefix >= first,
        "byte {prefix} changed before the first patched range at {first}"
    );

    let mut cursor = 0;
    let mut shifted = 0;
    for splice in splices {
        assert!(
            splice.start >= cursor,
            "splices are not sorted and disjoint: {splice:?} follows offset {cursor}"
        );
        let gap = splice.start - cursor;
        assert_unchanged(
            &old[cursor..splice.start],
            new.get(shifted..shifted + gap),
            cursor,
        );
        shifted += gap + splice.text.len();
        cursor = splice.end;
    }
    assert_unchanged(&old[cursor..], new.get(shifted..), cursor);
}

#[test]
fn the_untouched_bytes_property_can_fail() {
    let before = "alpha beta gamma";
    let after = "alpha BETA gamma";
    assert_untouched_bytes(
        before,
        after,
        &[Splice {
            start: 6,
            end: 10,
            text: "BETA".to_string(),
        }],
    );

    let hook = std::panic::take_hook();
    std::panic::set_hook(Box::new(|_| {}));
    let dishonest = [
        vec![Splice {
            start: 15,
            end: 15,
            text: String::new(),
        }],
        vec![Splice {
            start: 0,
            end: 5,
            text: "alpha".to_string(),
        }],
        Vec::new(),
    ];
    let caught: Vec<bool> = dishonest
        .iter()
        .map(|splices| {
            std::panic::catch_unwind(|| assert_untouched_bytes(before, after, splices)).is_err()
        })
        .collect();
    std::panic::set_hook(hook);
    assert_eq!(
        caught,
        [true, true, true],
        "a splice list that does not account for the change was accepted"
    );
}

fn only_site(session: &DocumentSession) -> Site {
    let mut model = session.parse();
    assert_eq!(model.sites.len(), 1, "expected a single graph site");
    model.sites.remove(0)
}

#[test]
fn set_param_rewrites_one_constructor_argument() {
    let mut session = session("hello_world.cpp");
    let before = session.source().to_string();
    let outcome = session
        .apply(transaction(
            0,
            vec![Command::SetParam {
                site: 0,
                block: "source1".to_string(),
                ctor_arg_index: 1,
                new_text: "2.5f".to_string(),
            }],
        ))
        .expect("set_param applies");

    assert_eq!(outcome.revision, 1);
    assert_eq!(session.revision(), 1);
    assert_eq!(outcome.splices.len(), 1);
    assert_untouched_bytes(&before, session.source(), &outcome.splices);

    let site = only_site(&session);
    let source1 = site.block("source1").expect("source1 survives");
    assert_eq!(source1.ctor_args[1].text, "2.5f");
    assert_eq!(source1.display_name.as_deref(), Some("CWSource"));
    assert!(session
        .source()
        .contains(r#"SourceCWBlock<float> source1("CWSource", 2.5f, 1.0f, SPS);"#));
}

#[test]
fn one_transaction_rewrites_the_arity_and_wires_the_port() {
    let mut session = session("hello_world.cpp");
    let before = session.source().to_string();
    let outcome = session
        .apply(transaction(
            0,
            vec![
                Command::SetTemplateArg {
                    site: 0,
                    block: "adder".to_string(),
                    template_arg_index: 1,
                    new_text: "3".to_string(),
                },
                Command::Connect {
                    site: 0,
                    from: "throttle".to_string(),
                    to: "adder".to_string(),
                    port: "in".to_string(),
                    port_index: Some(2),
                },
            ],
        ))
        .expect("multi-range gesture applies");

    assert_eq!(outcome.splices.len(), 2);
    assert_eq!(session.revision(), 1);
    assert_untouched_bytes(&before, session.source(), &outcome.splices);

    let site = only_site(&session);
    let adder = site.block("adder").expect("adder survives");
    assert_eq!(adder.template_args[1].text, "3");
    assert_eq!(site.edges.len(), 5);
    assert_eq!(site.edges_between("throttle", "adder").len(), 1);
    assert!(session
        .source()
        .contains("cler::BlockRunner(&throttle, &plot.in[0], &adder.in[2])"));
}

#[test]
fn set_display_name_only_touches_the_name_literal() {
    let mut session = session("hello_world.cpp");
    let before = session.source().to_string();
    let outcome = session
        .apply(transaction(
            0,
            vec![Command::SetDisplayName {
                site: 0,
                block: "adder".to_string(),
                new_text: "Summing Junction".to_string(),
            }],
        ))
        .expect("set_display_name applies");
    assert_untouched_bytes(&before, session.source(), &outcome.splices);

    let site = only_site(&session);
    assert_eq!(
        site.block("adder").and_then(|b| b.display_name.clone()),
        Some("Summing Junction".to_string())
    );
}

#[test]
fn connect_appends_a_channel_to_an_existing_runner() {
    let mut session = session("flowgraph.cpp");
    let before = session.source().to_string();
    let before_edges = only_site(&session).edges.len();

    let outcome = session
        .apply(transaction(
            0,
            vec![Command::Connect {
                site: 0,
                from: "adder".to_string(),
                to: "sink".to_string(),
                port: "in".to_string(),
                port_index: None,
            }],
        ))
        .expect("connect applies");
    assert_untouched_bytes(&before, session.source(), &outcome.splices);

    let site = only_site(&session);
    assert_eq!(site.edges.len(), before_edges + 1);
    assert_eq!(site.runners.len(), 4);
    assert_eq!(site.edges_between("adder", "sink").len(), 1);
    assert!(session
        .source()
        .contains("cler::BlockRunner(&adder, &gain.in, &sink.in)"));
}

#[test]
fn connect_creates_a_runner_for_a_declared_but_unwired_block() {
    let staged = r#"#include "cler.hpp"
int main() {
    SourceBlock source("Source");
    GainBlock gain("Gain", 2.0f);
    SinkBlock sink("Sink");

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &sink.in),
        cler::BlockRunner(&sink)
    );

    flowgraph.run();
}
"#;
    let mut session = DocumentSession::load(staged).expect("staged source parses");
    assert!(
        !only_site(&session)
            .block("gain")
            .expect("gain block")
            .in_graph
    );

    let outcome = session
        .apply(transaction(
            0,
            vec![Command::Connect {
                site: 0,
                from: "gain".to_string(),
                to: "sink".to_string(),
                port: "in".to_string(),
                port_index: None,
            }],
        ))
        .expect("connect applies");
    assert_untouched_bytes(staged, session.source(), &outcome.splices);

    let site = only_site(&session);
    assert_eq!(site.runners.len(), 3);
    assert!(site.block("gain").expect("gain block").in_graph);
    assert!(session
        .source()
        .contains("        cler::BlockRunner(&gain, &sink.in)\n    );"));
}

#[test]
fn disconnect_removes_the_named_parallel_edge_only() {
    let mut session = session("plots.cpp");
    let before = session.source().to_string();
    let site = only_site(&session);
    let edge = site
        .edges
        .iter()
        .position(|e| {
            e.from == "cw_complex2realimag"
                && e.to == "cw_timeseries_plot"
                && e.port.index == Some(1)
        })
        .expect("second parallel edge");

    let outcome = session
        .apply(transaction(0, vec![Command::Disconnect { site: 0, edge }]))
        .expect("disconnect applies");
    assert_untouched_bytes(&before, session.source(), &outcome.splices);

    let after = only_site(&session);
    assert_eq!(after.edges.len(), site.edges.len() - 1);
    let remaining = after.edges_between("cw_complex2realimag", "cw_timeseries_plot");
    assert_eq!(remaining.len(), 1);
    assert_eq!(remaining[0].port.index, Some(0));
    assert!(session
        .source()
        .contains("cler::BlockRunner(&cw_complex2realimag, &cw_timeseries_plot.in[0]),"));
    assert!(session
        .source()
        .contains("cler::BlockRunner(&chirp_c2realimag, &chirp_timeseries_plot.in[0], &chirp_timeseries_plot.in[1])"));
}

#[test]
fn add_block_stages_a_declaration_without_wiring_it() {
    let mut session = session("hello_world.cpp");
    let before = session.source().to_string();
    let outcome = session
        .apply(transaction(
            0,
            vec![Command::AddBlock {
                site: 0,
                type_name: "SourceCWBlock".to_string(),
                template_args: vec!["float".to_string()],
                ctor_args: vec![
                    "\"CWSource3\"".to_string(),
                    "1.0f".to_string(),
                    "5.0f".to_string(),
                    "SPS".to_string(),
                ],
                var_name: "source3".to_string(),
            }],
        ))
        .expect("add_block applies");
    assert_untouched_bytes(&before, session.source(), &outcome.splices);

    let site = only_site(&session);
    let staged = site.block("source3").expect("staged block");
    assert!(!staged.in_graph);
    assert!(staged.editable);
    assert_eq!(staged.ctor_args.len(), 4);
    assert_eq!(site.runners.len(), 5);
    assert_eq!(site.edges.len(), 4);
    assert!(session.source().contains(
        "    SourceCWBlock<float> source3(\"CWSource3\", 1.0f, 5.0f, SPS);\n    auto flowgraph"
    ));
}

#[test]
fn adding_a_palette_block_adds_its_header_once() {
    let session = session("hello_world.cpp");
    let pending = session
        .preview_with_includes(
            transaction(
                0,
                vec![Command::AddBlock {
                    site: 0,
                    type_name: "KaiserLPFBlock".to_string(),
                    template_args: vec!["float".to_string()],
                    ctor_args: vec!["\"LPF\"".to_string()],
                    var_name: "lpf".to_string(),
                }],
            ),
            &["desktop_blocks/filters/kaiser_lpf.hpp".to_string()],
        )
        .expect("add block previews");
    assert!(pending
        .source()
        .contains("#include \"desktop_blocks/filters/kaiser_lpf.hpp\""));
    assert!(pending.source().contains("KaiserLPFBlock<float> lpf(\"LPF\");"));
}

#[test]
fn remove_from_graph_keeps_the_declaration() {
    let mut session = session("hello_world.cpp");
    let before = session.source().to_string();
    let outcome = session
        .apply(transaction(
            0,
            vec![Command::RemoveFromGraph {
                site: 0,
                block: "plot".to_string(),
            }],
        ))
        .expect("remove_from_graph applies");
    assert_untouched_bytes(&before, session.source(), &outcome.splices);

    let site = only_site(&session);
    assert_eq!(site.runners.len(), 4);
    assert!(site
        .runners
        .iter()
        .all(|r| r.block.as_deref() != Some("plot")));
    assert!(site.block("plot").is_some());
    assert!(session.source().contains("PlotTimeSeriesBlock plot("));
    assert!(!session.source().contains("cler::BlockRunner(&plot)"));
}

#[test]
fn delete_block_refuses_when_the_variable_is_used_outside_the_graph() {
    let mut session = session("hello_world.cpp");
    let before = session.source().to_string();
    let error = session
        .apply(transaction(
            0,
            vec![Command::DeleteBlock {
                site: 0,
                block: "plot".to_string(),
            }],
        ))
        .expect_err("plot is used outside the graph");

    let ApplyError::ReferencesOutsideGraph { block, spans } = error else {
        panic!("expected a reference refusal, got {error}");
    };
    assert_eq!(block, "plot");
    assert_eq!(spans.len(), 2);
    let cited: Vec<&str> = spans
        .iter()
        .map(|span| {
            let line_start = before[..span.start].rfind('\n').map(|i| i + 1).unwrap_or(0);
            let line_end = before[span.end..]
                .find('\n')
                .map(|i| span.end + i)
                .unwrap_or(before.len());
            before[line_start..line_end].trim()
        })
        .collect();
    assert_eq!(
        cited,
        [
            "plot.set_initial_window(0.0f, 0.0f, 800.0f, 400.0f); //x,y, width, height",
            "plot.render();"
        ]
    );
    assert!(spans.iter().all(|s| &before[s.start..s.end] == "plot"));
    assert_eq!(session.source(), before);
    assert_eq!(session.revision(), 0);
}

#[test]
fn delete_block_removes_declaration_and_runner_when_nothing_else_refers_to_it() {
    let mut session = session("hello_world.cpp");
    let before = session.source().to_string();
    let outcome = session
        .apply(transaction(
            0,
            vec![Command::DeleteBlock {
                site: 0,
                block: "source1".to_string(),
            }],
        ))
        .expect("source1 is only used by the graph");
    assert_eq!(outcome.splices.len(), 2);
    assert_untouched_bytes(&before, session.source(), &outcome.splices);

    let site = only_site(&session);
    assert!(site.block("source1").is_none());
    assert_eq!(site.blocks.len(), 4);
    assert_eq!(site.runners.len(), 4);
    assert_eq!(site.edges.len(), 3);
    assert!(!session.source().contains("source1"));
    assert!(session.source().contains("SourceCWBlock<float> source2"));
    assert!(session
        .source()
        .contains("cler::make_desktop_flowgraph(\n        cler::BlockRunner(&source2"));
}

#[test]
fn set_config_edits_an_assignment_and_appends_a_new_one() {
    let mut session = session("fm_receiver.cpp");
    let before = session.source().to_string();
    let outcome = session
        .apply(transaction(
            0,
            vec![
                Command::SetConfig {
                    site: 0,
                    path: "collect_detailed_stats".to_string(),
                    new_value: "false".to_string(),
                },
                Command::SetConfig {
                    site: 0,
                    path: "scheduler".to_string(),
                    new_value: "cler::SchedulerType::ThreadPerBlock".to_string(),
                },
            ],
        ))
        .expect("set_config applies");
    assert_eq!(outcome.splices.len(), 2);
    assert_untouched_bytes(&before, session.source(), &outcome.splices);

    let site = only_site(&session);
    let config = site.config.as_ref().expect("config");
    assert!(config.editable);
    let paths: Vec<&str> = config.assignments.iter().map(|a| a.path.as_str()).collect();
    assert_eq!(paths, ["collect_detailed_stats", "scheduler"]);
    assert_eq!(config.assignments[0].value, "false");
    assert_eq!(
        config.assignments[1].value,
        "cler::SchedulerType::ThreadPerBlock"
    );
    assert!(session.source().contains(
        "    config.collect_detailed_stats = false;\n    config.scheduler = cler::SchedulerType::ThreadPerBlock;\n    flowgraph.run(config);"
    ));
}

#[test]
fn set_config_inserts_before_run_when_no_assignment_exists() {
    let plain = r#"#include "cler.hpp"
int main() {
    SourceBlock source("Source");
    SinkBlock sink("Sink");

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &sink.in),
        cler::BlockRunner(&sink)
    );

    cler::FlowGraphConfig config;
    flowgraph.run(config);
}
"#;
    let mut session = DocumentSession::load(plain).expect("plain source parses");
    let outcome = session
        .apply(transaction(
            0,
            vec![Command::SetConfig {
                site: 0,
                path: "scheduler".to_string(),
                new_value: "cler::SchedulerType::FixedThreadPool".to_string(),
            }],
        ))
        .expect("set_config applies");
    assert_untouched_bytes(plain, session.source(), &outcome.splices);
    assert!(session.source().contains(
        "    config.scheduler = cler::SchedulerType::FixedThreadPool;\n    flowgraph.run(config);"
    ));

    let site = only_site(&session);
    let config = site.config.as_ref().expect("config");
    assert_eq!(config.assignments.len(), 1);
    assert!(config.editable);
}

#[test]
fn a_failing_command_rejects_the_whole_transaction() {
    let mut session = session("hello_world.cpp");
    let before = session.source().to_string();
    let error = session
        .apply(transaction(
            0,
            vec![
                Command::SetParam {
                    site: 0,
                    block: "source1".to_string(),
                    ctor_arg_index: 1,
                    new_text: "9.0f".to_string(),
                },
                Command::SetParam {
                    site: 0,
                    block: "source1".to_string(),
                    ctor_arg_index: 99,
                    new_text: "nonsense".to_string(),
                },
            ],
        ))
        .expect_err("the second command is out of range");
    assert!(matches!(error, ApplyError::IndexOutOfRange { .. }));
    assert_eq!(session.source(), before);
    assert_eq!(session.revision(), 0);
}

#[test]
fn read_only_elements_reject_with_their_reason() {
    let mut session = session("adsb_receiver.cpp");
    let before = session.source().to_string();
    let error = session
        .apply(transaction(
            0,
            vec![Command::SetParam {
                site: 0,
                block: "source".to_string(),
                ctor_arg_index: 0,
                new_text: "\"Nope\"".to_string(),
            }],
        ))
        .expect_err("the optional/emplace source is read-only");
    let ApplyError::NotEditable { reason, .. } = error else {
        panic!("expected a read-only refusal");
    };
    assert_eq!(
        reason,
        Some(cler_graph::model::Reason::OptionalEmplaceDeclaration)
    );
    assert_eq!(session.source(), before);
}

#[test]
fn a_stale_base_revision_is_rejected() {
    let mut session = session("hello_world.cpp");
    let command = Command::SetParam {
        site: 0,
        block: "source1".to_string(),
        ctor_arg_index: 1,
        new_text: "3.0f".to_string(),
    };
    session
        .apply(transaction(0, vec![command.clone()]))
        .expect("first edit applies");
    let before = session.source().to_string();

    let error = session
        .apply(transaction(0, vec![command]))
        .expect_err("stale revision");
    assert!(matches!(
        error,
        ApplyError::RevisionMismatch {
            base_revision: 0,
            current_revision: 1
        }
    ));
    assert_eq!(session.source(), before);
    assert_eq!(session.revision(), 1);
}

#[test]
fn a_foreign_schema_version_is_rejected() {
    let mut session = session("hello_world.cpp");
    let error = session
        .apply(Transaction {
            version: "9.9.9".to_string(),
            base_revision: 0,
            commands: Vec::new(),
        })
        .expect_err("schema mismatch");
    assert!(matches!(error, ApplyError::SchemaMismatch { .. }));
}

#[test]
fn reload_replaces_the_source_and_bumps_the_revision() {
    let mut session = session("hello_world.cpp");
    let revision = session.revision();
    session.reload(source("flowgraph.cpp")).expect("reload");
    assert_eq!(session.source(), source("flowgraph.cpp"));
    assert_eq!(session.revision(), revision + 1);
}

#[test]
fn overlapping_commands_are_rejected() {
    let mut session = session("hello_world.cpp");
    let before = session.source().to_string();
    let error = session
        .apply(transaction(
            0,
            vec![
                Command::SetParam {
                    site: 0,
                    block: "adder".to_string(),
                    ctor_arg_index: 0,
                    new_text: "\"A\"".to_string(),
                },
                Command::SetDisplayName {
                    site: 0,
                    block: "adder".to_string(),
                    new_text: "B".to_string(),
                },
            ],
        ))
        .expect_err("two commands rewrite the same span");
    assert!(matches!(error, ApplyError::OverlappingSplices { .. }));
    assert_eq!(session.source(), before);
}

#[test]
fn the_command_schema_round_trips_through_json() {
    let json = r#"{
        "version": "0.3.0",
        "base_revision": 3,
        "commands": [
            {"command": "set_param", "site": 0, "block": "source1", "ctor_arg_index": 1, "new_text": "2.0f"},
            {"command": "connect", "site": 0, "from": "a", "to": "b", "port": "in", "port_index": 2},
            {"command": "add_block", "site": 0, "type": "AddBlock", "template_args": ["float", "2"], "ctor_args": ["\"Adder\""], "var_name": "adder2"}
        ]
    }"#;
    let parsed: Transaction = serde_json::from_str(json).expect("transaction parses");
    assert_eq!(parsed.version, SCHEMA_VERSION);
    assert_eq!(parsed.base_revision, 3);
    assert_eq!(parsed.commands.len(), 3);
    let encoded = serde_json::to_string(&parsed.commands[1]).expect("command serializes");
    assert_eq!(
        encoded,
        r#"{"command":"connect","site":0,"from":"a","to":"b","port":"in","port_index":2}"#
    );
}

struct Lcg(u64);

impl Lcg {
    fn next(&mut self) -> u64 {
        self.0 = self
            .0
            .wrapping_mul(6364136223846793005)
            .wrapping_add(1442695040888963407);
        self.0 >> 16
    }

    fn below(&mut self, bound: usize) -> usize {
        if bound == 0 {
            return 0;
        }
        (self.next() % bound as u64) as usize
    }

    fn pick<'a, T>(&mut self, items: &'a [T]) -> Option<&'a T> {
        if items.is_empty() {
            return None;
        }
        let index = self.below(items.len());
        items.get(index)
    }
}

const PROPERTY_FILES: [&str; 8] = [
    "hello_world.cpp",
    "flowgraph.cpp",
    "plots.cpp",
    "mass_spring_damper.cpp",
    "polyphase_channelizer.cpp",
    "fm_receiver.cpp",
    "spike/spike.cpp",
    "uhd_device.cpp",
];

const KINDS: usize = 9;
const PARAM_KINDS: [usize; 3] = [0, 1, 2];
const ROUNDS: usize = KINDS * 40;

fn wired_blocks(site: &Site) -> Vec<String> {
    site.blocks
        .iter()
        .filter(|b| b.editable && !b.type_text.is_empty())
        .map(|b| b.var.clone())
        .collect()
}

fn word_offsets(text: &str, word: &str) -> Vec<usize> {
    let boundary = |c: Option<char>| !matches!(c, Some(c) if c.is_ascii_alphanumeric() || c == '_');
    let mut found = Vec::new();
    let mut from = 0;
    while let Some(at) = text[from..].find(word) {
        let start = from + at;
        let end = start + word.len();
        if boundary(text[..start].chars().next_back()) && boundary(text[end..].chars().next()) {
            found.push(start);
        }
        from = end;
    }
    found
}

fn only_referenced_by_the_graph(src: &str, site: &Site, var: &str) -> bool {
    let inside = |span: Span, start: usize| start >= span.start && start + var.len() <= span.end;
    let declaration = site.block(var).map(|b| b.span);
    word_offsets(src, var).into_iter().all(|start| {
        inside(site.span, start) || declaration.is_some_and(|span| inside(span, start))
    })
}

fn generate(
    rng: &mut Lcg,
    src: &str,
    site: &Site,
    at: usize,
    kind: usize,
    seq: usize,
) -> Option<Command> {
    let blocks = wired_blocks(site);
    match kind {
        0 => {
            let candidates: Vec<&String> = blocks
                .iter()
                .filter(|var| {
                    site.block(var)
                        .map(|b| !b.ctor_args.is_empty())
                        .unwrap_or(false)
                })
                .collect();
            let var = rng.pick(&candidates)?;
            let block = site.block(var)?;
            Some(Command::SetParam {
                site: at,
                block: (*var).clone(),
                ctor_arg_index: rng.below(block.ctor_args.len()),
                new_text: format!("{}", 1000 + seq),
            })
        }
        1 => {
            let candidates: Vec<&String> = blocks
                .iter()
                .filter(|var| {
                    site.block(var)
                        .map(|b| b.alias.is_none() && !b.template_args.is_empty())
                        .unwrap_or(false)
                })
                .collect();
            let var = rng.pick(&candidates)?;
            let block = site.block(var)?;
            let index = rng.below(block.template_args.len());
            let current = &block.template_args[index].text;
            let new_text = if current.parse::<i64>().is_ok() {
                format!("{}", 1 + seq % 6)
            } else {
                "double".to_string()
            };
            Some(Command::SetTemplateArg {
                site: at,
                block: (*var).clone(),
                template_arg_index: index,
                new_text,
            })
        }
        2 => {
            let candidates: Vec<&String> = blocks
                .iter()
                .filter(|var| {
                    site.block(var)
                        .and_then(|b| b.ctor_args.first())
                        .map(|arg| arg.text.starts_with('"'))
                        .unwrap_or(false)
                })
                .collect();
            let var = rng.pick(&candidates)?;
            Some(Command::SetDisplayName {
                site: at,
                block: (*var).clone(),
                new_text: format!("Renamed {seq}"),
            })
        }
        3 => {
            let sources: Vec<&String> = blocks
                .iter()
                .filter(|var| {
                    site.runners
                        .iter()
                        .find(|r| r.block.as_deref() == Some(var.as_str()))
                        .map(|r| r.editable)
                        .unwrap_or(true)
                })
                .collect();
            let from = rng.pick(&sources)?;
            let to = rng.pick(&blocks)?;
            Some(Command::Connect {
                site: at,
                from: (*from).clone(),
                to: to.clone(),
                port: "in".to_string(),
                port_index: None,
            })
        }
        4 => {
            let editable: Vec<usize> = site
                .edges
                .iter()
                .enumerate()
                .filter(|(_, e)| e.editable)
                .map(|(index, _)| index)
                .collect();
            let edge = rng.pick(&editable)?;
            Some(Command::Disconnect {
                site: at,
                edge: *edge,
            })
        }
        5 => {
            let wired: Vec<&String> = blocks
                .iter()
                .filter(|var| {
                    site.runners
                        .iter()
                        .any(|r| r.block.as_deref() == Some(var.as_str()) && r.editable)
                })
                .collect();
            let var = rng.pick(&wired)?;
            Some(Command::RemoveFromGraph {
                site: at,
                block: (*var).clone(),
            })
        }
        6 => Some(Command::AddBlock {
            site: at,
            type_name: "ThrottleBlock".to_string(),
            template_args: vec!["float".to_string()],
            ctor_args: vec![format!("\"Staged {seq}\""), "1000".to_string()],
            var_name: format!("staged_{seq}"),
        }),
        7 => {
            let config = site.config.as_ref().filter(|config| config.editable)?;
            let existing: Vec<&str> = config
                .assignments
                .iter()
                .filter(|assignment| assignment.editable)
                .map(|assignment| assignment.path.as_str())
                .collect();
            let fresh = ["adaptive_sleep", "collect_detailed_stats", "num_workers"];
            let path = if existing.is_empty() || seq.is_multiple_of(2) {
                (*rng.pick(&fresh)?).to_string()
            } else {
                (*rng.pick(&existing)?).to_string()
            };
            let new_value = if path == "num_workers" {
                format!("{}", 1 + seq % 4)
            } else if seq % 4 < 2 {
                "true".to_string()
            } else {
                "false".to_string()
            };
            Some(Command::SetConfig {
                site: at,
                path,
                new_value,
            })
        }
        _ => {
            let candidates: Vec<&String> = blocks
                .iter()
                .filter(|var| only_referenced_by_the_graph(src, site, var))
                .collect();
            let var = rng.pick(&candidates)?;
            Some(Command::DeleteBlock {
                site: at,
                block: (*var).clone(),
            })
        }
    }
}

fn check(before: &Site, after: &Site, command: &Command) {
    match command {
        Command::SetParam {
            block,
            ctor_arg_index,
            new_text,
            ..
        } => {
            let target = after.block(block).expect("block survives set_param");
            assert_eq!(&target.ctor_args[*ctor_arg_index].text, new_text);
        }
        Command::SetTemplateArg {
            block,
            template_arg_index,
            new_text,
            ..
        } => {
            let target = after.block(block).expect("block survives set_template_arg");
            assert_eq!(&target.template_args[*template_arg_index].text, new_text);
        }
        Command::SetDisplayName {
            block, new_text, ..
        } => {
            let target = after.block(block).expect("block survives set_display_name");
            assert_eq!(target.display_name.as_deref(), Some(new_text.as_str()));
        }
        Command::Connect { from, to, port, .. } => {
            assert_eq!(after.edges.len(), before.edges.len() + 1);
            assert!(after
                .edges
                .iter()
                .any(|e| e.from == *from && e.to == *to && e.port.name == *port));
        }
        Command::Disconnect { .. } => {
            assert_eq!(after.edges.len(), before.edges.len() - 1);
        }
        Command::RemoveFromGraph { block, .. } => {
            assert!(after
                .runners
                .iter()
                .all(|r| r.block.as_deref() != Some(block.as_str())));
            assert_eq!(after.runners.len(), before.runners.len() - 1);
        }
        Command::AddBlock { var_name, .. } => {
            let staged = after.block(var_name).expect("staged block appears");
            assert!(!staged.in_graph);
            assert_eq!(after.runners.len(), before.runners.len());
        }
        Command::SetConfig {
            path, new_value, ..
        } => {
            let config = after
                .config
                .as_ref()
                .expect("the config survives set_config");
            let assignment = config
                .assignments
                .iter()
                .find(|assignment| assignment.path == *path)
                .expect("the assignment exists after set_config");
            assert_eq!(&assignment.value, new_value);
            assert!(assignment.editable);
        }
        Command::DeleteBlock { block, .. } => {
            assert!(
                after.block(block).is_none(),
                "{block} survived delete_block"
            );
            assert!(after
                .runners
                .iter()
                .all(|r| r.block.as_deref() != Some(block.as_str())));
            assert!(after
                .edges
                .iter()
                .all(|e| e.from != *block && e.to != *block));
            assert_eq!(after.blocks.len(), before.blocks.len() - 1);
        }
        Command::DefineBlock { .. } => {
            assert_eq!(after.blocks.len(), before.blocks.len());
            assert_eq!(after.runners.len(), before.runners.len());
        }
    }
}

fn corrupt(rng: &mut Lcg, command: &Command) -> (u64, Command) {
    let mut broken = command.clone();
    match rng.below(5) {
        0 => return (7777, broken),
        1 => match &mut broken {
            Command::SetParam { site, .. }
            | Command::SetTemplateArg { site, .. }
            | Command::SetDisplayName { site, .. }
            | Command::SetConfig { site, .. }
            | Command::Connect { site, .. }
            | Command::Disconnect { site, .. }
            | Command::AddBlock { site, .. }
            | Command::RemoveFromGraph { site, .. }
            | Command::DeleteBlock { site, .. }
            | Command::DefineBlock { site, .. } => *site = 900,
        },
        2 => match &mut broken {
            Command::SetParam { block, .. }
            | Command::SetTemplateArg { block, .. }
            | Command::SetDisplayName { block, .. }
            | Command::RemoveFromGraph { block, .. }
            | Command::DeleteBlock { block, .. } => *block = "no_such_block".to_string(),
            Command::Connect { from, .. } => *from = "no_such_block".to_string(),
            Command::Disconnect { edge, .. } => *edge = 4242,
            Command::AddBlock { var_name, .. } => *var_name = "not an identifier".to_string(),
            Command::SetConfig { site, .. } => *site = 900,
            Command::DefineBlock { name, .. } => *name = "not an identifier".to_string(),
        },
        3 => match &mut broken {
            Command::SetParam { ctor_arg_index, .. } => *ctor_arg_index = 999,
            Command::SetTemplateArg {
                template_arg_index, ..
            } => *template_arg_index = 999,
            Command::Disconnect { edge, .. } => *edge = 4242,
            Command::Connect { port, .. } => *port = "in()".to_string(),
            Command::AddBlock { ctor_args, .. } => ctor_args.clear(),
            Command::SetDisplayName { block, .. }
            | Command::RemoveFromGraph { block, .. }
            | Command::DeleteBlock { block, .. } => *block = "no_such_block".to_string(),
            Command::SetConfig { path, .. } => *path = "x = 1; std::abort(); //".to_string(),
            Command::DefineBlock { name, .. } => *name = "LacksTheSuffix".to_string(),
        },
        _ => match &mut broken {
            Command::SetParam { new_text, .. } => {
                *new_text = "1.0f); volatile int pwned = (0".to_string()
            }
            Command::SetTemplateArg { new_text, .. } => {
                *new_text = "float> pwned; using other = std::vector<int".to_string()
            }
            Command::SetDisplayName { new_text, .. } => *new_text = "line1\nline2".to_string(),
            Command::SetConfig { new_value, .. } => {
                *new_value = "false; std::system(\"id\")".to_string()
            }
            Command::Connect { port, .. } => *port = "in(); std::abort()".to_string(),
            Command::Disconnect { edge, .. } => *edge = 4242,
            Command::AddBlock { type_name, .. } => {
                *type_name = "int pwned = 1; ThrottleBlock".to_string()
            }
            Command::RemoveFromGraph { block, .. } | Command::DeleteBlock { block, .. } => {
                *block = "no_such_block".to_string()
            }
            Command::DefineBlock { value_type, .. } => {
                *value_type = "float> pwned; using other = int".to_string()
            }
        },
    }
    (0, broken)
}

struct Round {
    name: &'static str,
    session: DocumentSession,
    at: usize,
    before: Site,
    commands: Vec<Command>,
}

fn target(command: &Command) -> Option<&str> {
    match command {
        Command::SetParam { block, .. }
        | Command::SetTemplateArg { block, .. }
        | Command::SetDisplayName { block, .. } => Some(block),
        _ => None,
    }
}

fn plan_round(rng: &mut Lcg, round: usize, kind: usize) -> Option<Round> {
    for offset in 0..PROPERTY_FILES.len() {
        let name = PROPERTY_FILES[(round / KINDS + offset) % PROPERTY_FILES.len()];
        let session = session(name);
        let model = session.parse();
        let editable: Vec<usize> = model
            .sites
            .iter()
            .enumerate()
            .filter(|(_, site)| site.editable)
            .map(|(index, _)| index)
            .collect();
        let Some(&at) = rng.pick(&editable) else {
            continue;
        };
        let site = &model.sites[at];
        let src = session.source();
        let Some(command) = generate(rng, src, site, at, kind, round) else {
            continue;
        };
        let mut commands = vec![command];
        if PARAM_KINDS.contains(&kind) {
            for step in 1..PARAM_KINDS.len() {
                if rng.below(2) == 0 {
                    continue;
                }
                let extra = PARAM_KINDS[(kind + step) % PARAM_KINDS.len()];
                let Some(more) = generate(rng, src, site, at, extra, round + 500) else {
                    continue;
                };
                if commands.iter().all(|held| target(held) != target(&more)) {
                    commands.push(more);
                }
            }
        }
        return Some(Round {
            name,
            before: site.clone(),
            at,
            commands,
            session,
        });
    }
    None
}

#[test]
fn random_valid_transactions_splice_exactly_and_invalid_ones_change_nothing() {
    let mut rng = Lcg(0x5eed_1234_9abc_def0);
    let mut applied = [0usize; KINDS];
    let mut multi = 0;
    let mut sites_beyond_the_first = 0;
    let mut rejected = 0;

    for round in 0..ROUNDS {
        let kind = round % KINDS;
        let Some(mut plan) = plan_round(&mut rng, round, kind) else {
            continue;
        };
        let name = plan.name;
        let before_source = plan.session.source().to_string();

        for command in &plan.commands {
            let (base, broken) = corrupt(&mut rng, command);
            let mut spoiled = DocumentSession::load(before_source.clone())
                .unwrap_or_else(|e| panic!("{name}: {e}"));
            let refused = spoiled.apply(transaction(base, vec![broken.clone()]));
            assert!(
                refused.is_err(),
                "{name}: expected a rejection for {broken:?}, got success"
            );
            assert_eq!(
                spoiled.source(),
                before_source,
                "{name}: rejected transaction mutated the source"
            );
            assert_eq!(spoiled.revision(), 0);
            rejected += 1;
        }

        let outcome = match plan.session.apply(transaction(0, plan.commands.clone())) {
            Ok(outcome) => outcome,
            Err(ApplyError::ReferencesOutsideGraph { .. }) => {
                assert_eq!(plan.session.source(), before_source);
                continue;
            }
            Err(error) => panic!("{name}: {:?} was rejected: {error}", plan.commands),
        };
        assert_untouched_bytes(&before_source, plan.session.source(), &outcome.splices);
        let changed = plan.session.source() != before_source;
        assert_eq!(plan.session.revision(), u64::from(changed));
        assert!(
            !plan.session.has_errors(),
            "{name}: the edit broke the parse"
        );

        let after = plan.session.parse().sites.remove(plan.at);
        for command in &plan.commands {
            check(&plan.before, &after, command);
        }
        applied[kind] += 1;
        if plan.commands.len() > 1 {
            multi += 1;
        }
        if plan.at > 0 {
            sites_beyond_the_first += 1;
        }
    }

    let total: usize = applied.iter().sum();
    println!(
        "property rounds: {total} applied {applied:?} by kind, {multi} multi-command, \
         {sites_beyond_the_first} on a site other than the first, \
         {rejected} rejected without a byte changing"
    );
    assert!(total > 300, "only {total} valid transactions exercised");
    assert!(
        applied.iter().all(|count| *count >= 10),
        "a command kind was barely exercised: {applied:?}"
    );
    assert!(multi >= 20, "only {multi} multi-command transactions");
    assert!(
        sites_beyond_the_first >= 10,
        "only {sites_beyond_the_first} rounds used a site other than the first"
    );
    assert!(rejected > 300, "only {rejected} rejections exercised");
}

fn percentile(sorted: &[Duration], fraction: f64) -> Duration {
    let index = ((sorted.len() as f64 * fraction) as usize).min(sorted.len() - 1);
    sorted[index]
}

#[test]
fn spike_round_trip_stays_under_the_latency_gate() {
    let mut session = session("spike/spike.cpp");
    let mut samples = Vec::new();
    for round in 0..120u64 {
        let base = session.revision();
        let start = Instant::now();
        let model = session.parse();
        assert!(!model.sites.is_empty());
        drop(model);
        session
            .apply(transaction(
                base,
                vec![Command::SetParam {
                    site: 0,
                    block: "power".to_string(),
                    ctor_arg_index: 1,
                    new_text: format!("-{}.0f", 100 + round % 30),
                }],
            ))
            .expect("set_param applies");
        samples.push(start.elapsed());
    }
    samples.sort_unstable();
    let p50 = percentile(&samples, 0.50);
    let p95 = percentile(&samples, 0.95);
    println!(
        "spike/spike.cpp round trip over {} iterations: p50 {:.2} ms, p95 {:.2} ms, max {:.2} ms",
        samples.len(),
        p50.as_secs_f64() * 1e3,
        p95.as_secs_f64() * 1e3,
        samples.last().copied().unwrap_or_default().as_secs_f64() * 1e3
    );
    let gate = if cfg!(debug_assertions) { 150 } else { 50 };
    assert!(
        p95 < Duration::from_millis(gate),
        "p95 {:.2} ms exceeds the {gate} ms gate",
        p95.as_secs_f64() * 1e3
    );
}

#[test]
fn the_cli_writes_atomically_and_prints_a_diff() {
    let dir = std::env::temp_dir().join(format!("cler-graph-apply-{}", std::process::id()));
    std::fs::create_dir_all(&dir).expect("temp dir");
    let file = dir.join("hello_world.cpp");
    let original = source("hello_world.cpp");
    std::fs::write(&file, &original).expect("seed the temp copy");

    let command = r#"{"version":"0.3.0","base_revision":0,"commands":[{"command":"set_param","site":0,"block":"source1","ctor_arg_index":1,"new_text":"4.25f"}]}"#;
    let dry = std::process::Command::new(env!("CARGO_BIN_EXE_cler-graph"))
        .args([
            "apply",
            &file.display().to_string(),
            "--transaction",
            command,
            "--unguarded",
            "--dry-run",
        ])
        .output()
        .expect("cli runs");
    assert!(
        dry.status.success(),
        "{}",
        String::from_utf8_lossy(&dry.stderr)
    );
    let diff = String::from_utf8_lossy(&dry.stdout);
    assert!(
        diff.contains("@@ -10,7 +10,7 @@"),
        "unexpected diff:\n{diff}"
    );
    assert!(diff.contains(
        "-    SourceCWBlock<float> source1(\"CWSource\", 1.0f, 1.0f, SPS); //amplitude, frequency"
    ));
    assert!(diff.contains(
        "+    SourceCWBlock<float> source1(\"CWSource\", 4.25f, 1.0f, SPS); //amplitude, frequency"
    ));
    assert_eq!(
        std::fs::read_to_string(&file).expect("file readable"),
        original,
        "--dry-run wrote to the file"
    );

    let wet = std::process::Command::new(env!("CARGO_BIN_EXE_cler-graph"))
        .args([
            "apply",
            &file.display().to_string(),
            "--transaction",
            command,
            "--unguarded",
        ])
        .output()
        .expect("cli runs");
    assert!(
        wet.status.success(),
        "{}",
        String::from_utf8_lossy(&wet.stderr)
    );
    let written = std::fs::read_to_string(&file).expect("file readable");
    assert!(written.contains("\"CWSource\", 4.25f, 1.0f, SPS"));
    assert_eq!(written.len(), original.len() + 1);
    assert!(!dir.join(".hello_world.cpp.cler-graph.tmp").exists());

    let stale = std::process::Command::new(env!("CARGO_BIN_EXE_cler-graph"))
        .args([
            "apply",
            &file.display().to_string(),
            "--transaction",
            r#"{"version":"0.3.0","base_revision":4,"commands":[]}"#,
            "--unguarded",
        ])
        .output()
        .expect("cli runs");
    assert!(!stale.status.success());
    assert!(String::from_utf8_lossy(&stale.stderr).contains("revision_mismatch"));

    std::fs::remove_dir_all(&dir).ok();
}

#[test]
fn a_preview_reports_the_splices_it_would_write_without_committing() {
    let session = session("hello_world.cpp");
    let before = session.source().to_string();

    let pending = session
        .preview(transaction(
            0,
            vec![Command::SetParam {
                site: 0,
                block: "source1".to_string(),
                ctor_arg_index: 1,
                new_text: "4.25f".to_string(),
            }],
        ))
        .expect("preview plans");

    assert!(pending.changes());
    let splices = pending.splices();
    assert_eq!(splices.len(), 1, "one param, one splice");
    let splice = &splices[0];
    assert_eq!(splice.text, "4.25f");
    assert_eq!(&before[splice.start..splice.end], "1.0f");
    assert_eq!(
        pending.source(),
        format!(
            "{}{}{}",
            &before[..splice.start],
            splice.text,
            &before[splice.end..]
        ),
        "the splices reproduce the pending source exactly"
    );
    assert_eq!(session.source(), before, "preview committed nothing");
    assert_eq!(session.revision(), 0);
}

#[test]
fn a_partially_read_only_site_still_edits_its_editable_blocks() {
    let mut session = session("adsb_receiver.cpp");
    let outcome = session
        .apply(transaction(
            0,
            vec![Command::SetParam {
                site: 0,
                block: "decoder".to_string(),
                ctor_arg_index: 0,
                new_text: "\"Decoder\"".to_string(),
            }],
        ))
        .expect("editable block in a partially read-only site applies");
    assert_eq!(outcome.revision, 1);
    assert!(session.source().contains("\"Decoder\""));

    let refused = session
        .apply(transaction(
            1,
            vec![Command::SetParam {
                site: 0,
                block: "source".to_string(),
                ctor_arg_index: 0,
                new_text: "\"X\"".to_string(),
            }],
        ))
        .expect_err("the optional-emplace block refuses edits");
    assert!(matches!(refused, ApplyError::NotEditable { .. }));
}

#[test]
fn connect_seeds_the_first_runner_into_an_empty_flowgraph() {
    let source = r#"#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/sources/source_cw.hpp"
#include "desktop_blocks/plots/plot_timeseries.hpp"

int main() {
    SourceCWBlock<float> source_cw("Source", 1.0f, 1.0f, 1000);
    PlotTimeSeriesBlock plot("Plot", {"in"}, 1000, 10.0f);
    auto flowgraph = cler::make_desktop_flowgraph();

    flowgraph.run();
    return 0;
}
"#;
    let mut session = DocumentSession::load(source).expect("loads");
    session
        .apply(transaction(
            0,
            vec![Command::Connect {
                site: 0,
                from: "source_cw".to_string(),
                to: "plot".to_string(),
                port: "in".to_string(),
                port_index: Some(0),
            }],
        ))
        .expect("the first connection seeds a runner");
    assert!(session
        .source()
        .contains("cler::BlockRunner(&source_cw, &plot.in[0])"));

    let site = only_site(&session);
    assert_eq!(site.runners.len(), 1);
    assert_eq!(site.edges.len(), 1);

    let second = session.source().to_string();
    assert!(second.contains("cler::make_desktop_flowgraph(\n"), "the call stays multi-line");
}

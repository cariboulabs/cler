use std::path::PathBuf;
use std::sync::OnceLock;

use cler_graph::model::{ConfigSource, Edge, PortKind, Reason, RunnerForm, Site};
use cler_graph::{palette_specs, BlockSpec, DocumentSession, FileModel};

const CORPUS: &str = "../../../desktop_examples";
const BLOCKS: &str = "../../../desktop_blocks";
const CONFLICT_FIXTURE: &str = "tests/data/type_conflict.cpp";

fn model(name: &str) -> FileModel {
    let path = format!("{CORPUS}/{name}");
    let session = DocumentSession::open(&path).unwrap_or_else(|e| panic!("{path}: {e}"));
    session.parse()
}

fn only_site(name: &str) -> Site {
    let mut model = model(name);
    assert_eq!(model.sites.len(), 1, "{name} should have one graph site");
    model.sites.remove(0)
}

fn desktop_palette() -> &'static [BlockSpec] {
    static PALETTE: OnceLock<Vec<BlockSpec>> = OnceLock::new();
    PALETTE.get_or_init(|| {
        palette_specs(&[PathBuf::from(BLOCKS)]).expect("desktop_blocks palette extracts")
    })
}

fn typed_model(path: &str) -> FileModel {
    let session = DocumentSession::open(path).unwrap_or_else(|e| panic!("{path}: {e}"));
    session.parse_with_palette(desktop_palette())
}

fn typed_site(name: &str) -> Site {
    let mut model = typed_model(&format!("{CORPUS}/{name}"));
    assert_eq!(model.sites.len(), 1, "{name} should have one graph site");
    model.sites.remove(0)
}

fn typed_source(source: &str) -> Site {
    let mut model = DocumentSession::load(source)
        .expect("inline source parses")
        .parse_with_palette(desktop_palette());
    assert_eq!(model.sites.len(), 1, "inline source should have one site");
    model.sites.remove(0)
}

fn sample_types(site: &Site) -> Vec<Option<&str>> {
    site.edges
        .iter()
        .map(|e| e.sample_type.as_deref())
        .collect()
}

fn labelled(edge: &Edge) -> String {
    format!("{} -> {}.{}", edge.from, edge.to, edge.port.name)
}

#[test]
fn hello_world_is_fully_editable() {
    let site = only_site("hello_world.cpp");
    assert_eq!(site.function, "main");
    assert_eq!(site.flowgraph_var.as_deref(), Some("flowgraph"));
    assert_eq!(site.blocks.len(), 5);
    assert_eq!(site.runners.len(), 5);
    assert_eq!(site.edges.len(), 4);
    assert!(site.editable);
    assert!(site.unresolved.is_empty());
    let config = site.config.as_ref().expect("a bare run() exposes an absent config");
    assert_eq!(config.source, ConfigSource::Absent);
    assert!(config.editable);
    assert!(config.assignments.is_empty());
    assert!(site.blocks.iter().all(|b| b.editable && b.in_graph));
    assert!(site.edges.iter().all(|e| e.editable));

    let adder = site.block("adder").expect("adder block");
    assert_eq!(adder.type_name, "AddBlock");
    assert_eq!(adder.display_name.as_deref(), Some("Adder"));
    let args: Vec<&str> = adder
        .template_args
        .iter()
        .map(|a| a.text.as_str())
        .collect();
    assert_eq!(args, ["float", "2"]);

    let first = &site.edges[0];
    assert_eq!(
        (first.from.as_str(), first.to.as_str()),
        ("source1", "adder")
    );
    assert_eq!(first.port.name, "in");
    assert_eq!(first.port.index, Some(0));
    assert_eq!(first.port.kind, PortKind::IndexedField);
}

#[test]
fn uhd_device_keeps_four_sites_apart() {
    let model = model("uhd_device.cpp");
    let functions: Vec<&str> = model.sites.iter().map(|s| s.function.as_str()).collect();
    assert_eq!(
        functions,
        ["mode_rx", "mode_tx_chirp", "mode_tx_cw", "mode_zero_span"]
    );

    let blocks: Vec<usize> = model.sites.iter().map(|s| s.blocks.len()).collect();
    let edges: Vec<usize> = model.sites.iter().map(|s| s.edges.len()).collect();
    assert_eq!(blocks, [4, 4, 4, 3]);
    assert_eq!(edges, [3, 3, 3, 2]);
    assert!(model.sites.iter().all(|s| s.editable));

    let offsets: Vec<usize> = model.sites.iter().map(|s| s.call_offset).collect();
    let mut sorted = offsets.clone();
    sorted.sort_unstable();
    sorted.dedup();
    assert_eq!(offsets, sorted);

    let fanouts: Vec<_> = model
        .sites
        .iter()
        .filter_map(|s| s.block("fanout"))
        .map(|b| b.span)
        .collect();
    assert_eq!(fanouts.len(), 3);
    for (index, span) in fanouts.iter().enumerate() {
        assert!(
            fanouts.iter().skip(index + 1).all(|other| other != span),
            "fanout declarations merged across sites"
        );
    }

    let spectrums: Vec<_> = model
        .sites
        .iter()
        .filter_map(|s| s.block("spectrum"))
        .collect();
    assert_eq!(spectrums.len(), 3);
    assert_eq!(spectrums[0].display_name.as_deref(), Some("USRP Spectrum"));
    assert_eq!(spectrums[1].display_name.as_deref(), Some("TX Spectrum"));

    let rx = model.site_in("mode_rx").expect("mode_rx site");
    assert!(
        rx.block("cw").is_none(),
        "mode_tx_cw block leaked into mode_rx"
    );
    assert!(rx.block("usrp_sink").is_none());
}

#[test]
fn spike_resolves_the_trig_alias() {
    let site = only_site("spike/spike.cpp");
    let trigger = site.block("trigger").expect("trigger block");
    assert_eq!(trigger.type_text, "Trig");
    assert_eq!(trigger.type_name, "TriggerBlock");
    assert_eq!(trigger.alias.as_deref(), Some("Trig"));
    assert_eq!(trigger.template_args[0].text, "float");
    assert!(trigger.editable);
    assert_eq!(site.blocks.len(), 9);
    assert_eq!(site.edges.len(), 6);

    let source = site.block("source").expect("source block");
    assert_eq!(source.type_name, "SpikeSourceBlock");
    assert!(source.editable);
    assert!(site.editable);
}

#[test]
fn adsb_receiver_renders_optional_emplace_as_read_only() {
    let site = only_site("adsb_receiver.cpp");
    assert!(!site.editable);
    assert_eq!(site.read_only_reason, Some(Reason::SiteHasReadOnlyElements));

    let source = site.block("source").expect("source block");
    assert_eq!(source.type_name, "SelectableSourceBlock");
    assert_eq!(source.type_text, "std::optional<SelectableSourceBlock>");
    assert!(!source.editable);
    assert_eq!(
        source.read_only_reason,
        Some(Reason::OptionalEmplaceDeclaration)
    );
    assert_eq!(source.display_name.as_deref(), Some("Source"));
    assert_eq!(source.ctor_args.len(), 6);

    let runner = site
        .runners
        .iter()
        .find(|r| r.block.as_deref() == Some("source"))
        .expect("runner for the optional-held source");
    assert_eq!(runner.block_expr, "&*source");
    assert_eq!(
        runner.read_only_reason,
        Some(Reason::UnsupportedBlockExpression)
    );

    let edge = &site.edges_between("source", "iq2mag")[0];
    assert_eq!(edge.text, "&iq2mag.in");
    assert!(!edge.editable);
    assert_eq!(
        edge.read_only_reason,
        Some(Reason::UnsupportedBlockExpression)
    );

    let null_sink = site.block("null_sink").expect("declared but unwired sink");
    assert!(!null_sink.in_graph);
    assert!(null_sink.editable);

    assert!(site.unresolved.is_empty());
    assert_eq!(site.edges.len(), 3);
}

#[test]
fn selectable_blocks_keeps_the_method_call_port() {
    let site = only_site("selectable_blocks.cpp");
    assert!(!site.editable);
    let edges = site.edges_between("source", "gain");
    assert_eq!(edges.len(), 1);
    let edge = edges[0];
    assert_eq!(edge.text, "gain.in()");
    assert_eq!(edge.port.name, "in");
    assert_eq!(edge.port.kind, PortKind::MethodCall);
    assert!(!edge.editable);
    assert_eq!(edge.read_only_reason, Some(Reason::MethodCallPort));

    let downstream = site.edges_between("gain", "sink");
    assert_eq!(downstream.len(), 1);
    assert!(downstream[0].editable);
    assert!(site.blocks.iter().all(|b| b.editable));
}

#[test]
fn demod_iq_recording_recovers_named_runners() {
    let site = only_site("ezgmsk/demod_iq_recording.cpp");
    assert!(!site.editable);
    assert_eq!(site.runners.len(), 5);
    assert!(site
        .runners
        .iter()
        .all(|r| r.form == RunnerForm::NamedVariable));
    assert!(site
        .runners
        .iter()
        .all(|r| r.read_only_reason == Some(Reason::NamedRunnerVariable)));

    let blocks: Vec<&str> = site.blocks.iter().map(|b| b.var.as_str()).collect();
    assert_eq!(
        blocks,
        [
            "input_file_block",
            "decimator",
            "fanout",
            "output_file_block",
            "ezgmsk_demod"
        ]
    );
    assert!(site.blocks.iter().all(|b| b.editable && b.in_graph));

    assert_eq!(site.edges.len(), 4);
    assert!(site
        .edges
        .iter()
        .all(|e| e.read_only_reason == Some(Reason::NamedRunnerVariable)));
    assert_eq!(site.edges_between("fanout", "ezgmsk_demod").len(), 1);
    assert_eq!(site.edges_between("fanout", "output_file_block").len(), 1);
    assert!(site.unresolved.is_empty());
}

#[test]
fn polyphase_channelizer_resolves_constexpr_template_args() {
    let site = only_site("polyphase_channelizer.cpp");
    let adder = site.block("adder").expect("adder block");
    assert_eq!(adder.template_args[1].text, "NUM_CHANNELS");
    assert_eq!(adder.template_args[1].resolved.as_deref(), Some("5"));

    let channelizer = site.block("channelizer").expect("channelizer block");
    assert_eq!(channelizer.template_args[0].resolved.as_deref(), Some("5"));
    assert_eq!(channelizer.template_args[1].resolved.as_deref(), Some("3"));

    let config = site.config.as_ref().expect("config variable");
    assert_eq!(config.var.as_deref(), Some("config"));
    assert_eq!(config.source, ConfigSource::Variable);
    assert!(config.editable);
    let paths: Vec<&str> = config.assignments.iter().map(|a| a.path.as_str()).collect();
    assert_eq!(paths, ["scheduler", "collect_detailed_stats"]);
    assert_eq!(
        config.assignments[0].value,
        "cler::SchedulerType::FixedThreadPool"
    );
    assert!(site.editable);
}

#[test]
fn mass_spring_damper_keeps_the_feedback_cycle() {
    let site = only_site("mass_spring_damper.cpp");
    assert!(site.editable);
    assert_eq!(site.edges.len(), 6);

    let feedback = site.edges_between("fanout", "controller");
    assert_eq!(feedback.len(), 1);
    assert_eq!(feedback[0].port.name, "measured_position_in");
    assert_eq!(feedback[0].port.kind, PortKind::Field);

    let chain = [
        ("controller", "throttle"),
        ("throttle", "plant"),
        ("plant", "fanout"),
        ("fanout", "controller"),
    ];
    for (from, to) in chain {
        assert_eq!(
            site.edges_between(from, to).len(),
            1,
            "missing cycle edge {from}->{to}"
        );
    }
}

#[test]
fn plots_keeps_parallel_edges_distinct() {
    let site = only_site("plots.cpp");
    assert!(site.editable);
    assert_eq!(site.edges.len(), 14);

    let parallel = site.edges_between("cw_complex2realimag", "cw_timeseries_plot");
    assert_eq!(parallel.len(), 2);
    assert_eq!(parallel[0].port.index, Some(0));
    assert_eq!(parallel[1].port.index, Some(1));
    assert_ne!(parallel[0].span, parallel[1].span);
    assert_eq!(parallel[0].runner_index, parallel[1].runner_index);
    assert_ne!(parallel[0].arg_index, parallel[1].arg_index);

    let fanned = site.edges_between("chirp_fanout", "cspectrum_plot");
    assert_eq!(fanned.len(), 1);
    assert_eq!(fanned[0].port.index, Some(1));
}

#[test]
fn streamlined_has_no_sites_and_no_error() {
    let model = model("streamlined.cpp");
    assert!(model.sites.is_empty());
    assert_eq!(model.version, cler_graph::SCHEMA_VERSION);
}

#[test]
fn inline_config_temporary_is_read_only() {
    let site = only_site("cariboulite_spectrum.cpp");
    let config = site.config.as_ref().expect("inline config");
    assert_eq!(config.source, ConfigSource::InlineTemporary);
    assert!(!config.editable);
    assert_eq!(config.read_only_reason, Some(Reason::InlineConfigTemporary));
    assert!(site.blocks.iter().all(|b| b.editable));
    assert!(site.edges.iter().all(|e| e.editable));
}

#[test]
fn flowgraph_example_keeps_two_named_ports_on_one_block() {
    let site = only_site("flowgraph.cpp");
    let parallel = site.edges_between("source", "adder");
    assert_eq!(parallel.len(), 2);
    assert_eq!(parallel[0].port.name, "in0");
    assert_eq!(parallel[1].port.name, "in1");
    assert!(site.editable);
}

#[test]
fn factory_config_is_read_only_but_nested_paths_are_captured() {
    let source = r#"
#include "cler.hpp"
int main() {
    SourceBlock source("Source");
    SinkBlock sink("Sink");
    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &sink.in),
        cler::BlockRunner(&sink)
    );
    auto config = cler::flowgraph_config::pinned_islands(2);
    config.pinned_islands.calibration_ms = 500;
    fg.run_for(std::chrono::milliseconds(200), config);
}
"#;
    let model = DocumentSession::load(source)
        .expect("inline source parses")
        .parse();
    let site = &model.sites[0];
    let config = site.config.as_ref().expect("factory config");
    assert_eq!(config.var.as_deref(), Some("config"));
    assert!(!config.editable);
    assert_eq!(config.read_only_reason, Some(Reason::FactoryConfig));
    assert_eq!(config.assignments.len(), 1);
    assert_eq!(config.assignments[0].path, "pinned_islands.calibration_ms");
    assert_eq!(config.assignments[0].value, "500");
    assert!(config.assignments[0].editable);
    assert!(!site.editable);
    assert_eq!(site.edges.len(), 1);
}

#[test]
fn corpus_meets_m0_targets() {
    let mut files: Vec<std::path::PathBuf> = Vec::new();
    let mut stack = vec![std::path::PathBuf::from(CORPUS)];
    while let Some(dir) = stack.pop() {
        for entry in std::fs::read_dir(&dir).expect("corpus directory").flatten() {
            let path = entry.path();
            if path.is_dir() {
                stack.push(path);
            } else if path.extension().map(|e| e == "cpp").unwrap_or(false) {
                files.push(path);
            }
        }
    }
    assert!(files.len() >= 22, "corpus shrank: {} files", files.len());

    let mut with_sites = 0;
    let mut fully_editable = 0;
    for file in &files {
        let model = DocumentSession::open(file)
            .unwrap_or_else(|e| panic!("{}: {e}", file.display()))
            .parse();
        if model.sites.is_empty() {
            continue;
        }
        with_sites += 1;
        if model.sites.iter().all(|s| s.editable) {
            fully_editable += 1;
        }
        for site in &model.sites {
            assert!(
                !site.blocks.is_empty(),
                "{} site {} rendered no blocks",
                file.display(),
                site.function
            );
            for runner in &site.runners {
                assert!(
                    runner.block.is_some() || !site.unresolved.is_empty(),
                    "{} dropped a runner argument silently",
                    file.display()
                );
            }
        }
    }
    assert_eq!(with_sites, 22);
    assert!(
        fully_editable >= 15,
        "only {fully_editable} files fully editable"
    );
}

#[test]
fn hello_world_edges_are_all_float() {
    let site = typed_site("hello_world.cpp");
    assert_eq!(sample_types(&site), vec![Some("float"); 4]);
    assert!(site
        .edges
        .iter()
        .all(|e| e.source_type.as_deref() == Some("float")));
    assert!(site.edges.iter().all(|e| !e.type_conflict));
}

#[test]
fn polyphase_channelizer_carries_complex_baseband() {
    let site = typed_site("polyphase_channelizer.cpp");
    assert_eq!(site.edges.len(), 17);
    for edge in &site.edges {
        assert_eq!(
            edge.sample_type.as_deref(),
            Some("std::complex<float>"),
            "{} lost its element type",
            labelled(edge)
        );
        assert!(!edge.type_conflict);
    }

    let variadic = site.edges_between("channelizer", "plot_polyphase_cspectrum");
    assert_eq!(variadic.len(), 5);
    assert!(
        variadic.iter().all(|e| e.source_type.is_none()),
        "a variadic output pack is not a resolved element type"
    );

    let fixed = site.edges_between("adder", "throughput");
    assert_eq!(fixed[0].source_type.as_deref(), Some("std::complex<float>"));
}

#[test]
fn mass_spring_damper_edges_are_all_float() {
    let site = typed_site("mass_spring_damper.cpp");
    assert_eq!(sample_types(&site), vec![Some("float"); 6]);
    assert!(site.edges.iter().all(|e| !e.type_conflict));

    let feedback = site.edges_between("fanout", "controller");
    assert_eq!(feedback[0].sample_type.as_deref(), Some("float"));
}

#[test]
fn a_mismatched_edge_is_flagged_as_a_type_conflict() {
    let mut model = typed_model(CONFLICT_FIXTURE);
    let site = model.sites.remove(0);
    let conflicting: Vec<&cler_graph::model::Edge> =
        site.edges.iter().filter(|e| e.type_conflict).collect();
    assert_eq!(conflicting.len(), 1);
    assert_eq!(labelled(conflicting[0]), "throttle -> throughput.in");
    assert_eq!(
        conflicting[0].sample_type.as_deref(),
        Some("std::complex<float>")
    );
    assert_eq!(conflicting[0].source_type.as_deref(), Some("float"));

    assert_eq!(
        sample_types(&site),
        vec![
            Some("float"),
            Some("std::complex<float>"),
            Some("std::complex<float>")
        ]
    );
    assert!(site.editable, "a type conflict is a lint, not a refusal");
}

#[test]
fn template_arguments_substitute_positionally() {
    let site = typed_source(
        r#"
#include "cler.hpp"

template <typename T, size_t N>
struct MixBlock : public cler::BlockBase {
    cler::Channel<T> in;
    MixBlock(const char* name) : cler::BlockBase(name), in(N) {}
    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        return cler::Empty{};
    }
};

template <typename T>
struct DependentBlock : public cler::BlockBase {
    cler::Channel<T::value_type> in;
    DependentBlock(const char* name) : cler::BlockBase(name), in(8) {}
    cler::Result<cler::Empty, cler::Error> procedure() { return cler::Empty{}; }
};

int main() {
    MixBlock<float, 4> real("Real");
    MixBlock<std::complex<float>, 4> complex_mix("Complex");
    DependentBlock<std::complex<float>> dependent("Dependent");
    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&real, &complex_mix.in),
        cler::BlockRunner(&complex_mix, &dependent.in),
        cler::BlockRunner(&dependent)
    );
    fg.run();
}
"#,
    );

    let crossing = site.edges_between("real", "complex_mix");
    assert_eq!(crossing[0].source_type.as_deref(), Some("float"));
    assert_eq!(
        crossing[0].sample_type.as_deref(),
        Some("std::complex<float>")
    );
    assert!(crossing[0].type_conflict);

    let onwards = site.edges_between("complex_mix", "dependent");
    assert_eq!(
        onwards[0].source_type.as_deref(),
        Some("std::complex<float>")
    );
    assert_eq!(
        onwards[0].sample_type, None,
        "a dependent name is never guessed at"
    );
    assert!(
        !onwards[0].type_conflict,
        "one resolved end can never conflict"
    );
}

#[test]
fn a_type_outside_the_palette_stays_unresolved() {
    let site = typed_site("spike/spike.cpp");
    let unknown = site.edges_between("fanout", "power");
    assert_eq!(unknown[0].sample_type, None);
    assert_eq!(unknown[0].source_type, None);

    let known = site.edges_between("power", "trigger");
    assert_eq!(
        known[0].sample_type.as_deref(),
        Some("float"),
        "the Trig alias resolves through to TriggerBlock<float>"
    );
}

#[test]
fn parsing_without_a_palette_resolves_only_the_open_file() {
    let hello = only_site("hello_world.cpp");
    assert!(hello.edges.iter().all(|e| e.sample_type.is_none()));

    let local = only_site("mass_spring_damper.cpp");
    let into_plant = local.edges_between("throttle", "plant");
    assert_eq!(into_plant[0].sample_type.as_deref(), Some("float"));
    let into_throttle = local.edges_between("controller", "throttle");
    assert_eq!(into_throttle[0].sample_type, None);
    assert_eq!(into_throttle[0].source_type.as_deref(), Some("float"));
}

#[test]
fn the_corpus_reports_no_type_conflicts() {
    for entry in std::fs::read_dir(CORPUS)
        .expect("corpus directory")
        .flatten()
    {
        let path = entry.path();
        if path.extension().map(|e| e != "cpp").unwrap_or(true) {
            continue;
        }
        let model = typed_model(&path.display().to_string());
        for site in &model.sites {
            for edge in &site.edges {
                assert!(
                    !edge.type_conflict,
                    "{} invented a conflict on {}: {:?} vs {:?}",
                    path.display(),
                    labelled(edge),
                    edge.source_type,
                    edge.sample_type
                );
            }
        }
    }
}

#[test]
fn read_only_files_are_never_reported_editable() {
    for name in [
        "adsb_receiver.cpp",
        "selectable_blocks.cpp",
        "ezgmsk/demod_iq_recording.cpp",
    ] {
        let site = only_site(name);
        assert!(!site.editable, "{name} misparsed as editable");
        assert!(site.read_only_elements() > 0);
    }
}

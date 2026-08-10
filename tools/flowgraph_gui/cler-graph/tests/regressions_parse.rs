use cler_graph::model::{FileModel, Reason, Site};
use cler_graph::DocumentSession;

const CORPUS: &str = "../../../desktop_examples";

fn parse(source: &str) -> FileModel {
    DocumentSession::load(source)
        .expect("session loads")
        .parse()
}

fn only(source: &str) -> Site {
    let mut model = parse(source);
    assert_eq!(model.sites.len(), 1, "expected exactly one site");
    model.sites.remove(0)
}

#[test]
fn b1_shadowed_declaration_is_ambiguous_and_read_only() {
    let source = r#"
#include "cler.hpp"
#include "desktop_blocks/math/gain.hpp"
int main(int argc, char** argv) {
    if (argc > 1) {
        GainBlock<float> gain("Calibration Gain", 0.5f);
        gain.set_gain(1.0f);
    }

    SourceCWBlock<float> source("Source", 1.0f, 1.0f, 1000);
    GainBlock<float> gain("Signal Gain", 2.0f);
    SinkNullBlock<float> sink("Sink");

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &gain.in),
        cler::BlockRunner(&gain, &sink.in),
        cler::BlockRunner(&sink)
    );
    flowgraph.run();
}
"#;
    let site = only(source);
    assert_eq!(site.blocks.iter().filter(|b| b.var == "gain").count(), 1);
    let gain = site.block("gain").expect("gain block");
    assert!(!gain.editable);
    assert_eq!(gain.read_only_reason, Some(Reason::AmbiguousDeclaration));
    assert_eq!(gain.display_name.as_deref(), Some("Signal Gain"));
    assert_eq!(gain.ctor_args[1].text, "2.0f");
    assert!(source[gain.span.start..gain.span.end].contains("Signal Gain"));
    assert!(!site.editable);
}

#[test]
fn b2_two_sites_in_one_function_are_read_only() {
    let source = r#"
#include "cler.hpp"
int main() {
    SourceCWBlock<float> tone("Tone", 1.0f, 1.0f, 1000);
    SinkNullBlock<float> tone_sink("ToneSink");
    auto warmup = cler::make_desktop_flowgraph(
        cler::BlockRunner(&tone, &tone_sink.in),
        cler::BlockRunner(&tone_sink)
    );
    warmup.run_for(std::chrono::seconds(1));

    SourceChirpBlock<float> chirp("Chirp", 1.0f, 0.0f, 10.0f, 1000, 1.0f);
    SinkFileBlock<float> recorder("Recorder", "out.bin");
    auto capture = cler::make_desktop_flowgraph(
        cler::BlockRunner(&chirp, &recorder.in),
        cler::BlockRunner(&recorder)
    );
    capture.run();
}
"#;
    let model = parse(source);
    assert_eq!(model.sites.len(), 2);
    for site in &model.sites {
        assert!(!site.editable);
        assert_eq!(site.read_only_reason, Some(Reason::MultiSiteFunction));
        assert!(site.blocks.iter().all(|b| !b.editable));
        assert!(site.edges.iter().all(|e| !e.editable));
    }

    let warmup = &model.sites[0];
    assert!(warmup.block("chirp").is_none(), "declared after the call");
    assert!(warmup.block("recorder").is_none());
    assert_eq!(warmup.blocks.len(), 2);

    let capture = &model.sites[1];
    assert_eq!(capture.blocks.len(), 4);
    assert!(capture.block("chirp").expect("chirp").in_graph);
    assert!(!capture.block("tone").expect("tone").in_graph);
}

#[test]
fn uhd_device_four_sites_in_four_functions_stay_editable() {
    let model = DocumentSession::open(format!("{CORPUS}/uhd_device.cpp"))
        .expect("uhd_device.cpp opens")
        .parse();
    assert_eq!(model.sites.len(), 4);
    assert!(model.sites.iter().all(|s| s.editable));
}

#[test]
fn b3_brace_init_renders_name_and_args_but_stays_read_only() {
    let source = r#"
#include "cler.hpp"
int main() {
    SourceCWBlock<float> source{"Source", 1.0f, 1.0f, 1000};
    GainBlock<float> gain{"Gain", 2.0f};
    SinkNullBlock<float> sink{"Sink"};
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &gain.in),
        cler::BlockRunner(&gain, &sink.in),
        cler::BlockRunner(&sink)
    );
    flowgraph.run();
}
"#;
    let site = only(source);
    let gain = site.block("gain").expect("gain");
    assert!(!gain.editable);
    assert_eq!(gain.read_only_reason, Some(Reason::BraceInitDeclaration));
    assert_eq!(gain.display_name.as_deref(), Some("Gain"));
    let texts: Vec<&str> = gain.ctor_args.iter().map(|a| a.text.as_str()).collect();
    assert_eq!(texts, ["\"Gain\"", "2.0f"]);
    for arg in &gain.ctor_args {
        assert_eq!(&source[arg.span.start..arg.span.end], arg.text);
    }
    assert_eq!(site.edges.len(), 2);
}

#[test]
fn b4_nested_call_and_lambda_ctor_args_split_correctly() {
    let source = r#"
#include "cler.hpp"
int main() {
    SourceCWBlock<float> source("Source", scale(mix(1, 2), 3), 1.0f, 1000);
    GainBlock<float> gain("Gain", [](float a, float b) { return a + b; }(1.0f, 2.0f));
    SinkNullBlock<float> sink("Sink");
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &gain.in),
        cler::BlockRunner(&gain, &sink.in),
        cler::BlockRunner(&sink)
    );
    flowgraph.run();
}
"#;
    let site = only(source);
    let source_block = site.block("source").expect("source");
    let texts: Vec<&str> = source_block
        .ctor_args
        .iter()
        .map(|a| a.text.as_str())
        .collect();
    assert_eq!(texts, ["\"Source\"", "scale(mix(1, 2), 3)", "1.0f", "1000"]);
    let gain = site.block("gain").expect("gain");
    assert_eq!(gain.ctor_args.len(), 2);
    assert!(gain.ctor_args[1].text.starts_with("[](float a, float b)"));
}

#[test]
fn b5_nested_template_args_split_correctly() {
    let source = r#"
#include "cler.hpp"
int main() {
    AddBlock<std::complex<float>, 3> adder("Adder");
    SinkNullBlock<std::complex<float>> sink("Sink");
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&adder, &sink.in),
        cler::BlockRunner(&sink)
    );
    flowgraph.run();
}
"#;
    let site = only(source);
    let adder = site.block("adder").expect("adder");
    let args: Vec<&str> = adder
        .template_args
        .iter()
        .map(|a| a.text.as_str())
        .collect();
    assert_eq!(args, ["std::complex<float>", "3"]);
    assert_eq!(adder.template_args[1].resolved.as_deref(), Some("3"));
}

#[test]
fn b6_whitespace_in_runner_arguments_survives() {
    let source = "
#include \"cler.hpp\"
int main() {
    SourceCWBlock<float> source(\"Source\", 1.0f, 1.0f, 1000);
    AddBlock<float, 2> adder(\"Adder\");
    SinkNullBlock<float> sink(\"Sink\");
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner( & source ,
                           & adder
                             . in [ 1 ] ),
        cler::BlockRunner(&adder,&sink.in),
        cler::BlockRunner(&sink)
    );
    flowgraph.run();
}
";
    let site = only(source);
    assert_eq!(site.edges.len(), 2);
    let edge = &site.edges[0];
    assert_eq!((edge.from.as_str(), edge.to.as_str()), ("source", "adder"));
    assert_eq!(edge.port.name, "in");
    assert_eq!(edge.port.index, Some(1));
    assert!(edge.editable);
    assert!(site.editable);
}

#[test]
fn b7_reassigned_config_field_is_read_only() {
    let source = r#"
#include "cler.hpp"
int main(int argc, char** argv) {
    SourceCWBlock<float> source("Source", 1.0f, 1.0f, 1000);
    SinkNullBlock<float> sink("Sink");
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &sink.in),
        cler::BlockRunner(&sink)
    );
    cler::FlowGraphConfig config;
    config.scheduler = cler::SchedulerType::ThreadPerBlock;
    if (argc > 1) {
        config.scheduler = cler::SchedulerType::FixedThreadPool;
    }
    flowgraph.run(config);
}
"#;
    let site = only(source);
    let config = site.config.as_ref().expect("config");
    let paths: Vec<&str> = config.assignments.iter().map(|a| a.path.as_str()).collect();
    assert_eq!(paths, ["scheduler", "scheduler"]);
    assert!(config.assignments.iter().all(|a| !a.editable));
    assert!(config
        .assignments
        .iter()
        .all(|a| a.read_only_reason == Some(Reason::AmbiguousConfigAssignment)));
    assert!(!config.editable);
    assert_eq!(
        config.read_only_reason,
        Some(Reason::AmbiguousConfigAssignment)
    );
    assert!(!site.editable);
}

#[test]
fn b7b_config_assignment_inside_a_lambda_is_ignored() {
    let source = r#"
#include "cler.hpp"
int main() {
    SourceCWBlock<float> source("Source", 1.0f, 1.0f, 1000);
    SinkNullBlock<float> sink("Sink");
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &sink.in),
        cler::BlockRunner(&sink)
    );
    cler::FlowGraphConfig config;
    auto go_fixed = [&]() { config.scheduler = cler::SchedulerType::FixedThreadPool; };
    flowgraph.run(config);
    go_fixed();
}
"#;
    let site = only(source);
    let config = site.config.as_ref().expect("config");
    assert!(config.assignments.is_empty());
    assert!(config.editable);
    assert!(site.editable);
}

#[test]
fn b8_config_declared_before_the_blocks_still_binds() {
    let source = r#"
#include "cler.hpp"
int main() {
    cler::FlowGraphConfig config;
    config.scheduler = cler::SchedulerType::FixedThreadPool;

    SourceCWBlock<float> source("Source", 1.0f, 1.0f, 1000);
    SinkNullBlock<float> sink("Sink");
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &sink.in),
        cler::BlockRunner(&sink)
    );
    flowgraph.run(config);
}
"#;
    let site = only(source);
    let config = site.config.as_ref().expect("config");
    assert_eq!(config.var.as_deref(), Some("config"));
    assert!(config.editable);
    assert_eq!(config.assignments.len(), 1);
}

#[test]
fn b9_unicode_identifiers() {
    let source = "
#include \"cler.hpp\"
int main() {
    SourceCWBlock<float> quelle(\"Quelle\", 1.0f, 1.0f, 1000);
    GainBlock<float> verstärkung(\"Verstärkung\", 2.0f);
    SinkNullBlock<float> senke(\"Senke\");
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&quelle, &verstärkung.in),
        cler::BlockRunner(&verstärkung, &senke.in),
        cler::BlockRunner(&senke)
    );
    flowgraph.run();
}
";
    let site = only(source);
    assert!(
        site.edges.len() == 2 || !site.editable,
        "either the graph is right or the site is read-only"
    );
}

#[test]
fn b10_unqualified_runners_are_not_dropped() {
    let source = r#"
#include "cler.hpp"
using namespace cler;
int main() {
    SourceCWBlock<float> source("Source", 1.0f, 1.0f, 1000);
    GainBlock<float> gain("Gain", 2.0f);
    SinkNullBlock<float> sink("Sink");
    auto flowgraph = make_desktop_flowgraph(
        BlockRunner(&source, &gain.in),
        BlockRunnerMayBlock(&gain, &sink.in),
        BlockRunner(&sink)
    );
    flowgraph.run();
}
"#;
    let site = only(source);
    assert_eq!(site.runners.len(), 3);
    assert_eq!(site.edges.len(), 2);
    assert!(site.unresolved.is_empty());
    assert!(site.editable);
    assert!(site.runners[1].may_block);
    assert!(!site.runners[0].may_block);
}

#[test]
fn b11_unbound_flowgraph_is_read_only_and_keeps_its_config() {
    let source = r#"
#include "cler.hpp"
int main() {
    SourceCWBlock<float> source("Source", 1.0f, 1.0f, 1000);
    SinkNullBlock<float> sink("Sink");
    cler::FlowGraphConfig config;
    config.scheduler = cler::SchedulerType::FixedThreadPool;
    cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &sink.in),
        cler::BlockRunner(&sink)
    ).run(config);
}
"#;
    let site = only(source);
    assert_eq!(site.blocks.len(), 2);
    assert_eq!(site.edges.len(), 1);
    assert!(!site.editable);
    assert_eq!(site.read_only_reason, Some(Reason::UnboundFlowgraph));
    assert!(site.blocks.iter().all(|b| !b.editable));
    assert!(site.edges.iter().all(|e| !e.editable));
    let config = site.config.as_ref().expect("config still rendered");
    assert_eq!(config.var.as_deref(), Some("config"));
    assert_eq!(config.assignments.len(), 1);
    assert!(!config.editable);
    assert!(config.assignments.iter().all(|a| !a.editable));
}

#[test]
fn b12_constexpr_table_is_scoped_to_its_function() {
    let source = r#"
#include "cler.hpp"
void mode_two() {
    constexpr size_t NUM_CH = 2;
    constexpr size_t LAST_CH = 1;
    AddBlock<float, NUM_CH> adder("Adder");
    PlotTimeSeriesBlock plot("Plot", {"a", "b"}, 1000, 1.0f);
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&adder, &plot.in[LAST_CH]),
        cler::BlockRunner(&plot)
    );
    flowgraph.run();
}

void mode_four() {
    constexpr size_t NUM_CH = 4;
    constexpr size_t LAST_CH = 3;
    AddBlock<float, NUM_CH> adder("Adder");
    PlotTimeSeriesBlock plot("Plot", {"a", "b", "c", "d"}, 1000, 1.0f);
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&adder, &plot.in[LAST_CH]),
        cler::BlockRunner(&plot)
    );
    flowgraph.run();
}
"#;
    let model = parse(source);
    assert_eq!(model.sites.len(), 2);

    let two = model.site_in("mode_two").expect("mode_two");
    let adder = two.block("adder").expect("adder");
    assert_eq!(adder.template_args[1].text, "NUM_CH");
    assert_eq!(adder.template_args[1].resolved.as_deref(), Some("2"));
    assert_eq!(two.edges[0].port.index, Some(1));
    assert!(two.block("plot").expect("plot").in_graph);
    assert!(two.editable);

    let four = model.site_in("mode_four").expect("mode_four");
    assert_eq!(
        four.block("adder").expect("adder").template_args[1]
            .resolved
            .as_deref(),
        Some("4")
    );
    assert_eq!(four.edges[0].port.index, Some(3));
    assert!(four.editable);
}

#[test]
fn b12b_local_constant_wins_over_a_file_scope_one() {
    let source = r#"
#include "cler.hpp"
int main() {
    constexpr size_t NUM_CH = 2;
    AddBlock<float, NUM_CH> adder("Adder");
    SinkNullBlock<float> sink("Sink");
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&adder, &sink.in),
        cler::BlockRunner(&sink)
    );
    flowgraph.run();
}

namespace calibration {
    constexpr size_t NUM_CH = 16;
}
"#;
    let site = only(source);
    let adder = site.block("adder").expect("adder");
    assert_eq!(adder.template_args[1].resolved.as_deref(), Some("2"));
}

#[test]
fn b12c_unwired_non_block_local_is_not_a_canvas_node() {
    let source = r#"
#include "cler.hpp"
int main() {
    MemoryBlock scratch(4096);
    SourceCWBlock<float> source("Source", 1.0f, 1.0f, 1000);
    SinkNullBlock<float> sink("Sink");
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &sink.in),
        cler::BlockRunner(&sink)
    );
    flowgraph.run();
}
"#;
    let site = only(source);
    assert!(site.block("scratch").is_none());
    assert_eq!(site.blocks.len(), 2);
    assert!(site.editable);
}

#[test]
fn unwired_declaration_of_a_tu_defined_block_still_renders() {
    let source = r#"
#include "cler.hpp"
struct ScratchBlock : public cler::BlockBase {
    cler::Channel<float> in;
    ScratchBlock(size_t capacity) : BlockBase("scratch"), in(capacity) {}
};
int main() {
    ScratchBlock scratch(4096);
    SourceCWBlock<float> source("Source", 1.0f, 1.0f, 1000);
    SinkNullBlock<float> sink("Sink");
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &sink.in),
        cler::BlockRunner(&sink)
    );
    flowgraph.run();
}
"#;
    let site = only(source);
    let scratch = site.block("scratch").expect("TU-defined block renders");
    assert!(!scratch.in_graph);
    assert_eq!(site.blocks.len(), 3);
}

#[test]
fn gui_manager_is_not_a_canvas_node() {
    let model = DocumentSession::open(format!("{CORPUS}/hello_world.cpp"))
        .expect("hello_world.cpp opens")
        .parse();
    let site = &model.sites[0];
    assert!(site.block("gui").is_none());
    assert_eq!(site.blocks.len(), 5);
}

#[test]
fn b12d_conditional_emplace_is_flagged() {
    let source = r#"
#include "cler.hpp"
#include <optional>
int main(int argc, char** argv) {
    std::optional<SelectableSourceBlock> source;
    if (argc > 1) {
        source.emplace("File Source", argv[1], 2000000.0);
    } else {
        source.emplace("Radio Source", 1090000000.0, 2400000.0);
    }
    IQToMagnitudeBlock iq2mag("IQ2Mag");
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&*source, &iq2mag.in),
        cler::BlockRunner(&iq2mag)
    );
    flowgraph.run();
}
"#;
    let site = only(source);
    let block = site.block("source").expect("source");
    assert!(!block.editable);
    assert_eq!(
        block.read_only_reason,
        Some(Reason::ConditionalConstruction)
    );
    assert_eq!(block.display_name.as_deref(), Some("File Source"));
    assert_eq!(block.ctor_args.len(), 3);
}

#[test]
fn b12e_struct_local_alias_does_not_leak_into_the_file_table() {
    let source = r#"
#include "cler.hpp"
struct HelperBlock : public cler::BlockBase {
    using Sample = std::complex<float>;
    cler::Channel<Sample> in;
    HelperBlock(const char* name) : BlockBase(name), in(64) {}
};
int main() {
    Sample source("Source", 1.0f, 1.0f, 1000);
    SinkNullBlock<std::complex<float>> sink("Sink");
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &sink.in),
        cler::BlockRunner(&sink)
    );
    flowgraph.run();
}
"#;
    let site = only(source);
    let block = site.block("source").expect("source");
    assert_eq!(block.type_text, "Sample");
    assert_eq!(block.type_name, "Sample");
    assert_eq!(block.alias, None);
}

#[test]
fn b12f_comments_between_runners_keep_indices_aligned() {
    let source = r#"
#include "cler.hpp"
int main() {
    SourceCWBlock<float> source("Source", 1.0f, 1.0f, 1000);
    GainBlock<float> gain("Gain", 2.0f);
    SinkNullBlock<float> sink("Sink");
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, /* to the gain */ &gain.in),
        // the amplifier
        cler::BlockRunner(&gain, &sink.in),
        cler::BlockRunner(&sink)
    );
    flowgraph.run();
}
"#;
    let site = only(source);
    assert_eq!(site.runners.len(), 3);
    let indices: Vec<usize> = site.runners.iter().map(|r| r.index).collect();
    assert_eq!(indices, [0, 1, 2]);
    assert_eq!(site.edges[0].arg_index, 1);
    assert_eq!(site.edges[0].runner_index, 0);
    assert!(site.editable);
}

#[test]
fn b13_display_name_is_unescaped() {
    let source = r#"
#include "cler.hpp"
int main() {
    SourceCWBlock<float> source("Ch \"A\"", 1.0f, 1.0f, 1000);
    SinkNullBlock<float> sink("Path\\to\\sink");
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &sink.in),
        cler::BlockRunner(&sink)
    );
    flowgraph.run();
}
"#;
    let site = only(source);
    assert_eq!(
        site.block("source")
            .expect("source")
            .display_name
            .as_deref(),
        Some("Ch \"A\"")
    );
    assert_eq!(
        site.block("sink").expect("sink").display_name.as_deref(),
        Some("Path\\to\\sink")
    );
    assert!(site.blocks.iter().all(|b| b.editable));
}

#[test]
fn display_name_with_a_non_round_trippable_escape_is_read_only() {
    let source = r#"
#include "cler.hpp"
int main() {
    SourceCWBlock<float> source("Line\nBreak", 1.0f, 1.0f, 1000);
    SinkNullBlock<float> sink("Sink");
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &sink.in),
        cler::BlockRunner(&sink)
    );
    flowgraph.run();
}
"#;
    let site = only(source);
    let block = site.block("source").expect("source");
    assert_eq!(block.display_name.as_deref(), Some("\"Line\\nBreak\""));
    assert!(!block.editable);
    assert_eq!(block.read_only_reason, Some(Reason::UnsupportedDisplayName));
}

#[test]
fn b14_concatenated_display_name_is_read_only() {
    let source = r#"
#include "cler.hpp"
#define TAG "Rx"
int main() {
    SourceCWBlock<float> source(TAG "-Source", 1.0f, 1.0f, 1000);
    SinkNullBlock<float> sink("Sink" "-0");
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &sink.in),
        cler::BlockRunner(&sink)
    );
    flowgraph.run();
}
"#;
    let site = only(source);
    let first = site.block("source").expect("source");
    assert_eq!(first.display_name.as_deref(), Some("TAG \"-Source\""));
    assert!(!first.editable);
    assert_eq!(
        first.read_only_reason,
        Some(Reason::ConcatenatedDisplayName)
    );
    let sink = site.block("sink").expect("sink");
    assert_eq!(sink.display_name.as_deref(), Some("\"Sink\" \"-0\""));
    assert!(!sink.editable);
    assert_eq!(sink.read_only_reason, Some(Reason::ConcatenatedDisplayName));
}

#[test]
fn b23_preprocessor_guarded_declarations_are_ambiguous() {
    let source = r#"
#include "cler.hpp"
int main() {
#if defined(CLER_WITH_UHD)
    SourceUHDBlock<std::complex<float>> src("USRP", 915e6, 2e6, "", 40.0, 1);
#else
    SourceFileBlock<std::complex<float>> src("Recording", "iq.bin");
#endif
    SinkNullBlock<std::complex<float>> sink("Sink");
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&src, &sink.in),
        cler::BlockRunner(&sink)
    );
    flowgraph.run();
}
"#;
    let site = only(source);
    assert_eq!(site.blocks.iter().filter(|b| b.var == "src").count(), 1);
    let src = site.block("src").expect("src");
    assert!(!src.editable);
    assert_eq!(src.read_only_reason, Some(Reason::AmbiguousDeclaration));
    assert!(!site.editable);
}

#[test]
fn b24_preprocessor_inside_the_runner_list_never_claims_editable() {
    let source = r#"
#include "cler.hpp"
int main() {
    SourceCWBlock<float> src("Source", 1.0f, 1.0f, 1000);
    PlotTimeSeriesBlock plot("Plot", {"a"}, 1000, 1.0f);
    SinkNullBlock<float> sink("Sink");
    auto flowgraph = cler::make_desktop_flowgraph(
#ifdef CLER_WITH_GUI
        cler::BlockRunner(&src, &plot.in[0]),
        cler::BlockRunner(&plot)
#else
        cler::BlockRunner(&src, &sink.in),
        cler::BlockRunner(&sink)
#endif
    );
    flowgraph.run();
}
"#;
    let model = parse(source);
    assert!(model.has_errors, "the grammar cannot read this call");
    assert!(model.sites.iter().all(|s| !s.editable));
    if let Some(site) = model.sites.first() {
        assert_eq!(site.read_only_reason, Some(Reason::ParseError));
    }
}

#[test]
fn b25_parse_errors_are_reported_and_pin_every_site() {
    let source = r#"
#include "cler.hpp"
int main() {
    SourceCWBlock<float> src("Source", 1.0f, 1.0f, 1000);
    SinkNullBlock<float> sink("Sink");
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&src, &sink.in),
        cler::BlockRunner(&sink)
    );
    flowgraph.run();
    this is not c++ @@@ ###
}
"#;
    let session = DocumentSession::load(source).expect("loads");
    assert!(session.has_errors());
    let model = session.parse();
    assert!(model.has_errors);
    assert!(!model.errors.is_empty());
    for span in &model.errors {
        assert!(span.end <= source.len() && span.start < span.end);
    }
    let site = &model.sites[0];
    assert!(!site.editable);
    assert_eq!(site.read_only_reason, Some(Reason::ParseError));
    assert!(site.blocks.iter().all(|b| !b.editable));
    let json = serde_json::to_string(&model).expect("model serializes");
    assert!(json.contains("\"has_errors\":true"));
}

#[test]
fn corpus_files_carry_no_parse_errors() {
    let mut stack = vec![std::path::PathBuf::from(CORPUS)];
    let mut checked = 0;
    while let Some(dir) = stack.pop() {
        for entry in std::fs::read_dir(&dir).expect("corpus directory").flatten() {
            let path = entry.path();
            if path.is_dir() {
                stack.push(path);
                continue;
            }
            if path.extension().map(|e| e != "cpp").unwrap_or(true) {
                continue;
            }
            let model = DocumentSession::open(&path)
                .unwrap_or_else(|e| panic!("{}: {e}", path.display()))
                .parse();
            assert!(!model.has_errors, "{} has parse errors", path.display());
            assert!(
                model
                    .sites
                    .iter()
                    .all(|s| s.read_only_reason != Some(Reason::ParseError)),
                "{} pinned by a parse error",
                path.display()
            );
            checked += 1;
        }
    }
    assert!(checked >= 22, "corpus shrank: {checked} files");
}

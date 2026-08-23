use cler_graph::extract_specs;
use cler_graph::palette_types::{BlockSpec, Direction, PortCount, TemplateParamKind};
use std::path::{Path, PathBuf};

const REPO: &str = "../../..";

fn specs_of(relative: &str) -> Vec<BlockSpec> {
    let path = Path::new(REPO).join(relative);
    let source =
        std::fs::read_to_string(&path).unwrap_or_else(|e| panic!("read {}: {e}", path.display()));
    extract_specs(&source, relative)
}

fn spec_named(relative: &str, name: &str) -> BlockSpec {
    specs_of(relative)
        .into_iter()
        .find(|s| s.name == name)
        .unwrap_or_else(|| panic!("{name} not found in {relative}"))
}

fn port_names(spec: &BlockSpec, direction: Direction) -> Vec<&str> {
    spec.ports
        .iter()
        .filter(|p| p.direction == direction)
        .map(|p| p.name.as_str())
        .collect()
}

#[test]
fn add_block_input_count_is_a_template_arg() {
    let spec = spec_named("desktop_blocks/math/add.hpp", "AddBlock");
    assert_eq!(spec.input_count, PortCount::TemplateArg(1));
    assert_eq!(spec.output_count, PortCount::Fixed(1));
    assert_eq!(spec.template_params[1].name, "NumInputs");
    assert_eq!(
        spec.template_params[1].kind,
        TemplateParamKind::NonType {
            param_type: "size_t".to_string()
        }
    );
    let input = spec.port("in").expect("in port");
    assert_eq!(input.direction, Direction::Input);
    assert_eq!(input.element_type, "T");
    assert!(input.variable);
    assert_eq!(port_names(&spec, Direction::Output), vec!["out"]);
    assert!(!spec.may_block);
}

#[test]
fn fanout_output_count_is_a_ctor_arg() {
    let spec = spec_named("desktop_blocks/utils/fanout.hpp", "FanoutBlock");
    assert_eq!(spec.input_count, PortCount::Fixed(1));
    assert_eq!(spec.output_count, PortCount::CtorArg(1));
    assert_eq!(spec.ctor_params[1].name, "num_outputs");
    let input = spec.port("in").expect("in port");
    assert!(!input.variable);
    let outputs = spec.port("outs").expect("outs port");
    assert_eq!(outputs.direction, Direction::Output);
    assert!(outputs.variable);
}

#[test]
fn kaiser_filter_has_usable_constructor_defaults() {
    let spec = spec_named("desktop_blocks/filters/kaiser_lpf.hpp", "KaiserLPFBlock");
    assert_eq!(spec.ctor_params[1].default.as_deref(), Some("1.0e6"));
    assert_eq!(spec.ctor_params[2].default.as_deref(), Some("100.0e3"));
    assert_eq!(spec.ctor_params[3].default.as_deref(), Some("20.0e3"));
}

#[test]
fn plot_input_count_is_a_ctor_arg_length_not_the_slot_cap() {
    let spec = spec_named(
        "desktop_blocks/plots/plot_cspectrum.hpp",
        "PlotCSpectrumBlock",
    );
    assert_eq!(spec.input_count, PortCount::CtorArgLen(1));
    assert_ne!(spec.input_count, PortCount::Fixed(16));
    assert_eq!(spec.output_count, PortCount::Fixed(0));
    assert_eq!(spec.ctor_params[1].name, "signal_labels");
    let input = spec.port("in").expect("in port");
    assert_eq!(input.element_type, "std::complex<float>");
    assert!(input.variable);
    assert_eq!(port_names(&spec, Direction::Output), Vec::<&str>::new());
}

#[test]
fn all_three_plot_blocks_agree_on_the_label_vector() {
    for (file, name) in [
        (
            "desktop_blocks/plots/plot_cspectrum.hpp",
            "PlotCSpectrumBlock",
        ),
        (
            "desktop_blocks/plots/plot_cspectrogram.hpp",
            "PlotCSpectrogramBlock",
        ),
        (
            "desktop_blocks/plots/plot_timeseries.hpp",
            "PlotTimeSeriesBlock",
        ),
    ] {
        let spec = spec_named(file, name);
        assert_eq!(spec.input_count, PortCount::CtorArgLen(1), "{name}");
        assert_ne!(spec.input_count, PortCount::Fixed(16), "{name}");
    }
}

#[test]
fn sink_uhd_input_count_is_a_ctor_arg_with_synonyms() {
    let spec = spec_named("desktop_blocks/sinks/sink_uhd.hpp", "SinkUHDBlock");
    assert_eq!(spec.input_count, PortCount::CtorArg(2));
    assert_eq!(spec.output_count, PortCount::Fixed(0));
    assert_eq!(spec.ctor_params[2].name, "num_channels");
    assert_eq!(spec.ctor_params[2].default.as_deref(), Some("1"));
    assert!(spec.may_block);
    assert!(spec.port("in").expect("in port").variable);
    let synonyms: Vec<&str> = spec.synonyms.iter().map(|s| s.name.as_str()).collect();
    assert_eq!(
        synonyms,
        vec!["SinkUHDBlockCF32", "SinkUHDBlockSC16", "SinkUHDBlockSC8"]
    );
    assert_eq!(spec.synonyms[0].template_args, vec!["std::complex<float>"]);
}

#[test]
fn source_uhd_output_count_is_a_ctor_arg() {
    let spec = spec_named("desktop_blocks/sources/source_uhd.hpp", "SourceUHDBlock");
    assert_eq!(spec.output_count, PortCount::CtorArg(5));
    assert_eq!(spec.input_count, PortCount::Fixed(0));
    assert_eq!(spec.ctor_params[5].name, "num_channels");
    assert!(spec.may_block);
    assert_eq!(port_names(&spec, Direction::Input), Vec::<&str>::new());
    assert!(spec.port("outs").expect("outs port").variable);
}

#[test]
fn channelizer_output_count_is_a_template_arg() {
    let spec = spec_named(
        "desktop_blocks/channelizers/polyphase_channelizer.hpp",
        "PolyphaseChannelizerBlock",
    );
    assert_eq!(spec.output_count, PortCount::TemplateArg(0));
    assert_eq!(spec.input_count, PortCount::Fixed(1));
    assert_eq!(spec.template_params[0].name, "NUM_CHANNELS");
    assert_eq!(
        spec.port("in").expect("in port").element_type,
        "std::complex<float>"
    );
    assert!(spec.port("outs").expect("outs port").variable);
}

#[test]
fn fused_block_is_unknown() {
    let spec = spec_named("desktop_blocks/utils/fused.hpp", "FusedBlock");
    assert_eq!(spec.input_count, PortCount::Unknown);
    assert_eq!(spec.output_count, PortCount::Unknown);
    assert!(spec.template_params[0].pack);
    assert_eq!(spec.ctor_params[1].param_type, "Kernels...");
}

#[test]
fn source_hackrf_has_no_input_ports() {
    let spec = spec_named(
        "desktop_blocks/sources/source_hackrf.hpp",
        "SourceHackRFBlock",
    );
    assert_eq!(port_names(&spec, Direction::Input), Vec::<&str>::new());
    assert!(spec.port("_iq").is_none());
    assert_eq!(port_names(&spec, Direction::Output), vec!["out"]);
    assert_eq!(spec.input_count, PortCount::Fixed(0));
    assert_eq!(spec.output_count, PortCount::Fixed(1));
}

#[test]
fn sink_hackrf_keeps_its_public_input_and_drops_the_private_buffer() {
    let spec = spec_named("desktop_blocks/sinks/sink_hackrf.hpp", "SinkHackRFBlock");
    assert_eq!(port_names(&spec, Direction::Input), vec!["in"]);
    assert!(spec.port("_iq").is_none());
}

#[test]
fn rational_resampler_resolves_its_in_struct_alias() {
    let spec = spec_named(
        "desktop_blocks/resamplers/rational_resampler.hpp",
        "RationalResamplerBlock",
    );
    assert_eq!(
        spec.port("in").expect("in port").element_type,
        "std::complex<float>"
    );
    assert_eq!(
        spec.port("out").expect("out port").element_type,
        "std::complex<float>"
    );
    assert_eq!(spec.input_count, PortCount::Fixed(1));
    assert_eq!(spec.output_count, PortCount::Fixed(1));
    assert_eq!(
        spec.template_params
            .iter()
            .map(|p| p.name.as_str())
            .collect::<Vec<_>>(),
        vec!["INTERP", "DECIM", "TAPS_PER_PHASE"]
    );
}

#[test]
fn gain_is_a_clean_single_port_block() {
    let spec = spec_named("desktop_blocks/math/gain.hpp", "GainBlock");
    assert_eq!(port_names(&spec, Direction::Input), vec!["in"]);
    assert_eq!(port_names(&spec, Direction::Output), vec!["out"]);
    assert!(spec.ports.iter().all(|p| !p.variable));
    assert_eq!(spec.input_count, PortCount::Fixed(1));
    assert_eq!(spec.output_count, PortCount::Fixed(1));
    assert!(!spec.may_block);
    assert_eq!(spec.ctor_params[1].name, "gain_value");
    assert_eq!(spec.ctor_params[1].param_type, "const T");
}

#[test]
fn throttle_declares_may_block() {
    let spec = spec_named("desktop_blocks/utils/throttle.hpp", "ThrottleBlock");
    assert!(spec.may_block);
    assert_eq!(spec.input_count, PortCount::Fixed(1));
    assert_eq!(spec.output_count, PortCount::Fixed(1));
}

#[test]
fn source_file_is_a_may_block_source() {
    let spec = spec_named("desktop_blocks/sources/source_file.hpp", "SourceFileBlock");
    assert!(spec.may_block);
    assert_eq!(port_names(&spec, Direction::Input), Vec::<&str>::new());
    assert_eq!(port_names(&spec, Direction::Output), vec!["out"]);
}

#[test]
fn trigger_block_carries_its_template_default() {
    let spec = spec_named("desktop_blocks/triggers/trigger_block.hpp", "TriggerBlock");
    assert_eq!(spec.template_params[0].default.as_deref(), Some("float"));
    assert_eq!(port_names(&spec, Direction::Input), vec!["in"]);
    assert_eq!(port_names(&spec, Direction::Output), Vec::<&str>::new());
}

#[test]
fn superblock_has_no_inputs_and_procedure_outputs() {
    let spec = spec_named(
        "desktop_examples/polyphase_channelizer.cpp",
        "CustomSourceBlock",
    );
    assert_eq!(port_names(&spec, Direction::Input), Vec::<&str>::new());
    assert_eq!(port_names(&spec, Direction::Output), vec!["out1", "out2"]);
    assert_eq!(spec.input_count, PortCount::Fixed(0));
    assert_eq!(spec.output_count, PortCount::Fixed(2));
}

#[test]
fn method_call_port_degrades_to_unknown() {
    let spec = spec_named("desktop_examples/selectable_blocks.cpp", "switchGainBlock");
    assert_eq!(spec.input_count, PortCount::Unknown);
    assert_eq!(spec.output_count, PortCount::Unknown);
    assert_eq!(port_names(&spec, Direction::Input), Vec::<&str>::new());
}

#[test]
fn multi_channel_inline_blocks_keep_their_port_names() {
    let spec = spec_named("desktop_examples/flowgraph.cpp", "AdderBlock");
    assert_eq!(port_names(&spec, Direction::Input), vec!["in0", "in1"]);
    assert_eq!(spec.port("in1").expect("in1").element_type, "double");
    let plant = spec_named("desktop_examples/mass_spring_damper.cpp", "PlantBlock");
    assert_eq!(port_names(&plant, Direction::Input), vec!["force_in"]);
    assert_eq!(
        port_names(&plant, Direction::Output),
        vec!["measured_position_out"]
    );
}

#[test]
fn an_example_without_inline_blocks_yields_no_specs() {
    assert!(specs_of("desktop_examples/pluto_spectrum.cpp").is_empty());
}

fn headers(dir: &Path, out: &mut Vec<PathBuf>) {
    let mut entries: Vec<PathBuf> = std::fs::read_dir(dir)
        .unwrap_or_else(|e| panic!("read_dir {}: {e}", dir.display()))
        .filter_map(|e| e.ok())
        .map(|e| e.path())
        .collect();
    entries.sort();
    for path in entries {
        if path.is_dir() {
            headers(&path, out);
        } else if path.extension().is_some_and(|e| e == "hpp") {
            out.push(path);
        }
    }
}

type Row = (String, usize, usize, PortCount, PortCount);

fn expected(table: &[(&str, usize, usize, PortCount, PortCount)]) -> Vec<Row> {
    table
        .iter()
        .map(|(name, inputs, outputs, input_count, output_count)| {
            (
                name.to_string(),
                *inputs,
                *outputs,
                *input_count,
                *output_count,
            )
        })
        .collect()
}

const EXPECTED_BLOCKS: &[(&str, usize, usize, PortCount, PortCount)] = &[
    (
        "ADSBAggregateBlock",
        1,
        0,
        PortCount::Fixed(1),
        PortCount::Fixed(0),
    ),
    (
        "ADSBDecoderBlock",
        1,
        1,
        PortCount::Fixed(1),
        PortCount::Fixed(1),
    ),
    (
        "AISDecoderBlock",
        1,
        1,
        PortCount::Fixed(1),
        PortCount::Fixed(1),
    ),
    ("AISMapBlock", 1, 0, PortCount::CtorArg(1), PortCount::Fixed(0)),
    (
        "AFSKDemodBlock",
        1,
        1,
        PortCount::Fixed(1),
        PortCount::Fixed(1),
    ),
    ("APRSMapBlock", 1, 0, PortCount::Fixed(1), PortCount::Fixed(0)),
    (
        "PolyphaseChannelizerBlock",
        1,
        1,
        PortCount::Fixed(1),
        PortCount::TemplateArg(0),
    ),
    (
        "AnalogDemodBlock",
        1,
        1,
        PortCount::Fixed(1),
        PortCount::Fixed(1),
    ),
    (
        "EZGmskDemodBlock",
        1,
        0,
        PortCount::Fixed(1),
        PortCount::Fixed(0),
    ),
    (
        "EZGmskModBlock",
        1,
        1,
        PortCount::Fixed(1),
        PortCount::Fixed(1),
    ),
    (
        "PacketDeframerBlock",
        1,
        1,
        PortCount::Fixed(1),
        PortCount::Fixed(1),
    ),
    (
        "FECDecoderBlock",
        1,
        1,
        PortCount::Fixed(1),
        PortCount::Fixed(1),
    ),
    (
        "FECEncoderBlock",
        1,
        1,
        PortCount::Fixed(1),
        PortCount::Fixed(1),
    ),
    (
        "PacketFramerBlock",
        1,
        1,
        PortCount::Fixed(1),
        PortCount::Fixed(1),
    ),
    (
        "KaiserLPFBlock",
        1,
        1,
        PortCount::Fixed(1),
        PortCount::Fixed(1),
    ),
    (
        "FMDemodBlock",
        1,
        1,
        PortCount::Fixed(1),
        PortCount::Fixed(1),
    ),
    (
        "FMMpxDecoderBlock",
        1,
        1,
        PortCount::Fixed(1),
        PortCount::Fixed(1),
    ),
    (
        "BERCounterBlock",
        1,
        0,
        PortCount::Fixed(1),
        PortCount::Fixed(0),
    ),
    (
        "LinearDemodulatorBlock",
        1,
        2,
        PortCount::Fixed(1),
        PortCount::Fixed(2),
    ),
    (
        "LinearModulatorBlock",
        1,
        1,
        PortCount::Fixed(1),
        PortCount::Fixed(1),
    ),
    (
        "PlotConstellationBlock",
        1,
        0,
        PortCount::Fixed(1),
        PortCount::Fixed(0),
    ),
    (
        "SymbolSourceBlock",
        0,
        1,
        PortCount::Fixed(0),
        PortCount::Fixed(1),
    ),
    (
        "AddBlock",
        1,
        1,
        PortCount::TemplateArg(1),
        PortCount::Fixed(1),
    ),
    (
        "ComplexToMagPhaseBlock",
        1,
        2,
        PortCount::Fixed(1),
        PortCount::Fixed(2),
    ),
    (
        "FrequencyShiftBlock",
        1,
        1,
        PortCount::Fixed(1),
        PortCount::Fixed(1),
    ),
    ("GainBlock", 1, 1, PortCount::Fixed(1), PortCount::Fixed(1)),
    (
        "NoiseAWGNBlock",
        1,
        1,
        PortCount::Fixed(1),
        PortCount::Fixed(1),
    ),
    (
        "PlotCSpectrogramBlock",
        1,
        0,
        PortCount::CtorArgLen(1),
        PortCount::Fixed(0),
    ),
    (
        "PlotCSpectrumBlock",
        1,
        0,
        PortCount::CtorArgLen(1),
        PortCount::Fixed(0),
    ),
    (
        "PlotTimeSeriesBlock",
        1,
        0,
        PortCount::CtorArgLen(1),
        PortCount::Fixed(0),
    ),
    (
        "MultiStageResamplerBlock",
        1,
        1,
        PortCount::Fixed(1),
        PortCount::Fixed(1),
    ),
    (
        "RationalResamplerBlock",
        1,
        1,
        PortCount::Fixed(1),
        PortCount::Fixed(1),
    ),
    (
        "SigMFRecorderBlock",
        1,
        0,
        PortCount::Fixed(1),
        PortCount::Fixed(0),
    ),
    (
        "SinkSigMFBlock",
        1,
        0,
        PortCount::Fixed(1),
        PortCount::Fixed(0),
    ),
    (
        "SourceSigMFBlock",
        0,
        1,
        PortCount::Fixed(0),
        PortCount::Fixed(1),
    ),
    (
        "SinkAudioBlock",
        1,
        0,
        PortCount::Fixed(1),
        PortCount::Fixed(0),
    ),
    (
        "SinkFileBlock",
        1,
        0,
        PortCount::Fixed(1),
        PortCount::Fixed(0),
    ),
    (
        "SinkHackRFBlock",
        1,
        0,
        PortCount::Fixed(1),
        PortCount::Fixed(0),
    ),
    (
        "SinkNullBlock",
        1,
        0,
        PortCount::Fixed(1),
        PortCount::Fixed(0),
    ),
    (
        "SinkSoapySDRBlock",
        1,
        0,
        PortCount::Fixed(1),
        PortCount::Fixed(0),
    ),
    (
        "SinkUHDBlock",
        1,
        0,
        PortCount::CtorArg(2),
        PortCount::Fixed(0),
    ),
    (
        "SourceAudioFileBlock",
        0,
        1,
        PortCount::Fixed(0),
        PortCount::Fixed(1),
    ),
    (
        "SourceCaribouliteBlock",
        0,
        1,
        PortCount::Fixed(0),
        PortCount::Fixed(1),
    ),
    (
        "SourceChirpBlock",
        0,
        1,
        PortCount::Fixed(0),
        PortCount::Fixed(1),
    ),
    (
        "SourceCWBlock",
        0,
        1,
        PortCount::Fixed(0),
        PortCount::Fixed(1),
    ),
    (
        "SourceFileBlock",
        0,
        1,
        PortCount::Fixed(0),
        PortCount::Fixed(1),
    ),
    (
        "SourceHackRFBlock",
        0,
        1,
        PortCount::Fixed(0),
        PortCount::Fixed(1),
    ),
    (
        "SourceMux",
        0,
        1,
        PortCount::Fixed(0),
        PortCount::Fixed(1),
    ),
    (
        "SourcePlutoBlock",
        0,
        1,
        PortCount::Fixed(0),
        PortCount::Fixed(1),
    ),
    (
        "SimSourceBlock",
        0,
        1,
        PortCount::Fixed(0),
        PortCount::Fixed(1),
    ),
    (
        "SourceSoapySDRBlock",
        0,
        1,
        PortCount::Fixed(0),
        PortCount::Fixed(1),
    ),
    (
        "SourceUHDBlock",
        0,
        1,
        PortCount::Fixed(0),
        PortCount::CtorArg(5),
    ),
    (
        "SpectrumBlock",
        1,
        1,
        PortCount::Fixed(1),
        PortCount::Fixed(1),
    ),
    (
        "TriggerBlock",
        1,
        0,
        PortCount::Fixed(1),
        PortCount::Fixed(0),
    ),
    (
        "SinkUDPSocketBlock",
        1,
        0,
        PortCount::Fixed(1),
        PortCount::Fixed(0),
    ),
    (
        "SourceUDPSocketBlock",
        0,
        1,
        PortCount::Fixed(0),
        PortCount::Fixed(1),
    ),
    (
        "FanoutBlock",
        1,
        1,
        PortCount::Fixed(1),
        PortCount::CtorArg(1),
    ),
    ("FusedBlock", 1, 1, PortCount::Unknown, PortCount::Unknown),
    (
        "ThrottleBlock",
        1,
        1,
        PortCount::Fixed(1),
        PortCount::Fixed(1),
    ),
    (
        "ThroughputBlock",
        1,
        1,
        PortCount::Fixed(1),
        PortCount::Fixed(1),
    ),
    (
        "WebSinkBlock",
        2,
        0,
        PortCount::Fixed(2),
        PortCount::Fixed(0),
    ),
];

#[test]
fn every_desktop_block_is_correct_or_unknown() {
    let mut files = Vec::new();
    headers(&Path::new(REPO).join("desktop_blocks"), &mut files);
    let mut found = Vec::new();
    for path in &files {
        let source = std::fs::read_to_string(path).unwrap_or_default();
        for spec in extract_specs(&source, &path.display().to_string()) {
            found.push((
                spec.name.clone(),
                spec.inputs().count(),
                spec.outputs().count(),
                spec.input_count,
                spec.output_count,
            ));
        }
    }
    assert_eq!(found, expected(EXPECTED_BLOCKS));
    let unknown = found
        .iter()
        .filter(|(_, _, _, i, o)| *i == PortCount::Unknown || *o == PortCount::Unknown)
        .count();
    assert_eq!(unknown, 1, "only FusedBlock may be unknown");
}

const EXPECTED_INLINE: &[(&str, usize, usize, PortCount, PortCount)] = &[
    (
        "SourceBlock",
        0,
        2,
        PortCount::Fixed(0),
        PortCount::Fixed(2),
    ),
    ("AdderBlock", 2, 1, PortCount::Fixed(2), PortCount::Fixed(1)),
    ("GainBlock", 1, 1, PortCount::Fixed(1), PortCount::Fixed(1)),
    ("SinkBlock", 1, 0, PortCount::Fixed(1), PortCount::Fixed(0)),
    (
        "SourceOneBlock",
        0,
        1,
        PortCount::Fixed(0),
        PortCount::Fixed(1),
    ),
    (
        "SourceTwoBlock",
        0,
        1,
        PortCount::Fixed(0),
        PortCount::Fixed(1),
    ),
    ("Gain2Block", 1, 1, PortCount::Fixed(1), PortCount::Fixed(1)),
    ("Gain3Block", 1, 1, PortCount::Fixed(1), PortCount::Fixed(1)),
    (
        "switchSourceBlock",
        0,
        1,
        PortCount::Fixed(0),
        PortCount::Fixed(1),
    ),
    (
        "switchGainBlock",
        0,
        1,
        PortCount::Unknown,
        PortCount::Unknown,
    ),
    (
        "SinkPrintBlock",
        1,
        0,
        PortCount::Fixed(1),
        PortCount::Fixed(0),
    ),
    ("PlantBlock", 1, 1, PortCount::Fixed(1), PortCount::Fixed(1)),
    (
        "ControllerBlock",
        1,
        2,
        PortCount::Fixed(1),
        PortCount::Fixed(2),
    ),
    ("RootLocusBlock", 0, 0, PortCount::Fixed(0), PortCount::Fixed(0)),
    (
        "SelectableSourceBlock",
        0,
        1,
        PortCount::Fixed(0),
        PortCount::Fixed(1),
    ),
    (
        "IQToMagnitudeBlock",
        1,
        1,
        PortCount::Fixed(1),
        PortCount::Fixed(1),
    ),
    (
        "CustomSourceBlock",
        0,
        2,
        PortCount::Fixed(0),
        PortCount::Fixed(2),
    ),
    (
        "SourceBlobBlock",
        0,
        1,
        PortCount::Fixed(0),
        PortCount::Fixed(1),
    ),
];

#[test]
fn every_inline_example_block_is_correct_or_unknown() {
    let files = [
        "desktop_examples/flowgraph.cpp",
        "desktop_examples/selectable_blocks.cpp",
        "desktop_examples/mass_spring_damper.cpp",
        
        "desktop_examples/adsb_receiver.cpp",
        "desktop_examples/polyphase_channelizer.cpp",
        "desktop_examples/udp.cpp",
    ];
    let mut found = Vec::new();
    for file in files {
        for spec in specs_of(file) {
            found.push((
                spec.name.clone(),
                spec.inputs().count(),
                spec.outputs().count(),
                spec.input_count,
                spec.output_count,
            ));
        }
    }
    assert_eq!(found, expected(EXPECTED_INLINE));
}

#[test]
fn specs_serialize_to_json() {
    let spec = spec_named("desktop_blocks/utils/fanout.hpp", "FanoutBlock");
    let json = serde_json::to_string(&spec).expect("serialize");
    assert!(json.contains("\"output_count\":{\"ctor_arg\":1}"));
    assert!(json.contains("\"input_count\":{\"fixed\":1}"));
    assert!(json.contains("\"conditional_members\":false"));
    assert!(json.contains("\"direction\":\"output\""));
}

#[test]
fn plot_blocks_are_gui_and_dsp_blocks_are_not() {
    for (file, name) in [
        ("desktop_blocks/plots/plot_timeseries.hpp", "PlotTimeSeriesBlock"),
        ("desktop_blocks/plots/plot_cspectrum.hpp", "PlotCSpectrumBlock"),
        ("desktop_blocks/plots/plot_cspectrogram.hpp", "PlotCSpectrogramBlock"),
    ] {
        assert!(spec_named(file, name).is_gui, "{name} draws a window");
    }
    for (file, name) in [
        ("desktop_blocks/math/gain.hpp", "GainBlock"),
        ("desktop_blocks/utils/fanout.hpp", "FanoutBlock"),
        ("desktop_blocks/sinks/sink_null.hpp", "SinkNullBlock"),
    ] {
        assert!(!spec_named(file, name).is_gui, "{name} has nothing to draw");
    }
}

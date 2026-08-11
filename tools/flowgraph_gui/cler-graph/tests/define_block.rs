use std::process::Command as Process;

use cler_graph::palette_types::{Direction, PortCount};
use cler_graph::{
    extract_specs, ApplyError, BlockParam, BlockSpec, Command, DocumentSession, InputPort,
    Transaction, SCHEMA_VERSION,
};

const CORPUS: &str = "../../../desktop_examples";
const INCLUDE: &str = "../../../include";

fn session(name: &str) -> DocumentSession {
    let path = format!("{CORPUS}/{name}");
    let text = std::fs::read_to_string(&path).unwrap_or_else(|e| panic!("{path}: {e}"));
    DocumentSession::load(text).unwrap_or_else(|e| panic!("{name}: {e}"))
}

fn transaction(base_revision: u64, commands: Vec<Command>) -> Transaction {
    Transaction {
        version: SCHEMA_VERSION.to_string(),
        base_revision,
        commands,
    }
}

fn ports(names: &[&str]) -> Vec<InputPort> {
    names
        .iter()
        .map(|name| InputPort {
            name: (*name).to_string(),
        })
        .collect()
}

fn param(name: &str, cpp_type: &str, default: Option<&str>) -> BlockParam {
    BlockParam {
        name: name.to_string(),
        cpp_type: cpp_type.to_string(),
        default: default.map(str::to_string),
    }
}

fn gain_command() -> Command {
    Command::DefineBlock {
        site: 0,
        name: "MyGainBlock".to_string(),
        value_type: "float".to_string(),
        inputs: ports(&["in"]),
        outputs: 1,
        params: vec![param("gain", "float", None)],
        may_block: false,
    }
}

fn source_command() -> Command {
    Command::DefineBlock {
        site: 0,
        name: "ToneSourceBlock".to_string(),
        value_type: "std::complex<float>".to_string(),
        inputs: Vec::new(),
        outputs: 2,
        params: vec![param("frequency", "float", Some("1.0f"))],
        may_block: false,
    }
}

fn sink_command() -> Command {
    Command::DefineBlock {
        site: 0,
        name: "LoggerSinkBlock".to_string(),
        value_type: "float".to_string(),
        inputs: ports(&["left", "right"]),
        outputs: 0,
        params: Vec::new(),
        may_block: true,
    }
}

fn defined(command: Command) -> (DocumentSession, String) {
    let mut open = session("hello_world.cpp");
    let before = open.source().to_string();
    let outcome = open
        .apply(transaction(0, vec![command]))
        .expect("define_block applies");
    assert_eq!(outcome.splices.len(), 1, "define_block is one insertion");
    let splice = &outcome.splices[0];
    assert_eq!(splice.start, splice.end, "define_block replaces nothing");
    assert_eq!(
        before.len() + splice.text.len(),
        open.source().len(),
        "the file grew by exactly the generated text"
    );
    let text = splice.text.trim_end().to_string();
    (open, text)
}

fn spec(open: &DocumentSession, name: &str) -> BlockSpec {
    extract_specs(open.source(), "open.cpp")
        .into_iter()
        .find(|found| found.name == name)
        .unwrap_or_else(|| panic!("{name} is not discovered by the palette extractor"))
}

fn refusal(command: Command) -> ApplyError {
    let mut open = session("hello_world.cpp");
    let before = open.source().to_string();
    let error = open
        .apply(transaction(0, vec![command]))
        .expect_err("define_block refuses");
    assert_eq!(open.source(), before, "a refusal leaves the file alone");
    assert_eq!(open.revision(), 0, "a refusal does not bump the revision");
    error
}

#[test]
fn the_gain_skeleton_is_generated_verbatim() {
    let (open, text) = defined(gain_command());
    assert_eq!(
        text,
        r#"struct MyGainBlock : public cler::BlockBase {
    cler::Channel<float> in;

    MyGainBlock(const char* name, float gain)
        : cler::BlockBase(name), in(cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(float)), _gain(gain) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out0) {
        // TODO: implement
        auto [in_ptr, in_size] = in.read_dbf();
        auto [out0_ptr, out0_size] = out0->write_dbf();
        size_t transferable = std::min(in_size, out0_size);
        if (transferable == 0) {
            return cler::Error::NotEnoughSpaceOrSamples;
        }

        for (size_t i = 0; i < transferable; ++i) {
            out0_ptr[i] = in_ptr[i];
        }

        in.commit_read(transferable);
        out0->commit_write(transferable);
        return cler::Empty{};
    }

private:
    float _gain;
};"#
    );

    assert_eq!(
        text.matches("//").count(),
        1,
        "the scaffold carries exactly one comment"
    );
    assert!(open
        .source()
        .contains("};\n\nint main() {\n    cler::GuiManager"));
    assert!(!open.parse().has_errors, "the patched file still parses");
}

#[test]
fn the_gain_block_is_discovered_with_fixed_ports() {
    let (open, _) = defined(gain_command());
    let found = spec(&open, "MyGainBlock");

    assert_eq!(found.input_count, PortCount::Fixed(1));
    assert_eq!(found.output_count, PortCount::Fixed(1));
    assert!(!found.may_block);
    assert_eq!(found.template_params, Vec::new());

    let inputs: Vec<&str> = found.inputs().map(|port| port.name.as_str()).collect();
    let outputs: Vec<&str> = found.outputs().map(|port| port.name.as_str()).collect();
    assert_eq!(inputs, ["in"]);
    assert_eq!(outputs, ["out0"]);
    assert!(found
        .ports
        .iter()
        .all(|port| port.element_type == "float" && !port.variable));

    let ctor: Vec<(&str, &str)> = found
        .ctor_params
        .iter()
        .map(|param| (param.param_type.as_str(), param.name.as_str()))
        .collect();
    assert_eq!(ctor, [("const char*", "name"), ("float", "gain")]);
    assert_eq!(found.ctor_params[1].default, None);
}

#[test]
fn a_source_writes_to_every_output_and_reads_nothing() {
    let (open, text) = defined(source_command());
    assert_eq!(
        text,
        r#"struct ToneSourceBlock : public cler::BlockBase {
    ToneSourceBlock(const char* name, float frequency = 1.0f)
        : cler::BlockBase(name), _frequency(frequency) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<std::complex<float>>* out0, cler::ChannelBase<std::complex<float>>* out1) {
        // TODO: implement
        auto [out0_ptr, out0_size] = out0->write_dbf();
        auto [out1_ptr, out1_size] = out1->write_dbf();
        size_t transferable = std::min(out0_size, out1_size);
        if (transferable == 0) {
            return cler::Error::NotEnoughSpace;
        }

        for (size_t i = 0; i < transferable; ++i) {
            out0_ptr[i] = std::complex<float>{};
            out1_ptr[i] = std::complex<float>{};
        }

        out0->commit_write(transferable);
        out1->commit_write(transferable);
        return cler::Empty{};
    }

private:
    float _frequency;
};"#
    );

    let found = spec(&open, "ToneSourceBlock");
    assert_eq!(found.input_count, PortCount::Fixed(0));
    assert_eq!(found.output_count, PortCount::Fixed(2));
    assert_eq!(found.inputs().count(), 0);
    assert!(found
        .outputs()
        .all(|port| port.element_type == "std::complex<float>"));
    assert_eq!(found.ctor_params[1].default.as_deref(), Some("1.0f"));
}

#[test]
fn a_sink_commits_every_read_and_declares_may_block() {
    let (open, text) = defined(sink_command());
    assert_eq!(
        text,
        r#"struct LoggerSinkBlock : public cler::BlockBase {
    static constexpr bool may_block = true;
    cler::Channel<float> left;
    cler::Channel<float> right;

    LoggerSinkBlock(const char* name)
        : cler::BlockBase(name), left(cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(float)), right(cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(float)) {}

    cler::Result<cler::Empty, cler::Error> procedure() {
        // TODO: implement
        auto [left_ptr, left_size] = left.read_dbf();
        auto [right_ptr, right_size] = right.read_dbf();
        size_t transferable = std::min(left_size, right_size);
        if (transferable == 0) {
            return cler::Error::NotEnoughSamples;
        }

        left.commit_read(transferable);
        right.commit_read(transferable);
        return cler::Empty{};
    }
};"#
    );

    let found = spec(&open, "LoggerSinkBlock");
    assert_eq!(found.input_count, PortCount::Fixed(2));
    assert_eq!(found.output_count, PortCount::Fixed(0));
    assert!(found.may_block);
    assert_eq!(
        found
            .ports
            .iter()
            .filter(|port| port.direction == Direction::Output)
            .count(),
        0
    );
}

#[test]
fn the_progress_contract_holds_in_every_shape() {
    for command in [gain_command(), source_command(), sink_command()] {
        let (_, text) = defined(command);
        let body = text
            .split_once("// TODO: implement")
            .expect("the scaffold marks its body")
            .1;
        assert!(
            body.contains("if (transferable == 0) {"),
            "the shortfall guard is missing: {body}"
        );
        let bail = body
            .find("return cler::Error::")
            .expect("the shortfall returns an error");
        let commits = ["commit_read(", "commit_write("];
        for commit in commits {
            if let Some(at) = body.find(commit) {
                assert!(at > bail, "{commit} commits before the shortfall guard");
            }
        }
        assert!(body.contains("return cler::Empty{};"));
    }
}

#[test]
fn a_block_lands_at_file_scope_before_its_function() {
    let (open, _) = defined(gain_command());
    let source = open.source();
    let struct_at = source
        .find("struct MyGainBlock")
        .expect("the struct landed");
    let main_at = source.find("int main()").expect("main survives");
    assert!(struct_at < main_at, "the struct lands before the function");
    assert_eq!(
        source[..struct_at].chars().next_back(),
        Some('\n'),
        "the struct starts its own line"
    );
    assert!(
        source[..struct_at].ends_with("\n\n"),
        "one blank line above"
    );
    assert!(
        source[..main_at].ends_with("};\n\n"),
        "one blank line below"
    );
}

#[test]
fn a_function_with_nothing_above_it_still_gets_its_blank_line() {
    let squeezed = std::fs::read_to_string(format!("{CORPUS}/hello_world.cpp"))
        .expect("the corpus file is readable")
        .replace(
            "gui_manager.hpp\"\n\nint main()",
            "gui_manager.hpp\"\nint main()",
        );
    let mut open = DocumentSession::load(squeezed).expect("the squeezed file parses");
    open.apply(transaction(0, vec![gain_command()]))
        .expect("define_block applies");
    let source = open.source();
    let struct_at = source
        .find("struct MyGainBlock")
        .expect("the struct landed");
    assert!(
        source[..struct_at].ends_with("\n\n"),
        "the generator opened its own blank line"
    );
}

#[test]
fn a_name_known_only_to_the_wider_palette_is_the_callers_to_refuse() {
    let (open, _) = defined(Command::DefineBlock {
        site: 0,
        name: "SinkFileBlock".to_string(),
        value_type: "float".to_string(),
        inputs: ports(&["in"]),
        outputs: 0,
        params: Vec::new(),
        may_block: false,
    });
    assert!(
        open.source().contains("struct SinkFileBlock"),
        "the collision check is translation-unit only: a name owned by a palette \
         header outside the open file is accepted, so the frontend must pre-check \
         define_block names against the palette spec list"
    );
}

#[test]
fn undo_restores_the_file_byte_for_byte() {
    let mut open = session("hello_world.cpp");
    let before = open.source().to_string();
    open.apply(transaction(0, vec![gain_command()]))
        .expect("define_block applies");
    assert_ne!(open.source(), before);
    open.undo().expect("undo rewinds the definition");
    assert_eq!(open.source(), before);
    assert!(open.can_redo());
    open.redo().expect("redo replays the definition");
    assert!(open.source().contains("struct MyGainBlock"));
}

#[test]
fn the_command_round_trips_through_json() {
    let json = r#"{
        "version": "0.3.0",
        "base_revision": 0,
        "commands": [
            {"command": "define_block", "site": 0, "name": "MyGainBlock", "value_type": "float",
             "inputs": [{"name": "in"}], "outputs": 1,
             "params": [{"name": "gain", "cpp_type": "float", "default": "1.0f"}],
             "may_block": false}
        ]
    }"#;
    let parsed: Transaction = serde_json::from_str(json).expect("transaction parses");
    assert_eq!(parsed.version, SCHEMA_VERSION);
    assert_eq!(parsed.commands.len(), 1);

    let sparse = r#"{"command": "define_block", "site": 0, "name": "SinkBlock", "value_type": "float", "inputs": [{"name": "in"}]}"#;
    let lean: Command = serde_json::from_str(sparse).expect("the optional fields default");
    let mut open = session("hello_world.cpp");
    open.apply(transaction(0, vec![lean]))
        .expect("a defaulted define_block applies");
    assert_eq!(spec(&open, "SinkBlock").output_count, PortCount::Fixed(0));

    let unknown = r#"{"command": "define_block", "site": 0, "name": "SinkBlock", "value_type": "float", "outputs": 1, "category": "math"}"#;
    serde_json::from_str::<Command>(unknown).expect_err("unknown fields are rejected");
}

#[test]
fn the_refusal_matrix_holds() {
    let refusals: Vec<(&str, Command)> = vec![
        (
            "name lacks the Block suffix",
            Command::DefineBlock {
                site: 0,
                name: "Gain".to_string(),
                value_type: "float".to_string(),
                inputs: ports(&["in"]),
                outputs: 1,
                params: Vec::new(),
                may_block: false,
            },
        ),
        (
            "name is only the suffix",
            Command::DefineBlock {
                site: 0,
                name: "Block".to_string(),
                value_type: "float".to_string(),
                inputs: ports(&["in"]),
                outputs: 1,
                params: Vec::new(),
                may_block: false,
            },
        ),
        (
            "name is not an identifier",
            Command::DefineBlock {
                site: 0,
                name: "My Gain Block".to_string(),
                value_type: "float".to_string(),
                inputs: ports(&["in"]),
                outputs: 1,
                params: Vec::new(),
                may_block: false,
            },
        ),
        (
            "name collides with a type in the translation unit",
            Command::DefineBlock {
                site: 0,
                name: "AddBlock".to_string(),
                value_type: "float".to_string(),
                inputs: ports(&["in"]),
                outputs: 1,
                params: Vec::new(),
                may_block: false,
            },
        ),
        (
            "value_type is not a type",
            Command::DefineBlock {
                site: 0,
                name: "MyGainBlock".to_string(),
                value_type: "1.0f".to_string(),
                inputs: ports(&["in"]),
                outputs: 1,
                params: Vec::new(),
                may_block: false,
            },
        ),
        (
            "value_type smuggles a declaration",
            Command::DefineBlock {
                site: 0,
                name: "MyGainBlock".to_string(),
                value_type: "float> pwned; using other = int".to_string(),
                inputs: ports(&["in"]),
                outputs: 1,
                params: Vec::new(),
                may_block: false,
            },
        ),
        (
            "a param type is not a type",
            Command::DefineBlock {
                site: 0,
                name: "MyGainBlock".to_string(),
                value_type: "float".to_string(),
                inputs: ports(&["in"]),
                outputs: 1,
                params: vec![param("gain", "not a type", None)],
                may_block: false,
            },
        ),
        (
            "a param default is not an expression",
            Command::DefineBlock {
                site: 0,
                name: "MyGainBlock".to_string(),
                value_type: "float".to_string(),
                inputs: ports(&["in"]),
                outputs: 1,
                params: vec![param("gain", "float", Some("1.0f); std::abort(; (0"))],
                may_block: false,
            },
        ),
        (
            "a param is named after a keyword",
            Command::DefineBlock {
                site: 0,
                name: "MyGainBlock".to_string(),
                value_type: "float".to_string(),
                inputs: ports(&["in"]),
                outputs: 1,
                params: vec![param("delete", "float", None)],
                may_block: false,
            },
        ),
        (
            "an input is named after a keyword",
            Command::DefineBlock {
                site: 0,
                name: "MyGainBlock".to_string(),
                value_type: "float".to_string(),
                inputs: ports(&["operator"]),
                outputs: 1,
                params: Vec::new(),
                may_block: false,
            },
        ),
        (
            "two inputs share a name",
            Command::DefineBlock {
                site: 0,
                name: "MyGainBlock".to_string(),
                value_type: "float".to_string(),
                inputs: ports(&["in", "in"]),
                outputs: 1,
                params: Vec::new(),
                may_block: false,
            },
        ),
        (
            "a param shadows an input",
            Command::DefineBlock {
                site: 0,
                name: "MyGainBlock".to_string(),
                value_type: "float".to_string(),
                inputs: ports(&["in"]),
                outputs: 1,
                params: vec![param("in", "float", None)],
                may_block: false,
            },
        ),
        (
            "an input shadows a generated output parameter",
            Command::DefineBlock {
                site: 0,
                name: "MyGainBlock".to_string(),
                value_type: "float".to_string(),
                inputs: ports(&["out0"]),
                outputs: 1,
                params: Vec::new(),
                may_block: false,
            },
        ),
        (
            "an input shadows the display-name parameter",
            Command::DefineBlock {
                site: 0,
                name: "MyGainBlock".to_string(),
                value_type: "float".to_string(),
                inputs: ports(&["name"]),
                outputs: 1,
                params: Vec::new(),
                may_block: false,
            },
        ),
        (
            "a member name would become a reserved identifier",
            Command::DefineBlock {
                site: 0,
                name: "MyGainBlock".to_string(),
                value_type: "float".to_string(),
                inputs: ports(&["in"]),
                outputs: 1,
                params: vec![param("_gain", "float", None)],
                may_block: false,
            },
        ),
        (
            "the block has no ports at all",
            Command::DefineBlock {
                site: 0,
                name: "MyGainBlock".to_string(),
                value_type: "float".to_string(),
                inputs: Vec::new(),
                outputs: 0,
                params: Vec::new(),
                may_block: false,
            },
        ),
        (
            "the site does not exist",
            Command::DefineBlock {
                site: 9,
                name: "MyGainBlock".to_string(),
                value_type: "float".to_string(),
                inputs: ports(&["in"]),
                outputs: 1,
                params: Vec::new(),
                may_block: false,
            },
        ),
    ];

    let reported: Vec<(&str, String)> = refusals
        .into_iter()
        .map(|(reason, command)| (reason, refusal(command).to_string()))
        .collect();
    let kinds: Vec<&str> = reported
        .iter()
        .map(|(_, error)| {
            error
                .split('"')
                .nth(3)
                .unwrap_or_else(|| panic!("no error tag in {error}"))
        })
        .collect();

    assert_eq!(
        kinds,
        [
            "invalid_block_name",
            "invalid_block_name",
            "invalid_identifier",
            "duplicate_type",
            "invalid_type",
            "invalid_type",
            "invalid_type",
            "invalid_expression",
            "reserved_identifier",
            "reserved_identifier",
            "duplicate_variable",
            "duplicate_variable",
            "duplicate_variable",
            "duplicate_variable",
            "reserved_identifier",
            "unsupported_shape",
            "unknown_site",
        ],
        "{reported:#?}"
    );
}

#[test]
fn the_generated_skeletons_compile() {
    let Ok(version) = Process::new("g++").arg("--version").output() else {
        eprintln!("skipping the compile proof: g++ is not on PATH");
        return;
    };
    assert!(version.status.success(), "g++ --version failed");

    let (_, gain) = defined(gain_command());
    let (_, tone) = defined(source_command());
    let (_, logger) = defined(sink_command());
    let unit = format!(
        "#include \"cler.hpp\"\n#include <complex>\n\n{gain}\n\n{tone}\n\n{logger}\n\n\
         int main() {{\n    \
         MyGainBlock gain(\"gain\", 2.0f);\n    \
         ToneSourceBlock tone(\"tone\");\n    \
         LoggerSinkBlock logger(\"logger\");\n    \
         cler::Channel<float> out(cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(float));\n    \
         auto runner = cler::BlockRunner(&gain, &out);\n    \
         (void)runner;\n    \
         return 0;\n}}\n"
    );

    let path = std::path::Path::new(env!("CARGO_TARGET_TMPDIR")).join("define_block_skeletons.cpp");
    std::fs::write(&path, &unit).expect("the probe translation unit is written");
    let compiled = Process::new("g++")
        .args(["-fsyntax-only", "-std=c++17", "-I", INCLUDE])
        .arg(&path)
        .output()
        .expect("g++ runs");
    assert!(
        compiled.status.success(),
        "the generated skeletons do not compile:\n{}\n--- {} ---\n{unit}",
        String::from_utf8_lossy(&compiled.stderr),
        path.display()
    );
}

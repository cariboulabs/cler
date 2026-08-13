use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicUsize, Ordering};

use cler_flowgraph_gui::ai_agent::{self, Auth, Chunk, Proposal, Turn};
use cler_graph::palette_types::{Direction, Port, PortCount};
use cler_graph::{BlockSpec, Command};
use serde_json::{json, Value};

static COUNTER: AtomicUsize = AtomicUsize::new(0);

const STREAM: &str = r#"event: message_start
data: {"type":"message_start","message":{"id":"msg_01","type":"message","role":"assistant","model":"claude-opus-5","content":[],"usage":{"input_tokens":4211,"output_tokens":1}}}

event: content_block_start
data: {"type":"content_block_start","index":0,"content_block":{"type":"text","text":""}}

event: content_block_delta
data: {"type":"content_block_delta","index":0,"delta":{"type":"thinking_delta","thinking":"weighing the graph"}}

event: content_block_delta
data: {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"chirp (chirp)"}}

event: content_block_delta
data: {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":" feeds the throttle."}}

event: content_block_stop
data: {"type":"content_block_stop","index":0}

event: message_delta
data: {"type":"message_delta","delta":{"stop_reason":"end_turn"},"usage":{"output_tokens":57}}

event: message_stop
data: {"type":"message_stop"}
"#;

const OVERLOADED: &str = r#"event: error
data: {"type":"error","error":{"type":"overloaded_error","message":"Overloaded"}}
"#;

fn temp_dir(name: &str) -> PathBuf {
    let unique = COUNTER.fetch_add(1, Ordering::Relaxed);
    let dir = std::env::temp_dir().join(format!(
        "cler-gui-assistant-{}-{name}-{unique}",
        std::process::id()
    ));
    std::fs::create_dir_all(&dir).expect("temp directory");
    dir
}

fn write_key(dir: &Path, key: &str, mode: u32) -> PathBuf {
    let file = ai_agent::key_path(dir);
    std::fs::write(&file, key).expect("key file");
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        std::fs::set_permissions(&file, std::fs::Permissions::from_mode(mode)).expect("chmod");
    }
    let _ = mode;
    file
}

fn played(transcript: &str) -> Vec<Chunk> {
    transcript.lines().filter_map(ai_agent::chunk).collect()
}

fn spec(name: &str) -> BlockSpec {
    BlockSpec {
        name: name.to_string(),
        origin: "desktop_blocks/sources/cw.hpp".to_string(),
        synonyms: Vec::new(),
        template_params: Vec::new(),
        ctor_params: Vec::new(),
        may_block: true,
        is_gui: false,
        conditional_members: false,
        ports: vec![
            Port {
                name: "in".to_string(),
                direction: Direction::Input,
                element_type: "float".to_string(),
                variable: false,
            },
            Port {
                name: "out0".to_string(),
                direction: Direction::Output,
                element_type: "std::complex<float>".to_string(),
                variable: false,
            },
        ],
        input_count: PortCount::Fixed(1),
        output_count: PortCount::Fixed(1),
    }
}

#[test]
fn without_a_key_the_assistant_is_unavailable_and_says_what_to_do() {
    let dir = temp_dir("absent");
    std::env::remove_var(ai_agent::ANTHROPIC.key_env);

    let status = ai_agent::status(&dir);

    assert!(!status.available, "no key means no assistant");
    assert_eq!(status.model, ai_agent::ANTHROPIC.default_model);
    let reason = status.reason.expect("a reason");
    assert!(reason.contains(ai_agent::ANTHROPIC.key_env), "{reason}");
    assert!(
        reason.contains(&ai_agent::key_path(&dir).display().to_string()),
        "{reason}"
    );
    assert!(reason.contains("chmod 600"), "{reason}");
}

#[test]
fn the_environment_key_outranks_the_key_file() {
    let dir = temp_dir("both");
    write_key(&dir, "sk-file\n", 0o600);

    assert_eq!(
        ai_agent::locate(Some("  sk-env  ".to_string()), &dir),
        Ok(Auth::ApiKey("sk-env".to_string()))
    );
    assert_eq!(
        ai_agent::locate(None, &dir),
        Ok(Auth::ApiKey("sk-file".to_string()))
    );
    assert_eq!(
        ai_agent::locate(Some("   ".to_string()), &dir),
        Ok(Auth::ApiKey("sk-file".to_string())),
        "a blank variable is not a key"
    );
}

#[cfg(unix)]
#[test]
fn a_key_file_other_users_can_read_is_refused() {
    let dir = temp_dir("loose");
    write_key(&dir, "sk-file", 0o644);

    let refusal = ai_agent::locate(None, &dir).expect_err("loose permissions");

    assert!(refusal.contains("chmod 600"), "{refusal}");
    assert!(refusal.contains("644"), "{refusal}");
}

#[test]
fn an_empty_key_file_is_refused_by_name() {
    let dir = temp_dir("empty");
    write_key(&dir, "\n\n", 0o600);

    let refusal = ai_agent::locate(None, &dir).expect_err("empty file");

    assert!(refusal.contains("is empty"), "{refusal}");
}

#[test]
fn the_guide_carries_only_the_sections_the_assistant_needs() {
    let guide = ai_agent::guide();

    for wanted in ["## 1. ", "## 4. ", "## 5. ", "## 6. "] {
        assert!(guide.contains(wanted), "guide is missing {wanted}");
    }
    for skipped in ["## 2. ", "## 3. ", "## 9. ", "## 12. "] {
        assert!(!guide.contains(skipped), "guide should not carry {skipped}");
    }
    assert!(
        guide.contains("read_dbf") || guide.contains("procedure"),
        "the block-writing rules should survive the slice"
    );
    assert!(guide.len() < 24_000, "the guide slice is {} bytes", guide.len());
}

#[test]
fn a_huge_file_is_clamped_and_the_source_is_elided_middle_out() {
    let source = format!(
        "// FIRST LINE\n{}\n// LAST LINE\n",
        "int filler = 0; // padding\n".repeat(20_000)
    );
    let model = format!("{{\"blocks\":\"{}\"}}", "x".repeat(200_000));

    let built = ai_agent::context("/tmp/big.cpp", &model, &source, &[spec("CWSource")]);

    assert!(source.len() > 500_000, "the fixture must exceed the budget");
    assert!(
        built.len() < 60_000,
        "context is {} bytes, over the budget",
        built.len()
    );
    assert!(built.contains("characters elided"), "no elision marker");
    assert!(built.contains("// FIRST LINE"), "the head was dropped");
    assert!(built.contains("// LAST LINE"), "the tail was dropped");
    assert!(built.contains("/tmp/big.cpp"), "the file is not named");
}

#[test]
fn a_small_file_reaches_the_assistant_whole() {
    let source = "int main() { return 0; }\n";

    let built = ai_agent::context("/tmp/small.cpp", "{\"sites\":[]}", source, &[]);

    assert!(built.contains(source), "the source was rewritten");
    assert!(!built.contains("characters elided"), "nothing to elide");
}

#[test]
fn the_palette_reaches_the_assistant_as_names_and_ports() {
    let listed = ai_agent::palette_list(&[spec("CWSource")]);

    assert_eq!(
        listed,
        "CWSource | in: in:float | out: out0:std::complex<float> | may_block"
    );
    assert!(!listed.contains("desktop_blocks"), "origins are not needed");
}

#[test]
fn a_canned_stream_yields_text_then_usage_then_a_stop() {
    let chunks = played(STREAM);

    let text: String = chunks
        .iter()
        .filter_map(|chunk| match chunk {
            Chunk::Text(piece) => Some(piece.as_str()),
            _ => None,
        })
        .collect();
    assert_eq!(text, "chirp (chirp) feeds the throttle.");

    let usage: Vec<&Chunk> = chunks
        .iter()
        .filter(|chunk| matches!(chunk, Chunk::Usage(_, _)))
        .collect();
    assert_eq!(usage, vec![&Chunk::Usage(4211, 1), &Chunk::Usage(0, 57)]);
    assert_eq!(chunks.last(), Some(&Chunk::Done));
    assert!(
        !chunks.iter().any(|chunk| matches!(chunk, Chunk::Failed(_))),
        "a clean stream carries no failure"
    );
}

#[test]
fn an_error_event_in_the_stream_becomes_a_human_sentence() {
    let chunks = played(OVERLOADED);

    assert_eq!(
        chunks,
        vec![Chunk::Failed(
            "the Anthropic API is overloaded right now — try again in a moment".to_string()
        )]
    );
}

#[test]
fn every_error_shape_reads_as_a_sentence() {
    let sentence = |kind: &str| {
        let error: Value =
            serde_json::from_str(&format!(r#"{{"type":"{kind}","message":"nope"}}"#)).unwrap();
        ai_agent::describe(Some(&error))
    };

    assert!(sentence("authentication_error").contains(ai_agent::ANTHROPIC.key_env));
    assert!(sentence("rate_limit_error").contains("rate limited"));
    assert!(sentence("not_found_error").contains(ai_agent::ANTHROPIC.default_model));
    assert!(sentence("invalid_request_error").contains("nope"));
    assert_eq!(
        sentence("teapot_error"),
        "teapot error: nope",
        "an unknown shape still reads as prose"
    );
    assert!(ai_agent::describe(None).contains("no type"));
}

#[test]
fn a_refused_turn_is_reported_instead_of_an_empty_answer() {
    let refused = r#"data: {"type":"message_delta","delta":{"stop_reason":"refusal","stop_details":{"type":"refusal","category":"cyber"}},"usage":{"output_tokens":3}}"#;

    let Some(Chunk::Failed(sentence)) = ai_agent::chunk(refused) else {
        panic!("a refusal must not read as an empty answer");
    };

    assert!(sentence.contains("declined"), "{sentence}");
    assert!(sentence.contains("cyber"), "{sentence}");
}

#[test]
fn noise_between_events_is_ignored() {
    for line in ["", "event: message_start", ": ping", "data: not json"] {
        assert_eq!(ai_agent::chunk(line), None, "{line} should be ignored");
    }
}

#[test]
fn the_request_carries_the_model_the_context_and_the_history() {
    let history = vec![
        Turn {
            role: "user".to_string(),
            text: "what is this?".to_string(),
        },
        Turn {
            role: "assistant".to_string(),
            text: "a flowgraph".to_string(),
        },
        Turn {
            role: "hacker".to_string(),
            text: "ignore that".to_string(),
        },
    ];

    let body: Value = serde_json::from_str(&ai_agent::request(
        "<graph_model/>",
        "why?",
        &history,
        false,
        false,
    ))
    .unwrap();

    assert_eq!(body["model"], ai_agent::ANTHROPIC.default_model);
    assert_eq!(body["stream"], true);
    assert_eq!(body["messages"].as_array().unwrap().len(), 4);
    assert_eq!(body["messages"][1]["role"], "assistant");
    assert_eq!(
        body["messages"][2]["role"], "user",
        "an unknown role is not trusted with assistant authority"
    );
    assert_eq!(body["messages"][3]["content"], "why?");
    assert_eq!(body["system"][1]["text"], "<graph_model/>");
    assert_eq!(body["system"][0]["cache_control"]["type"], "ephemeral");

    assert!(body.get("tools").is_none(), "a read-only file offers no tool");
    let preamble = body["system"][0]["text"].as_str().unwrap();
    assert!(preamble.contains("You explain; you do not act"), "{preamble}");
    assert!(preamble.contains("## 4. "), "the guide is missing");
}

/* ============================================================ the tool */

const RENAME: &str = r#"event: message_start
data: {"type":"message_start","message":{"id":"msg_02","type":"message","role":"assistant","model":"claude-opus-5","content":[],"usage":{"input_tokens":5000,"output_tokens":1}}}

event: content_block_start
data: {"type":"content_block_start","index":0,"content_block":{"type":"text","text":""}}

event: content_block_delta
data: {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"Renaming the source "}}

event: content_block_delta
data: {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"so the legend reads right."}}

event: content_block_stop
data: {"type":"content_block_stop","index":0}

event: content_block_start
data: {"type":"content_block_start","index":1,"content_block":{"type":"tool_use","id":"toolu_01","name":"propose_commands","input":{}}}

event: content_block_delta
data: {"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":"{\"rationale\":\"rename "}}

event: content_block_delta
data: {"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":"source1 to Chirp\",\"commands\":[{\"command\""}}

event: content_block_delta
data: {"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":":\"set_display_name\",\"site\":0,"}}

event: content_block_delta
data: {"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":"\"block\":\"source1\",\"new_text\":\"Chirp\"}]}"}}

event: content_block_stop
data: {"type":"content_block_stop","index":1}

event: message_delta
data: {"type":"message_delta","delta":{"stop_reason":"tool_use"},"usage":{"output_tokens":98}}

event: message_stop
data: {"type":"message_stop"}
"#;

const TWICE: &str = r#"event: content_block_start
data: {"type":"content_block_start","index":0,"content_block":{"type":"tool_use","id":"toolu_01","name":"propose_commands","input":{}}}

event: content_block_delta
data: {"type":"content_block_delta","index":0,"delta":{"type":"input_json_delta","partial_json":"{\"rationale\":\"first\",\"commands\":[{\"command\":\"remove_from_graph\",\"site\":0,\"block\":\"plot\"}]}"}}

event: content_block_stop
data: {"type":"content_block_stop","index":0}

event: content_block_start
data: {"type":"content_block_start","index":1,"content_block":{"type":"tool_use","id":"toolu_02","name":"propose_commands","input":{}}}

event: content_block_delta
data: {"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":"{\"rationale\":\"second\",\"commands\":[{\"command\":\"delete_block\",\"site\":0,\"block\":\"adder\"}]}"}}

event: content_block_stop
data: {"type":"content_block_stop","index":1}

event: message_stop
data: {"type":"message_stop"}
"#;

const DECLINED_MID_TOOL: &str = r#"event: content_block_start
data: {"type":"content_block_start","index":0,"content_block":{"type":"tool_use","id":"toolu_01","name":"propose_commands","input":{}}}

event: content_block_delta
data: {"type":"content_block_delta","index":0,"delta":{"type":"input_json_delta","partial_json":"{\"rationale\":\"half a "}}

event: message_delta
data: {"type":"message_delta","delta":{"stop_reason":"refusal","stop_details":{"type":"refusal","category":"cyber"}},"usage":{"output_tokens":12}}
"#;

const NOT_A_COMMAND: &str = r#"event: content_block_start
data: {"type":"content_block_start","index":0,"content_block":{"type":"tool_use","id":"toolu_01","name":"propose_commands","input":{}}}

event: content_block_delta
data: {"type":"content_block_delta","index":0,"delta":{"type":"input_json_delta","partial_json":"{\"rationale\":\"rewire it\",\"commands\":[{\"command\":\"rewire\",\"site\":0}]}"}}

event: content_block_stop
data: {"type":"content_block_stop","index":0}

event: message_stop
data: {"type":"message_stop"}
"#;

fn spoken(transcript: &str) -> (ai_agent::Reply, String) {
    let mut said = String::new();
    let reply = ai_agent::gather(transcript.lines().map(str::to_string), |text| {
        said.push_str(text);
    });
    (reply, said)
}

fn tool_variants() -> Vec<Value> {
    ai_agent::tool()["input_schema"]["properties"]["commands"]["items"]["anyOf"]
        .as_array()
        .expect("the tool offers a list of command variants")
        .clone()
}

fn kind_of(schema: &Value) -> String {
    match &schema["type"] {
        Value::Array(kinds) => kinds[0].as_str().unwrap_or_default().to_string(),
        other => other.as_str().unwrap_or_default().to_string(),
    }
}

fn sample(schema: &Value) -> Value {
    if let Some(fixed) = schema.get("const") {
        return fixed.clone();
    }
    match kind_of(schema).as_str() {
        "string" => json!("x"),
        "integer" => json!(1),
        "boolean" => json!(true),
        "array" => json!([sample(&schema["items"])]),
        "object" => Value::Object(
            schema["properties"]
                .as_object()
                .expect("an object schema lists its properties")
                .iter()
                .map(|(name, field)| (name.clone(), sample(field)))
                .collect(),
        ),
        other => panic!("no sample value for schema type {other:?}"),
    }
}

fn tag(command: &Command) -> &'static str {
    match command {
        Command::SetParam { .. } => "set_param",
        Command::SetTemplateArg { .. } => "set_template_arg",
        Command::SetDisplayName { .. } => "set_display_name",
        Command::SetConfig { .. } => "set_config",
        Command::Connect { .. } => "connect",
        Command::Disconnect { .. } => "disconnect",
        Command::AddBlock { .. } => "add_block",
        Command::RemoveFromGraph { .. } => "remove_from_graph",
        Command::AddToGraph { .. } => "add_to_graph",
        Command::MaterializeGui { .. } => "materialize_gui",
        Command::DeleteBlock { .. } => "delete_block",
        Command::DefineBlock { .. } => "define_block",
    }
}

#[test]
fn the_tool_schema_matches_the_command_enum_field_for_field() {
    let variants = tool_variants();
    let mut covered: Vec<String> = Vec::new();

    for schema in &variants {
        let named = schema["properties"]["command"]["const"]
            .as_str()
            .expect("every variant pins its command tag")
            .to_string();
        let offered: Vec<String> = schema["properties"]
            .as_object()
            .expect("properties")
            .keys()
            .cloned()
            .collect();
        let required: Vec<String> = schema["required"]
            .as_array()
            .expect("required")
            .iter()
            .map(|name| name.as_str().expect("a field name").to_string())
            .collect();
        assert_eq!(
            schema["additionalProperties"],
            json!(false),
            "{named} invites fields the enum would reject"
        );

        let full = sample(schema);
        let parsed: Command = serde_json::from_value(full.clone())
            .unwrap_or_else(|cause| panic!("{named} does not deserialize: {cause}\n{full:#}"));
        assert_eq!(tag(&parsed), named, "{named} parsed into another variant");
        covered.push(named.clone());

        let emitted: Vec<String> = serde_json::to_value(&parsed)
            .expect("a command serializes")
            .as_object()
            .expect("an object")
            .keys()
            .cloned()
            .collect();
        assert_eq!(
            emitted, offered,
            "{named}: serde carries {emitted:?}, the schema offers {offered:?}"
        );

        for field in &offered {
            let mut without = full.clone();
            without
                .as_object_mut()
                .expect("an object")
                .remove(field.as_str());
            let accepted = serde_json::from_value::<Command>(without).is_ok();
            assert_eq!(
                accepted,
                !required.contains(field),
                "{named}.{field}: the schema calls it required={}, serde needs it={}",
                required.contains(field),
                !accepted
            );
        }

        let mut extra = full;
        extra["surprise"] = json!(1);
        assert!(
            serde_json::from_value::<Command>(extra).is_err(),
            "{named} would silently swallow an unknown field"
        );
    }

    covered.sort();
    covered.dedup();
    assert_eq!(
        covered.len(),
        variants.len(),
        "two schema variants map onto one command"
    );
    assert_eq!(covered.len(), 12, "the schema is missing a command");
}

#[test]
fn the_tool_reaches_the_model_only_when_the_file_can_be_edited() {
    let acting: Value = serde_json::from_str(&ai_agent::request(
        "<graph_model/>",
        "rename it",
        &[],
        true,
        false,
    ))
    .unwrap();
    let reading: Value = serde_json::from_str(&ai_agent::request(
        "<graph_model/>",
        "rename it",
        &[],
        false,
        false,
    ))
    .unwrap();

    assert_eq!(acting["tools"][0]["name"], ai_agent::TOOL_NAME);
    assert_eq!(acting["tools"].as_array().unwrap().len(), 1);
    assert!(reading.get("tools").is_none(), "example mode offers no tool");

    let permitted = acting["system"][0]["text"].as_str().unwrap();
    assert!(permitted.contains("propose_commands"), "{permitted}");
    assert!(permitted.contains("second tool call"), "{permitted}");
    assert!(
        permitted.contains("cannot also be wired in it"),
        "the prompt must name the one-transaction rule the planner enforces: {permitted}"
    );
    assert!(
        !permitted.contains("You explain; you do not act"),
        "the read-only rule leaked into the acting prompt"
    );
}

#[test]
fn a_tool_call_split_across_deltas_becomes_one_proposal_beside_the_text() {
    let (reply, said) = spoken(RENAME);

    assert_eq!(said, "Renaming the source so the legend reads right.");
    assert_eq!(reply.failure, None);
    assert_eq!(reply.dropped, 0);
    assert_eq!(
        reply.proposal,
        Some(Proposal {
            rationale: "rename source1 to Chirp".to_string(),
            commands: vec![json!({
                "command": "set_display_name",
                "site": 0,
                "block": "source1",
                "new_text": "Chirp"
            })]
        })
    );
    assert_eq!((reply.input, reply.output), (5000, 98));
}

#[test]
fn a_second_tool_call_in_one_turn_is_dropped_and_counted() {
    let (reply, _) = spoken(TWICE);

    let proposal = reply.proposal.expect("the first call survives");
    assert_eq!(proposal.rationale, "first");
    assert_eq!(proposal.commands[0]["command"], "remove_from_graph");
    assert_eq!(reply.dropped, 1, "the second call is dropped, not merged");
    assert_eq!(reply.failure, None);
}

#[test]
fn a_refusal_part_way_through_a_tool_call_yields_no_proposal() {
    let (reply, _) = spoken(DECLINED_MID_TOOL);

    assert_eq!(reply.proposal, None, "half a tool call is not a proposal");
    let failure = reply.failure.expect("a refusal is reported");
    assert!(failure.contains("declined"), "{failure}");
    assert!(failure.contains("cyber"), "{failure}");
}

#[test]
fn a_command_the_editor_does_not_have_is_refused_at_the_boundary() {
    let (reply, _) = spoken(NOT_A_COMMAND);

    assert_eq!(reply.proposal, None);
    let failure = reply.failure.expect("an unknown command is reported");
    assert!(failure.contains("cannot run"), "{failure}");
}

#[test]
fn a_plain_answer_carries_no_proposal() {
    let (reply, said) = spoken(STREAM);

    assert_eq!(said, "chirp (chirp) feeds the throttle.");
    assert_eq!(reply.proposal, None);
    assert_eq!(reply.dropped, 0);
    assert_eq!(reply.failure, None);
}

#[test]
fn store_key_normalizes_validates_and_locks_down() {
    let dir = std::env::temp_dir().join(format!("cler-key-{}", std::process::id()));
    std::fs::remove_dir_all(&dir).ok();

    assert!(ai_agent::store_key("   ", &dir).is_err());
    assert!(ai_agent::store_key("not-a-key", &dir).is_err());

    ai_agent::store_key("  sk-ant-test123  ", &dir).expect("valid key stores");
    assert_eq!(
        ai_agent::locate(None, &dir).expect("locate reads file"),
        Auth::ApiKey("sk-ant-test123".to_string())
    );

    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        let mode = std::fs::metadata(ai_agent::key_path(&dir))
            .expect("key file exists")
            .permissions()
            .mode();
        assert_eq!(mode & 0o777, 0o600);
    }

    std::fs::remove_dir_all(&dir).ok();
}

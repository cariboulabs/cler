use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicUsize, Ordering};

use cler_flowgraph_gui::assistant::{self, Chunk, Turn};
use cler_graph::palette_types::{Direction, Port, PortCount};
use cler_graph::BlockSpec;
use serde_json::Value;

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
    let file = assistant::key_path(dir);
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
    transcript.lines().filter_map(assistant::chunk).collect()
}

fn spec(name: &str) -> BlockSpec {
    BlockSpec {
        name: name.to_string(),
        origin: "desktop_blocks/sources/cw.hpp".to_string(),
        synonyms: Vec::new(),
        template_params: Vec::new(),
        ctor_params: Vec::new(),
        may_block: true,
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
    std::env::remove_var(assistant::KEY_ENV);

    let status = assistant::status(&dir);

    assert!(!status.available, "no key means no assistant");
    assert_eq!(status.model, assistant::MODEL);
    let reason = status.reason.expect("a reason");
    assert!(reason.contains(assistant::KEY_ENV), "{reason}");
    assert!(
        reason.contains(&assistant::key_path(&dir).display().to_string()),
        "{reason}"
    );
    assert!(reason.contains("chmod 600"), "{reason}");
}

#[test]
fn the_environment_key_outranks_the_key_file() {
    let dir = temp_dir("both");
    write_key(&dir, "sk-file\n", 0o600);

    assert_eq!(
        assistant::locate(Some("  sk-env  ".to_string()), &dir),
        Ok("sk-env".to_string())
    );
    assert_eq!(assistant::locate(None, &dir), Ok("sk-file".to_string()));
    assert_eq!(
        assistant::locate(Some("   ".to_string()), &dir),
        Ok("sk-file".to_string()),
        "a blank variable is not a key"
    );
}

#[cfg(unix)]
#[test]
fn a_key_file_other_users_can_read_is_refused() {
    let dir = temp_dir("loose");
    write_key(&dir, "sk-file", 0o644);

    let refusal = assistant::locate(None, &dir).expect_err("loose permissions");

    assert!(refusal.contains("chmod 600"), "{refusal}");
    assert!(refusal.contains("644"), "{refusal}");
}

#[test]
fn an_empty_key_file_is_refused_by_name() {
    let dir = temp_dir("empty");
    write_key(&dir, "\n\n", 0o600);

    let refusal = assistant::locate(None, &dir).expect_err("empty file");

    assert!(refusal.contains("is empty"), "{refusal}");
}

#[test]
fn the_guide_carries_only_the_sections_the_assistant_needs() {
    let guide = assistant::guide();

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

    let built = assistant::context("/tmp/big.cpp", &model, &source, &[spec("CWSource")]);

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

    let built = assistant::context("/tmp/small.cpp", "{\"sites\":[]}", source, &[]);

    assert!(built.contains(source), "the source was rewritten");
    assert!(!built.contains("characters elided"), "nothing to elide");
}

#[test]
fn the_palette_reaches_the_assistant_as_names_and_ports() {
    let listed = assistant::palette_list(&[spec("CWSource")]);

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
        assistant::describe(Some(&error))
    };

    assert!(sentence("authentication_error").contains(assistant::KEY_ENV));
    assert!(sentence("rate_limit_error").contains("rate limited"));
    assert!(sentence("not_found_error").contains(assistant::MODEL));
    assert!(sentence("invalid_request_error").contains("nope"));
    assert_eq!(
        sentence("teapot_error"),
        "teapot error: nope",
        "an unknown shape still reads as prose"
    );
    assert!(assistant::describe(None).contains("no type"));
}

#[test]
fn a_refused_turn_is_reported_instead_of_an_empty_answer() {
    let refused = r#"data: {"type":"message_delta","delta":{"stop_reason":"refusal","stop_details":{"type":"refusal","category":"cyber"}},"usage":{"output_tokens":3}}"#;

    let Some(Chunk::Failed(sentence)) = assistant::chunk(refused) else {
        panic!("a refusal must not read as an empty answer");
    };

    assert!(sentence.contains("declined"), "{sentence}");
    assert!(sentence.contains("cyber"), "{sentence}");
}

#[test]
fn noise_between_events_is_ignored() {
    for line in ["", "event: message_start", ": ping", "data: not json"] {
        assert_eq!(assistant::chunk(line), None, "{line} should be ignored");
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

    let body: Value =
        serde_json::from_str(&assistant::request("<graph_model/>", "why?", &history)).unwrap();

    assert_eq!(body["model"], assistant::MODEL);
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

    let preamble = body["system"][0]["text"].as_str().unwrap();
    assert!(preamble.contains("You explain; you do not act"), "{preamble}");
    assert!(preamble.contains("## 4. "), "the guide is missing");
}

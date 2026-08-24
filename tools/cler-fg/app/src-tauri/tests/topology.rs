use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicUsize, Ordering};

use cler_flowgraph_gui::document::{self, DocumentState, Documents};
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
        std::env::temp_dir().join(format!("cler-gui-topology-{}-{unique}", std::process::id()));
    std::fs::create_dir_all(&dir).expect("temp directory");
    let target = dir.join(name);
    std::fs::copy(corpus(name), &target).expect("corpus copy");
    target
}

#[cfg(unix)]
fn with_shipped_blocks(target: &Path) {
    let shipped = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../../../desktop_blocks");
    let link = target.with_file_name("desktop_blocks");
    if !link.exists() {
        std::os::unix::fs::symlink(shipped, link).expect("desktop_blocks link");
    }
}

fn as_str(path: &Path) -> &str {
    path.to_str().expect("utf-8 path")
}

fn text(path: &Path) -> String {
    std::fs::read_to_string(path).expect("readable document")
}

fn opened(name: &str) -> (Documents, PathBuf, DocumentState) {
    let path = temp_copy(name);
    let docs = Documents::default();
    let state = document::open(&docs, as_str(&path)).expect("open");
    (docs, path, state)
}

fn model(docs: &Documents, path: &Path) -> Value {
    let state = document::open(docs, as_str(path)).expect("reopen");
    serde_json::to_value(&state.model).expect("serialisable model")
}

fn site<'a>(model: &'a Value) -> &'a Value {
    &model["sites"][0]
}

fn block<'a>(model: &'a Value, var: &str) -> &'a Value {
    site(model)["blocks"]
        .as_array()
        .expect("blocks")
        .iter()
        .find(|entry| entry["var"] == var)
        .unwrap_or_else(|| panic!("no block {var}"))
}

fn edge_index(model: &Value, from: &str, to: &str, port: &str, index: Option<usize>) -> usize {
    site(model)["edges"]
        .as_array()
        .expect("edges")
        .iter()
        .position(|edge| {
            edge["from"] == from
                && edge["to"] == to
                && edge["port"]["name"] == port
                && edge["port"]["index"] == json!(index)
        })
        .unwrap_or_else(|| panic!("no edge {from} -> {to}.{port}"))
}

fn connect(from: &str, to: &str, port: &str, index: Option<usize>) -> Value {
    json!({
        "command": "connect",
        "site": 0,
        "from": from,
        "to": to,
        "port": port,
        "port_index": index,
    })
}

fn disconnect(edge: usize) -> Value {
    json!({ "command": "disconnect", "site": 0, "edge": edge })
}

fn apply(docs: &Documents, path: &Path, revision: u64, commands: Vec<Value>) -> DocumentState {
    document::apply(docs, as_str(path), revision, commands).expect("transaction applies");
    document::save(docs, as_str(path)).expect("save transaction")
}

fn undo(docs: &Documents, path: &Path) -> DocumentState {
    document::undo(docs, as_str(path)).expect("undo");
    document::save(docs, as_str(path)).expect("save undo")
}

#[cfg(unix)]
#[test]
fn palette_lists_shipped_blocks_and_the_open_translation_unit() {
    let path = temp_copy("mass_spring_damper.cpp");
    with_shipped_blocks(&path);
    let docs = Documents::default();
    document::open(&docs, as_str(&path)).expect("open");
    let specs = document::palette(&docs, as_str(&path)).expect("palette");
    let value = serde_json::to_value(&specs).expect("serialisable specs");
    let listed = value.as_array().expect("array");

    let named = |name: &str| {
        listed
            .iter()
            .find(|spec| spec["name"] == name)
            .unwrap_or_else(|| panic!("no spec {name}"))
            .clone()
    };

    let plant = named("PlantBlock");
    assert_eq!(plant["origin"], json!(path.canonicalize().unwrap().display().to_string()));
    assert_eq!(plant["input_count"], json!({ "fixed": 1 }));

    let fanout = named("FanoutBlock");
    assert_eq!(fanout["output_count"], json!({ "ctor_arg": 1 }));
    assert!(fanout["origin"].as_str().expect("origin").ends_with("fanout.hpp"));

    let plot = named("PlotTimeSeriesBlock");
    assert_eq!(plot["input_count"], json!({ "ctor_arg_len": 1 }));

    assert!(listed.len() > 39, "shipped palette plus TU blocks: {}", listed.len());
}

#[test]
fn palette_needs_an_open_document() {
    let docs = Documents::default();
    let missing = document::palette(&docs, "/nowhere/absent.cpp").expect_err("no session");
    assert!(missing.contains("no open document"), "{missing}");
}

#[test]
fn add_block_declares_without_wiring_it() {
    let (docs, path, state) = opened("hello_world.cpp");
    let runners_before = site(&serde_json::to_value(&state.model).unwrap())["runners"]
        .as_array()
        .expect("runners")
        .len();

    let added = apply(
        &docs,
        &path,
        state.revision,
        vec![json!({
            "command": "add_block",
            "site": 0,
            "type": "GainBlock",
            "template_args": ["float"],
            "ctor_args": ["\"Gain\"", "2.0f"],
            "var_name": "gain",
        })],
    );

    assert!(text(&path).contains("GainBlock<float> gain(\"Gain\", 2.0f);"));
    let after = serde_json::to_value(&added.model).expect("model");
    assert_eq!(block(&after, "gain")["in_graph"], json!(false));
    assert_eq!(
        site(&after)["runners"].as_array().expect("runners").len(),
        runners_before,
        "a declared block does not enter the runner list"
    );

    undo(&docs, &path);
    assert!(!text(&path).contains("GainBlock<float> gain"));
}

#[test]
fn add_block_refuses_a_colliding_variable() {
    let (docs, path, state) = opened("hello_world.cpp");
    let before = text(&path);
    let refusal = document::apply(
        &docs,
        as_str(&path),
        state.revision,
        vec![json!({
            "command": "add_block",
            "site": 0,
            "type": "GainBlock",
            "template_args": ["float"],
            "ctor_args": ["\"Gain\""],
            "var_name": "adder",
        })],
    )
    .expect_err("adder is taken");
    assert!(refusal.contains("duplicate_variable"), "{refusal}");
    assert_eq!(text(&path), before);
}

#[test]
fn the_fanout_output_copatch_is_one_atomic_transaction() {
    let (docs, path, state) = opened("mass_spring_damper.cpp");
    let start = serde_json::to_value(&state.model).expect("model");
    let freed = apply(
        &docs,
        &path,
        state.revision,
        vec![disconnect(edge_index(&start, "throttle", "plant", "force_in", None))],
    );
    let staged = text(&path);
    let before = model(&docs, &path);
    assert_eq!(block(&before, "fanout")["ctor_args"][1]["text"], json!("2"));

    let grown = apply(
        &docs,
        &path,
        freed.revision,
        vec![
            json!({
                "command": "set_param",
                "site": 0,
                "block": "fanout",
                "ctor_arg_index": 1,
                "new_text": "3",
            }),
            connect("fanout", "plant", "force_in", None),
        ],
    );

    assert_eq!(
        grown.revision,
        freed.revision + 1,
        "both commands land in one revision"
    );
    let source = text(&path);
    assert!(source.contains("FanoutBlock<float> fanout(\"Fanout\", 3);"), "{source}");
    assert!(
        source.contains(
            "cler::BlockRunner(&fanout, &plot.in[0], &controller.measured_position_in, &plant.force_in)"
        ),
        "{source}"
    );

    let after = serde_json::to_value(&grown.model).expect("model");
    assert_eq!(block(&after, "fanout")["ctor_args"][1]["text"], json!("3"));

    undo(&docs, &path);
    assert_eq!(text(&path), staged, "one undo reverses the whole gesture");
}

#[test]
fn the_template_arity_copatch_grows_addblock() {
    let (docs, path, state) = opened("hello_world.cpp");
    let start = serde_json::to_value(&state.model).expect("model");
    let freed = apply(
        &docs,
        &path,
        state.revision,
        vec![disconnect(edge_index(&start, "source2", "adder", "in", Some(1)))],
    );
    let staged = text(&path);

    let grown = apply(
        &docs,
        &path,
        freed.revision,
        vec![
            json!({
                "command": "set_template_arg",
                "site": 0,
                "block": "adder",
                "template_arg_index": 1,
                "new_text": "3",
            }),
            connect("source2", "adder", "in", Some(2)),
        ],
    );

    assert_eq!(grown.revision, freed.revision + 1);
    let source = text(&path);
    assert!(source.contains("AddBlock<float, 3> adder(\"Adder\");"), "{source}");
    assert!(source.contains("cler::BlockRunner(&source2, &adder.in[2])"), "{source}");

    undo(&docs, &path);
    assert_eq!(text(&path), staged);
}

#[test]
fn a_reconnect_is_one_transaction_of_disconnect_then_connect() {
    let (docs, path, state) = opened("hello_world.cpp");
    let start = serde_json::to_value(&state.model).expect("model");
    let freed = apply(
        &docs,
        &path,
        state.revision,
        vec![disconnect(edge_index(&start, "source2", "adder", "in", Some(1)))],
    );
    let staged = text(&path);
    let before = model(&docs, &path);

    let moved = apply(
        &docs,
        &path,
        freed.revision,
        vec![
            disconnect(edge_index(&before, "source1", "adder", "in", Some(0))),
            connect("source1", "adder", "in", Some(1)),
        ],
    );

    assert_eq!(moved.revision, freed.revision + 1);
    let source = text(&path);
    assert!(source.contains("cler::BlockRunner(&source1, &adder.in[1])"), "{source}");
    assert!(!source.contains("&adder.in[0]"), "{source}");

    undo(&docs, &path);
    assert_eq!(text(&path), staged);
}

#[test]
fn remove_from_graph_keeps_the_declaration_and_unwires_the_block() {
    let (docs, path, state) = opened("hello_world.cpp");
    let removed = apply(
        &docs,
        &path,
        state.revision,
        vec![json!({ "command": "remove_from_graph", "site": 0, "block": "source1" })],
    );

    let source = text(&path);
    assert!(source.contains("SourceCWBlock<float> source1("), "{source}");
    assert!(!source.contains("cler::BlockRunner(&source1"), "{source}");

    let after = serde_json::to_value(&removed.model).expect("model");
    assert_eq!(
        block(&after, "source1")["in_graph"],
        json!(false),
        "nothing references it any more, so it renders unwired"
    );

    undo(&docs, &path);
    assert!(text(&path).contains("cler::BlockRunner(&source1"));
}

#[test]
fn a_block_that_is_still_a_wire_target_stays_in_the_graph() {
    let (docs, path, state) = opened("hello_world.cpp");
    let removed = apply(
        &docs,
        &path,
        state.revision,
        vec![json!({ "command": "remove_from_graph", "site": 0, "block": "adder" })],
    );

    let after = serde_json::to_value(&removed.model).expect("model");
    assert!(!text(&path).contains("cler::BlockRunner(&adder"));
    assert_eq!(
        block(&after, "adder")["in_graph"],
        json!(true),
        "source1 and source2 still wire into adder.in"
    );
}

#[test]
fn delete_block_refuses_and_names_every_outside_reference() {
    let (docs, path, state) = opened("hello_world.cpp");
    let source = text(&path);

    let refusal = document::apply(
        &docs,
        as_str(&path),
        state.revision,
        vec![json!({ "command": "delete_block", "site": 0, "block": "plot" })],
    )
    .expect_err("plot is used outside the flowgraph");

    let reported: Value = serde_json::from_str(&refusal).expect("json refusal");
    assert_eq!(reported["error"], json!("references_outside_graph"));
    assert_eq!(reported["block"], json!("plot"));
    let spans = reported["spans"].as_array().expect("spans");
    assert!(!spans.is_empty());
    for span in spans {
        let start = span["start"].as_u64().expect("start") as usize;
        let end = span["end"].as_u64().expect("end") as usize;
        assert_eq!(&source[start..end], "plot");
    }
    assert_eq!(text(&path), source, "a refusal never touches the file");
}

#[test]
fn delete_block_removes_a_block_nothing_else_mentions() {
    let (docs, path, state) = opened("hello_world.cpp");
    let deleted = apply(
        &docs,
        &path,
        state.revision,
        vec![json!({ "command": "delete_block", "site": 0, "block": "source2" })],
    );

    let source = text(&path);
    assert!(!source.contains("source2"), "{source}");
    let after = serde_json::to_value(&deleted.model).expect("model");
    assert!(site(&after)["blocks"]
        .as_array()
        .expect("blocks")
        .iter()
        .all(|entry| entry["var"] != "source2"));

    undo(&docs, &path);
    assert!(text(&path).contains("source2"));
}

#[test]
fn a_disconnect_leaves_the_block_declared_but_unwired() {
    let (docs, path, state) = opened("hello_world.cpp");
    let start = serde_json::to_value(&state.model).expect("model");
    let cut = apply(
        &docs,
        &path,
        state.revision,
        vec![disconnect(edge_index(&start, "source2", "adder", "in", Some(1)))],
    );

    let source = text(&path);
    assert!(source.contains("cler::BlockRunner(&source2)"), "{source}");
    let after = serde_json::to_value(&cut.model).expect("model");
    assert_eq!(block(&after, "source2")["in_graph"], json!(true));
    assert!(site(&after)["edges"]
        .as_array()
        .expect("edges")
        .iter()
        .all(|edge| edge["from"] != "source2"));
}

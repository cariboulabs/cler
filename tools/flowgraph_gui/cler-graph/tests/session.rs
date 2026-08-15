use cler_graph::session::{self, Document, Edited, NodeMove, Point};
use cler_graph::PatchDirection;
use serde_json::json;

const SOURCE: &str = "int main() { return 0; }\n";

fn document() -> Document {
    Document::load(SOURCE.to_string()).expect("session loads")
}

fn movement(node: &str, from: (f64, f64), to: (f64, f64)) -> NodeMove {
    NodeMove {
        node: node.to_string(),
        from: Point {
            x: from.0,
            y: from.1,
        },
        to: Point { x: to.0, y: to.1 },
    }
}

#[test]
fn an_edit_undoes_back_to_the_saved_bytes_and_redoes_forward() {
    let mut doc = document();
    let edited = "int main() { return 7; }\n";
    assert!(matches!(
        session::edit(&mut doc, 0, edited).expect("edit applies"),
        Edited::Applied
    ));
    assert_eq!(doc.session.source(), edited);

    session::step(&mut doc, PatchDirection::Reverse).expect("undo");
    assert_eq!(doc.session.source(), SOURCE);
    let state = session::snapshot("doc.cpp", &doc, &[]);
    assert!(!state.dirty);
    assert!(state.can_redo && !state.can_undo);

    session::step(&mut doc, PatchDirection::Forward).expect("redo");
    assert_eq!(doc.session.source(), edited);
    assert!(session::step(&mut doc, PatchDirection::Forward)
        .expect_err("nothing left")
        .contains("nothing_to_redo"));
}

#[test]
fn an_edit_against_a_stale_revision_is_refused() {
    let mut doc = document();
    assert!(
        session::edit(&mut doc, 4, "int main() { return 1; }\n")
            .expect_err("stale base")
            .contains("revision"),
        "the caller must be told which revision it missed"
    );
}

#[test]
fn text_the_parser_rejects_never_enters_the_session() {
    let mut doc = document();
    let outcome = session::edit(&mut doc, 0, "int main() { return 1;\n").expect("reported, not an error");
    assert!(matches!(outcome, Edited::Unparsed(Some(_))));
    assert_eq!(doc.session.source(), SOURCE);
    assert!(!session::snapshot("doc.cpp", &doc, &[]).can_undo);
}

#[test]
fn node_movement_refuses_what_it_cannot_place() {
    let mut doc = document();
    assert!(session::move_nodes(&mut doc, "", vec![movement("a", (0.0, 0.0), (1.0, 1.0))])
        .expect_err("no view")
        .contains("requires a view"));
    assert!(
        session::move_nodes(&mut doc, "main", vec![movement("", (0.0, 0.0), (1.0, 1.0))])
            .expect_err("no node")
            .contains("requires a node")
    );
    assert!(session::move_nodes(
        &mut doc,
        "main",
        vec![movement("a", (0.0, 0.0), (f64::NAN, 1.0))]
    )
    .expect_err("not a place")
    .contains("finite"));
    assert!(session::move_nodes(
        &mut doc,
        "main",
        vec![
            movement("a", (0.0, 0.0), (1.0, 1.0)),
            movement("a", (0.0, 0.0), (2.0, 2.0)),
        ]
    )
    .expect_err("two destinations for one node")
    .contains("repeats a"));
    assert!(
        !session::snapshot("doc.cpp", &doc, &[]).can_undo,
        "a refused movement leaves no history behind"
    );
}

#[test]
fn a_no_op_movement_moves_nothing_and_records_nothing() {
    let mut doc = document();
    session::move_nodes(&mut doc, "main", vec![movement("a", (1.0, 2.0), (1.0, 2.0))])
        .expect("accepted");
    let state = session::snapshot("doc.cpp", &doc, &[]);
    assert!(!state.can_undo);
    assert_eq!(state.cache, json!({}));
}

#[test]
fn source_and_position_actions_walk_one_history() {
    let mut doc = document();
    session::edit(&mut doc, 0, "int main() { return 7; }\n").expect("edit");
    session::move_nodes(&mut doc, "main", vec![movement("a", (0.0, 0.0), (10.0, 20.0))])
        .expect("move");
    assert_eq!(doc.ui["views"]["main"]["positions"]["a"]["x"], 10.0);

    session::step(&mut doc, PatchDirection::Reverse).expect("undo the move");
    assert_eq!(doc.ui["views"]["main"]["positions"]["a"]["x"], 0.0);
    assert_eq!(doc.session.source(), "int main() { return 7; }\n");

    session::step(&mut doc, PatchDirection::Reverse).expect("undo the edit");
    assert_eq!(doc.session.source(), SOURCE);
    assert!(session::step(&mut doc, PatchDirection::Reverse)
        .expect_err("nothing left")
        .contains("nothing_to_undo"));
}

#[test]
fn the_desktop_gate_can_tell_a_position_action_from_a_source_action() {
    let mut doc = document();
    assert_eq!(
        session::pending_action_edits_source(&doc, PatchDirection::Reverse),
        None
    );
    session::edit(&mut doc, 0, "int main() { return 7; }\n").expect("edit");
    session::move_nodes(&mut doc, "main", vec![movement("a", (0.0, 0.0), (10.0, 20.0))])
        .expect("move");
    assert_eq!(
        session::pending_action_edits_source(&doc, PatchDirection::Reverse),
        Some(false)
    );
    session::step(&mut doc, PatchDirection::Reverse).expect("undo the move");
    assert_eq!(
        session::pending_action_edits_source(&doc, PatchDirection::Reverse),
        Some(true)
    );
    assert_eq!(
        session::pending_action_edits_source(&doc, PatchDirection::Forward),
        Some(false)
    );
}

use cler_flowgraph_gui::editor_command;

const PATH: &str = "/tmp/flowgraphs/hello_world.cpp";

fn plan(editor: &str) -> Option<(String, Vec<String>)> {
    editor_command(editor, PATH, 42)
}

#[test]
fn a_terminal_editor_asks_for_the_generic_opener_instead() {
    for editor in ["vim", "nvim", "nano", "hx", "/usr/bin/vi", "emacs -Q"] {
        assert_eq!(plan(editor), None, "{editor} needs a terminal");
    }
}

#[test]
fn the_code_family_jumps_with_goto() {
    assert_eq!(
        plan("code"),
        Some((
            "code".to_string(),
            vec!["--goto".to_string(), format!("{PATH}:42")]
        ))
    );
    assert_eq!(
        plan("/opt/cursor/cursor"),
        Some((
            "/opt/cursor/cursor".to_string(),
            vec!["--goto".to_string(), format!("{PATH}:42")]
        ))
    );
}

#[test]
fn the_remaining_shapes_each_get_their_own_line_flag() {
    assert_eq!(
        plan("subl"),
        Some(("subl".to_string(), vec![format!("{PATH}:42")]))
    );
    assert_eq!(
        plan("gvim"),
        Some((
            "gvim".to_string(),
            vec!["+42".to_string(), PATH.to_string()]
        ))
    );
    assert_eq!(
        plan("kate"),
        Some((
            "kate".to_string(),
            vec!["--line".to_string(), "42".to_string(), PATH.to_string()]
        ))
    );
}

#[test]
fn arguments_already_in_the_variable_come_first_and_survive() {
    assert_eq!(
        plan("code --wait"),
        Some((
            "code".to_string(),
            vec![
                "--wait".to_string(),
                "--goto".to_string(),
                format!("{PATH}:42")
            ]
        ))
    );
}

#[test]
fn an_unknown_editor_still_gets_the_file_without_a_line() {
    assert_eq!(
        plan("my-strange-editor"),
        Some((
            "my-strange-editor".to_string(),
            vec![PATH.to_string()]
        ))
    );
    assert_eq!(plan(""), None);
    assert_eq!(plan("   "), None);
}

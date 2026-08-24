use super::splice::Splice;

pub(super) fn include_splices(source: &str, includes: &[String]) -> Vec<Splice> {
    let mut missing: Vec<&str> = includes
        .iter()
        .map(String::as_str)
        .filter(|path| {
            !source.lines().any(|line| {
                let line = line.trim();
                line == format!("#include \"{path}\"") || line == format!("#include <{path}>")
            })
        })
        .collect();
    missing.sort_unstable();
    missing.dedup();
    if missing.is_empty() {
        return Vec::new();
    }
    let mut at = 0;
    let mut offset = 0;
    for line in source.split_inclusive('\n') {
        offset += line.len();
        if line.trim_start().starts_with("#include") {
            at = offset;
        }
    }
    let mut text = if at > 0 && !source[..at].ends_with('\n') {
        "\n".to_string()
    } else {
        String::new()
    };
    text.push_str(&missing
        .into_iter()
        .map(|path| format!("#include \"{path}\"\n"))
        .collect::<String>());
    vec![Splice::insert(at, text)]
}

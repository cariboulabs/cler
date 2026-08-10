use std::path::Path;
use std::process::Command;

const BIN_ENV: &str = "CLER_GRAPH_BIN";

pub fn parse(path: &Path) -> Result<String, String> {
    let bin = std::env::var(BIN_ENV)
        .map_err(|_| format!("{BIN_ENV} is not set; point it at the cler-graph binary"))?;

    let output = Command::new(&bin)
        .arg("parse")
        .arg(path)
        .output()
        .map_err(|err| format!("failed to run {bin}: {err}"))?;

    if !output.status.success() {
        let stderr = String::from_utf8_lossy(&output.stderr).trim().to_string();
        return Err(if stderr.is_empty() {
            format!("{bin} exited with {}", output.status)
        } else {
            stderr
        });
    }

    String::from_utf8(output.stdout).map_err(|err| format!("{bin} emitted invalid utf-8: {err}"))
}

use std::path::{Path, PathBuf};
use std::process::ExitCode;

use clap::{Parser, Subcommand};
use serde::Serialize;
use sha2::{Digest, Sha256};

use cler_graph::model::Site;
use cler_graph::palette_types::{Direction, PortCount};
use cler_graph::{
    palette_specs, source_files, BlockSpec, DocumentSession, Error, FileModel, Splice, Transaction,
    SCHEMA_VERSION,
};

#[derive(Parser)]
#[command(name = "cler-graph", about = "Parse cler desktop flowgraph sources")]
struct Cli {
    #[command(subcommand)]
    command: Command,
}

#[derive(Subcommand)]
enum Command {
    Parse {
        file: PathBuf,
        #[arg(long)]
        pretty: bool,
        #[arg(long, value_name = "DIR")]
        palette: Vec<PathBuf>,
    },
    Corpus {
        dir: PathBuf,
    },
    Apply {
        file: PathBuf,
        #[arg(long)]
        transaction: String,
        #[arg(long)]
        dry_run: bool,
        #[arg(
            long,
            conflicts_with = "unguarded",
            required_unless_present = "unguarded"
        )]
        expect_sha256: Option<String>,
        #[arg(long)]
        unguarded: bool,
    },
    Palette {
        #[arg(required = true)]
        paths: Vec<PathBuf>,
        #[arg(long)]
        table: bool,
    },
}

fn main() -> ExitCode {
    let cli = Cli::parse();
    let result = match cli.command {
        Command::Parse {
            file,
            pretty,
            palette,
        } => parse_command(&file, pretty, &palette),
        Command::Corpus { dir } => corpus_command(&dir),
        Command::Apply {
            file,
            transaction,
            dry_run,
            expect_sha256,
            unguarded,
        } => guarded_digest(expect_sha256, unguarded)
            .and_then(|expected| apply_command(&file, &transaction, dry_run, expected.as_deref())),
        Command::Palette { paths, table } => palette_command(&paths, table),
    };
    match result {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("cler-graph: {error}");
            ExitCode::FAILURE
        }
    }
}

type Failure = Box<dyn std::error::Error>;

const CONTEXT_LINES: usize = 3;

fn line_index(text: &str, offset: usize) -> usize {
    text.get(..offset).unwrap_or(text).matches('\n').count()
}

fn line_start(text: &str, offset: usize) -> usize {
    text.get(..offset)
        .and_then(|head| head.rfind('\n'))
        .map(|index| index + 1)
        .unwrap_or(0)
}

fn line_end(text: &str, offset: usize) -> usize {
    text.get(offset..)
        .and_then(|tail| tail.find('\n'))
        .map(|index| offset + index + 1)
        .unwrap_or(text.len())
}

fn on_line_boundary(text: &str, offset: usize) -> bool {
    offset == 0 || text.as_bytes().get(offset - 1) == Some(&b'\n')
}

fn line_bound(text: &str, offset: usize) -> usize {
    let counted = line_index(text, offset);
    if on_line_boundary(text, offset) {
        counted
    } else {
        counted + 1
    }
}

struct Document<'a> {
    lines: Vec<&'a str>,
    complete: bool,
}

fn document(text: &str) -> Document<'_> {
    Document {
        lines: if text.is_empty() {
            Vec::new()
        } else {
            text.strip_suffix('\n')
                .unwrap_or(text)
                .split('\n')
                .collect()
        },
        complete: text.is_empty() || text.ends_with('\n'),
    }
}

impl Document<'_> {
    fn push(&self, out: &mut String, mark: char, index: usize) {
        let Some(line) = self.lines.get(index) else {
            return;
        };
        out.push(mark);
        out.push_str(line);
        out.push('\n');
        if !self.complete && index + 1 == self.lines.len() {
            out.push_str("\\ No newline at end of file\n");
        }
    }
}

struct Hunk {
    old: (usize, usize),
    new: (usize, usize),
}

fn hunks(before: &str, after: &str, splices: &[Splice], lines: (&[&str], &[&str])) -> Vec<Hunk> {
    let (old_lines, new_lines) = lines;
    let mut hunks: Vec<Hunk> = Vec::new();
    let mut delta: isize = 0;
    for splice in splices {
        let low = line_start(before, splice.start);
        let high = if on_line_boundary(before, splice.end)
            && (splice.end > splice.start || splice.start == low)
        {
            splice.end
        } else {
            line_end(before, splice.end)
        };
        let grown = splice.text.len() as isize - (splice.end - splice.start) as isize;
        let new_low = (low as isize + delta).max(0) as usize;
        let new_high =
            (new_low as isize + (high - low) as isize + grown).max(new_low as isize) as usize;
        delta += grown;

        let mut old = (line_index(before, low), line_bound(before, high));
        let mut new = (line_index(after, new_low), line_bound(after, new_high));
        while old.0 < old.1 && new.0 < new.1 && old_lines.get(old.0) == new_lines.get(new.0) {
            old.0 += 1;
            new.0 += 1;
        }
        while old.1 > old.0 && new.1 > new.0 && old_lines.get(old.1 - 1) == new_lines.get(new.1 - 1)
        {
            old.1 -= 1;
            new.1 -= 1;
        }
        if old.0 == old.1 && new.0 == new.1 {
            continue;
        }
        match hunks.last_mut() {
            Some(last) if old.0 <= last.old.1 + 2 * CONTEXT_LINES => {
                last.old.1 = last.old.1.max(old.1);
                last.new.1 = last.new.1.max(new.1);
            }
            _ => hunks.push(Hunk { old, new }),
        }
    }
    hunks
}

fn first_line(begin: usize, count: usize) -> usize {
    if count == 0 {
        begin
    } else {
        begin + 1
    }
}

fn unified_diff(path: &Path, before: &str, after: &str, splices: &[Splice]) -> String {
    let old = document(before);
    let new = document(after);
    let shown = path.display();
    let mut out = format!("--- a/{shown}\n+++ b/{shown}\n");
    for hunk in hunks(before, after, splices, (&old.lines, &new.lines)) {
        let lead = CONTEXT_LINES.min(hunk.old.0).min(hunk.new.0);
        let trail = CONTEXT_LINES
            .min(old.lines.len() - hunk.old.1)
            .min(new.lines.len() - hunk.new.1);
        let old_count = hunk.old.1 - hunk.old.0 + lead + trail;
        let new_count = hunk.new.1 - hunk.new.0 + lead + trail;
        out.push_str(&format!(
            "@@ -{},{} +{},{} @@\n",
            first_line(hunk.old.0 - lead, old_count),
            old_count,
            first_line(hunk.new.0 - lead, new_count),
            new_count
        ));
        for index in hunk.old.0 - lead..hunk.old.0 {
            old.push(&mut out, ' ', index);
        }
        for index in hunk.old.0..hunk.old.1 {
            old.push(&mut out, '-', index);
        }
        for index in hunk.new.0..hunk.new.1 {
            new.push(&mut out, '+', index);
        }
        for index in hunk.old.1..hunk.old.1 + trail {
            old.push(&mut out, ' ', index);
        }
    }
    out
}

#[cfg(unix)]
fn inherit_owner(original: &std::fs::Metadata, temp: &Path) {
    use std::os::unix::fs::MetadataExt;
    std::os::unix::fs::chown(temp, Some(original.uid()), Some(original.gid())).ok();
}

#[cfg(not(unix))]
fn inherit_owner(_original: &std::fs::Metadata, _temp: &Path) {}

fn refuse_unless_writable(path: &Path) -> Result<(), Failure> {
    std::fs::OpenOptions::new()
        .write(true)
        .open(path)
        .map_err(|cause| format!("{} is not writable: {cause}", path.display()))?;
    Ok(())
}

fn write_atomic(path: &Path, contents: &str) -> Result<(), Failure> {
    let target = path
        .canonicalize()
        .map_err(|cause| format!("cannot resolve {}: {cause}", path.display()))?;
    refuse_unless_writable(&target)?;
    let original = std::fs::metadata(&target)?;
    let name = target
        .file_name()
        .map(|name| name.to_string_lossy().to_string())
        .unwrap_or_else(|| "document".to_string());
    let temp = target.with_file_name(format!(".{name}.cler-graph.tmp"));
    std::fs::write(&temp, contents)?;
    std::fs::set_permissions(&temp, original.permissions())?;
    inherit_owner(&original, &temp);
    std::fs::rename(&temp, &target)?;
    Ok(())
}

fn digest(bytes: &[u8]) -> String {
    format!("{:x}", Sha256::digest(bytes))
}

fn guarded_digest(
    expect_sha256: Option<String>,
    unguarded: bool,
) -> Result<Option<String>, Failure> {
    match (expect_sha256, unguarded) {
        (Some(_), true) => Err("--expect-sha256 and --unguarded cannot be combined".into()),
        (None, false) => Err("apply needs --expect-sha256 <hex> or --unguarded".into()),
        (expected, _) => Ok(expected),
    }
}

fn read_source(path: &Path) -> Result<String, Failure> {
    std::fs::read_to_string(path).map_err(|cause| {
        Failure::from(Error::Read {
            path: path.to_path_buf(),
            cause,
        })
    })
}

fn apply_command(
    file: &Path,
    transaction: &str,
    dry_run: bool,
    expected: Option<&str>,
) -> Result<(), Failure> {
    let before = read_source(file)?;
    if let Some(expected) = expected {
        let found = digest(before.as_bytes());
        if !found.eq_ignore_ascii_case(expected) {
            return Err(format!(
                "{} changed on disk: expected sha256 {expected}, found {found}",
                file.display()
            )
            .into());
        }
    }
    let transaction: Transaction = serde_json::from_str(transaction)?;
    let mut session = DocumentSession::load(before.clone())?;
    let outcome = session.apply(transaction)?;
    print!(
        "{}",
        unified_diff(file, &before, session.source(), &outcome.splices)
    );
    if !dry_run {
        write_atomic(file, session.source())?;
    }
    Ok(())
}

#[derive(Serialize)]
struct ParseOutput<'a> {
    sha256: String,
    #[serde(flatten)]
    model: &'a FileModel,
}

fn parse_command(file: &Path, pretty: bool, palette: &[PathBuf]) -> Result<(), Failure> {
    let session = DocumentSession::open(file)?;
    let model = session.parse_with_palette(&palette_specs(palette)?);
    let output = ParseOutput {
        sha256: digest(session.source().as_bytes()),
        model: &model,
    };
    let json = if pretty {
        serde_json::to_string_pretty(&output)
    } else {
        serde_json::to_string(&output)
    };
    match json {
        Ok(text) => {
            println!("{text}");
            Ok(())
        }
        Err(cause) => {
            eprintln!("cler-graph: cannot serialize model: {cause}");
            Ok(())
        }
    }
}

fn ports(spec: &BlockSpec, direction: Direction) -> String {
    let listed: Vec<String> = spec
        .ports
        .iter()
        .filter(|p| p.direction == direction)
        .map(|p| {
            format!(
                "{}{}: {}",
                p.name,
                if p.variable { "[]" } else { "" },
                p.element_type
            )
        })
        .collect();
    if listed.is_empty() {
        "-".to_string()
    } else {
        listed.join(", ")
    }
}

fn synonyms(spec: &BlockSpec) -> String {
    if spec.synonyms.is_empty() {
        return String::new();
    }
    let names: Vec<&str> = spec.synonyms.iter().map(|s| s.name.as_str()).collect();
    format!(" | synonyms: {}", names.join(","))
}

fn palette_table(specs: &[BlockSpec]) {
    let mut unknown = 0;
    for spec in specs {
        if spec.input_count == PortCount::Unknown || spec.output_count == PortCount::Unknown {
            unknown += 1;
        }
        println!(
            "{:<28} | {:<44} | {:<40} | in {:?} / out {:?}{}{}{}",
            spec.name,
            ports(spec, Direction::Input),
            ports(spec, Direction::Output),
            spec.input_count,
            spec.output_count,
            if spec.may_block { " | may_block" } else { "" },
            if spec.conditional_members {
                " | conditional_members"
            } else {
                ""
            },
            synonyms(spec)
        );
    }
    println!("\n{} structs, {} unknown", specs.len(), unknown);
}

fn palette_command(paths: &[PathBuf], table: bool) -> Result<(), Failure> {
    let specs = palette_specs(paths)?;
    if table {
        palette_table(&specs);
        return Ok(());
    }
    let document = serde_json::json!({ "version": SCHEMA_VERSION, "blocks": specs });
    println!("{}", serde_json::to_string(&document)?);
    Ok(())
}

fn status(model: &FileModel) -> &'static str {
    if model.sites.is_empty() {
        return "no-sites";
    }
    let editable = model.sites.iter().filter(|s| s.editable).count();
    match editable {
        0 => "read-only",
        n if n == model.sites.len() => "editable",
        _ => "partial",
    }
}

fn read_only_counts(sites: &[Site]) -> (usize, usize, usize) {
    let blocks = sites
        .iter()
        .flat_map(|s| s.blocks.iter())
        .filter(|b| !b.editable)
        .count();
    let edges = sites
        .iter()
        .flat_map(|s| s.edges.iter())
        .filter(|e| !e.editable)
        .count();
    let unresolved = sites.iter().map(|s| s.unresolved.len()).sum();
    (blocks, edges, unresolved)
}

fn config_label(sites: &[Site]) -> &'static str {
    let configs: Vec<_> = sites.iter().filter_map(|s| s.config.as_ref()).collect();
    match configs.first() {
        None => "-",
        Some(config) if config.editable => "editable",
        Some(_) => "read-only",
    }
}

const HEADER: &str = "file                                         sites blocks edges blk_ro edg_ro  unres    config status";

fn corpus_command(dir: &Path) -> Result<(), Failure> {
    let files = source_files(dir, &["cpp"])?;
    println!("{HEADER}");
    let mut totals = [0usize; 4];
    let mut with_sites = 0;
    for file in &files {
        let shown = file.strip_prefix(dir).unwrap_or(file).display().to_string();
        let model = match DocumentSession::open(file) {
            Ok(session) => session.parse(),
            Err(error) => {
                println!("{shown:<44} {error}");
                continue;
            }
        };
        let blocks: usize = model.sites.iter().map(|s| s.blocks.len()).sum();
        let edges: usize = model.sites.iter().map(|s| s.edges.len()).sum();
        let (blocks_ro, edges_ro, unresolved) = read_only_counts(&model.sites);
        let state = status(&model);
        println!(
            "{:<44} {:>5} {:>6} {:>5} {:>6} {:>6} {:>6} {:>9} {}",
            shown,
            model.sites.len(),
            blocks,
            edges,
            blocks_ro,
            edges_ro,
            unresolved,
            config_label(&model.sites),
            state
        );
        if !model.sites.is_empty() {
            with_sites += 1;
        }
        match state {
            "editable" => totals[0] += 1,
            "partial" => totals[1] += 1,
            "read-only" => totals[2] += 1,
            _ => totals[3] += 1,
        }
    }
    println!(
        "\n{} files, {} with sites: {} fully editable, {} partial, {} read-only, {} without sites",
        files.len(),
        with_sites,
        totals[0],
        totals[1],
        totals[2],
        totals[3]
    );
    Ok(())
}

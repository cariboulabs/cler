use crate::Splice;

const DIFF_CONTEXT: usize = 3;

struct Hunk {
    first: usize,
    last: usize,
    shift: isize,
    delta: isize,
}

fn line_starts(text: &str) -> Vec<usize> {
    let mut starts = vec![0];
    starts.extend(text.match_indices('\n').map(|(at, _)| at + 1));
    starts
}

fn line_of(starts: &[usize], offset: usize) -> usize {
    starts
        .partition_point(|start| *start <= offset)
        .saturating_sub(1)
}

fn hunks(starts: &[usize], splices: &[Splice]) -> Vec<Hunk> {
    let mut found: Vec<Hunk> = Vec::new();
    let mut shift = 0isize;
    for splice in splices {
        let first = line_of(starts, splice.start);
        let last = line_of(starts, splice.end);
        let delta = splice.text.len() as isize - (splice.end - splice.start) as isize;
        match found.last_mut() {
            Some(previous) if first <= previous.last + DIFF_CONTEXT * 2 => {
                previous.last = previous.last.max(last);
                previous.delta += delta;
            }
            _ => found.push(Hunk {
                first,
                last,
                shift,
                delta,
            }),
        }
        shift += delta;
    }
    found
}

pub fn unified(before: &str, after: &str, splices: &[Splice]) -> String {
    if splices.is_empty() || before == after {
        return String::new();
    }
    let starts = line_starts(before);
    let old: Vec<&str> = before.split('\n').collect();
    let mut out = String::new();
    for hunk in hunks(&starts, splices) {
        let from = hunk.first.saturating_sub(DIFF_CONTEXT);
        let to = (hunk.last + DIFF_CONTEXT).min(old.len() - 1);
        let opened = (starts[from] as isize + hunk.shift) as usize;
        let closed = (starts[to] as isize + old[to].len() as isize + hunk.shift + hunk.delta) as usize;
        let Some(window) = after.get(opened..closed) else {
            continue;
        };
        let fresh: Vec<&str> = window.split('\n').collect();
        let lead = hunk.first - from;
        let trail = to - hunk.last;
        let dropped = &old[hunk.first..=hunk.last];
        let gained = &fresh[lead..fresh.len() - trail];
        let head = shared(dropped.iter().zip(gained.iter()));
        let tail = shared(dropped[head..].iter().rev().zip(gained[head..].iter().rev()));

        out.push_str(&format!(
            "@@ -{},{} +{},{} @@\n",
            from + 1,
            to - from + 1,
            after[..opened].matches('\n').count() + 1,
            fresh.len()
        ));
        for line in old[from..hunk.first].iter().chain(&dropped[..head]) {
            out.push_str(&format!(" {line}\n"));
        }
        for line in &dropped[head..dropped.len() - tail] {
            out.push_str(&format!("-{line}\n"));
        }
        for line in &gained[head..gained.len() - tail] {
            out.push_str(&format!("+{line}\n"));
        }
        for line in dropped[dropped.len() - tail..]
            .iter()
            .chain(&old[hunk.last + 1..=to])
        {
            out.push_str(&format!(" {line}\n"));
        }
    }
    out
}

fn shared<'a>(pairs: impl Iterator<Item = (&'a &'a str, &'a &'a str)>) -> usize {
    pairs.take_while(|(left, right)| left == right).count()
}


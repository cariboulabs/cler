use sha2::{Digest, Sha256};

use super::splice::{merge, Splice};
use super::ApplyError;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SourcePatch {
    before_sha256: [u8; 32],
    after_sha256: [u8; 32],
    forward: Vec<Splice>,
    reverse: Vec<Splice>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PatchDirection {
    Forward,
    Reverse,
}

impl SourcePatch {
    pub(super) fn new(before: &str, after: &str, forward: Vec<Splice>) -> Result<Self, ApplyError> {
        let forward = coalesce_splices(forward);
        let replayed = merge(
            before,
            forward.iter().cloned().enumerate().collect::<Vec<_>>(),
        )?
        .0;
        if replayed != after {
            return Err(ApplyError::HistoryMismatch);
        }
        let mut reverse = Vec::with_capacity(forward.len());
        let mut shift = 0isize;
        for splice in &forward {
            let replaced = before
                .get(splice.start..splice.end)
                .ok_or(ApplyError::StaleSpan {
                    span: splice.span(),
                })?;
            let start = splice
                .start
                .checked_add_signed(shift)
                .ok_or(ApplyError::HistoryMismatch)?;
            reverse.push(Splice {
                start,
                end: start + splice.text.len(),
                text: replaced.to_string(),
            });
            shift += splice.text.len() as isize - (splice.end - splice.start) as isize;
        }
        let restored = merge(
            after,
            reverse.iter().cloned().enumerate().collect::<Vec<_>>(),
        )?
        .0;
        if restored != before {
            return Err(ApplyError::HistoryMismatch);
        }
        Ok(Self {
            before_sha256: source_digest(before),
            after_sha256: source_digest(after),
            forward,
            reverse,
        })
    }

    pub fn apply(&self, source: &str, direction: PatchDirection) -> Result<String, ApplyError> {
        let (expected, splices) = match direction {
            PatchDirection::Forward => (&self.before_sha256, &self.forward),
            PatchDirection::Reverse => (&self.after_sha256, &self.reverse),
        };
        if &source_digest(source) != expected {
            return Err(ApplyError::HistoryMismatch);
        }
        merge(
            source,
            splices.iter().cloned().enumerate().collect::<Vec<_>>(),
        )
        .map(|(next, _)| next)
    }
}

fn coalesce_splices(splices: Vec<Splice>) -> Vec<Splice> {
    let mut coalesced: Vec<Splice> = Vec::with_capacity(splices.len());
    for splice in splices {
        if let Some(previous) = coalesced.last_mut() {
            if previous.end == splice.start {
                previous.end = splice.end;
                previous.text.push_str(&splice.text);
                continue;
            }
        }
        coalesced.push(splice);
    }
    coalesced
}

fn source_digest(source: &str) -> [u8; 32] {
    Sha256::digest(source.as_bytes()).into()
}

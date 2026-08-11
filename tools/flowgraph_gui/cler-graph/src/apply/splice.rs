use serde::Serialize;

use crate::model::Span;

use super::ApplyError;

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct Splice {
    pub start: usize,
    pub end: usize,
    pub text: String,
}

impl Splice {
    pub(super) fn replace(span: Span, text: impl Into<String>) -> Self {
        Splice {
            start: span.start,
            end: span.end,
            text: text.into(),
        }
    }

    pub(super) fn insert(at: usize, text: impl Into<String>) -> Self {
        Splice {
            start: at,
            end: at,
            text: text.into(),
        }
    }

    pub(super) fn remove(start: usize, end: usize) -> Self {
        Splice {
            start,
            end,
            text: String::new(),
        }
    }

    pub(super) fn span(&self) -> Span {
        Span {
            start: self.start,
            end: self.end,
        }
    }
}

pub(super) fn merge(
    source: &str,
    mut ordered: Vec<(usize, Splice)>,
) -> Result<(String, Vec<Splice>), ApplyError> {
    ordered.sort_by_key(|(index, splice)| (splice.start, splice.end, *index));
    let splices: Vec<Splice> = ordered.into_iter().map(|(_, splice)| splice).collect();
    for pair in splices.windows(2) {
        let (first, second) = (&pair[0], &pair[1]);
        let both_insert = first.start == first.end && second.start == second.end;
        if second.start < first.end || (second.start == first.start && !both_insert) {
            return Err(ApplyError::OverlappingSplices {
                first: first.span(),
                second: second.span(),
            });
        }
    }
    let mut out = String::with_capacity(source.len());
    let mut cursor = 0;
    for splice in &splices {
        let head = source
            .get(cursor..splice.start)
            .ok_or(ApplyError::StaleSpan {
                span: splice.span(),
            })?;
        out.push_str(head);
        out.push_str(&splice.text);
        cursor = splice.end;
    }
    let tail = source.get(cursor..).ok_or(ApplyError::StaleSpan {
        span: Span {
            start: cursor,
            end: source.len(),
        },
    })?;
    out.push_str(tail);
    Ok((out, splices))
}

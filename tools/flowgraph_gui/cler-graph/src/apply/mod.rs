mod includes;
mod patch;
mod planner;
mod splice;

use std::fmt;

use serde::{Deserialize, Serialize};
use tree_sitter::Tree;

use crate::model::{Reason, Span, SCHEMA_VERSION};
use crate::{parse_source, DocumentSession};

use includes::include_splices;
use planner::Planner;
use splice::merge;

pub use patch::{PatchDirection, SourcePatch};
pub use splice::Splice;

#[derive(Debug, Clone, Deserialize, Serialize)]
#[serde(tag = "command", rename_all = "snake_case", deny_unknown_fields)]
pub enum Command {
    SetParam {
        site: usize,
        block: String,
        ctor_arg_index: usize,
        new_text: String,
    },
    SetTemplateArg {
        site: usize,
        block: String,
        template_arg_index: usize,
        new_text: String,
    },
    SetDisplayName {
        site: usize,
        block: String,
        new_text: String,
    },
    SetConfig {
        site: usize,
        path: String,
        new_value: String,
    },
    Connect {
        site: usize,
        from: String,
        to: String,
        port: String,
        #[serde(default)]
        port_index: Option<usize>,
    },
    Disconnect {
        site: usize,
        edge: usize,
    },
    AddBlock {
        site: usize,
        #[serde(rename = "type")]
        type_name: String,
        #[serde(default)]
        template_args: Vec<String>,
        #[serde(default)]
        ctor_args: Vec<String>,
        var_name: String,
    },
    RemoveFromGraph {
        site: usize,
        block: String,
    },
    DeleteBlock {
        site: usize,
        block: String,
    },
    DefineBlock {
        site: usize,
        name: String,
        value_type: String,
        #[serde(default)]
        inputs: Vec<InputPort>,
        #[serde(default)]
        outputs: usize,
        #[serde(default)]
        params: Vec<BlockParam>,
        #[serde(default)]
        may_block: bool,
    },
}

#[derive(Debug, Clone, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct InputPort {
    pub name: String,
}

#[derive(Debug, Clone, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct BlockParam {
    pub name: String,
    pub cpp_type: String,
    #[serde(default)]
    pub default: Option<String>,
}

#[derive(Debug, Clone, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct Transaction {
    pub version: String,
    pub base_revision: u64,
    pub commands: Vec<Command>,
}

#[derive(Debug, Clone, Serialize)]
pub struct ApplyOutcome {
    pub revision: u64,
    pub splices: Vec<Splice>,
}

pub struct PendingApply {
    source: String,
    tree: Tree,
    splices: Vec<Splice>,
    changed: bool,
    patch: Option<SourcePatch>,
}

impl PendingApply {
    pub fn source(&self) -> &str {
        &self.source
    }

    pub fn changes(&self) -> bool {
        self.changed
    }

    pub fn splices(&self) -> &[Splice] {
        &self.splices
    }

    pub fn patch(&self) -> Option<&SourcePatch> {
        self.patch.as_ref()
    }
}

#[derive(Debug, Clone, Serialize)]
#[serde(tag = "error", rename_all = "snake_case")]
pub enum ApplyError {
    SchemaMismatch {
        expected: &'static str,
        found: String,
    },
    RevisionMismatch {
        base_revision: u64,
        current_revision: u64,
    },
    UnknownSite {
        site: usize,
    },
    UnknownBlock {
        site: usize,
        block: String,
    },
    NotEditable {
        element: String,
        reason: Option<Reason>,
    },
    IndexOutOfRange {
        element: String,
        index: usize,
        len: usize,
    },
    NoDisplayNameArgument {
        block: String,
    },
    AliasedTemplateArguments {
        block: String,
        alias: String,
    },
    NoConfig {
        site: usize,
    },
    NotInGraph {
        block: String,
    },
    DuplicateVariable {
        var_name: String,
    },
    InvalidIdentifier {
        text: String,
    },
    InvalidExpression {
        element: String,
        text: String,
    },
    InvalidType {
        element: String,
        text: String,
    },
    InvalidBlockName {
        name: String,
    },
    DuplicateType {
        name: String,
    },
    ReservedIdentifier {
        text: String,
    },
    FileHasErrors {
        errors: Vec<Span>,
    },
    EmptyConstructorArguments {
        var_name: String,
    },
    ReferencesOutsideGraph {
        block: String,
        spans: Vec<Span>,
    },
    UnsupportedShape {
        detail: String,
    },
    StaleSpan {
        span: Span,
    },
    OverlappingSplices {
        first: Span,
        second: Span,
    },
    ProducesParseError,
    Reparse {
        detail: String,
    },
    NothingToUndo,
    NothingToRedo,
    HistoryMismatch,
}

impl fmt::Display for ApplyError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match serde_json::to_string(self) {
            Ok(text) => write!(f, "{text}"),
            Err(cause) => write!(f, "unserializable apply error: {cause}"),
        }
    }
}

impl std::error::Error for ApplyError {}

impl DocumentSession {
    pub fn apply(&mut self, transaction: Transaction) -> Result<ApplyOutcome, ApplyError> {
        let pending = self.preview(transaction)?;
        Ok(self.commit(pending))
    }

    pub fn preview(&self, transaction: Transaction) -> Result<PendingApply, ApplyError> {
        self.preview_with_includes(transaction, &[])
    }

    pub fn preview_with_includes(
        &self,
        transaction: Transaction,
        includes: &[String],
    ) -> Result<PendingApply, ApplyError> {
        if transaction.version != SCHEMA_VERSION {
            return Err(ApplyError::SchemaMismatch {
                expected: SCHEMA_VERSION,
                found: transaction.version,
            });
        }
        if transaction.base_revision != self.revision {
            return Err(ApplyError::RevisionMismatch {
                base_revision: transaction.base_revision,
                current_revision: self.revision,
            });
        }

        let model = self.parse();
        let planner = Planner {
            model: &model,
            tree: &self.tree,
            src: &self.source,
        };
        let mut splices = Vec::new();
        for (index, command) in transaction.commands.iter().enumerate() {
            splices.extend(planner.plan(command)?.into_iter().map(|s| (index, s)));
        }
        let include_index = transaction.commands.len();
        splices.extend(include_splices(&self.source, includes).into_iter().map(|splice| {
            (include_index, splice)
        }));
        let (next, splices) = merge(&self.source, splices)?;
        if next == self.source {
            return Ok(PendingApply {
                source: next,
                tree: self.tree.clone(),
                splices,
                changed: false,
                patch: None,
            });
        }

        let tree = parse_source(&next).map_err(|cause| ApplyError::Reparse {
            detail: cause.to_string(),
        })?;
        if tree.root_node().has_error() {
            return Err(ApplyError::ProducesParseError);
        }

        let patch = SourcePatch::new(&self.source, &next, splices.clone())?;
        Ok(PendingApply {
            source: next,
            tree,
            splices,
            changed: true,
            patch: Some(patch),
        })
    }

    pub fn preview_source(&self, source: String) -> Result<PendingApply, ApplyError> {
        if source == self.source {
            return Ok(PendingApply {
                source,
                tree: self.tree.clone(),
                splices: Vec::new(),
                changed: false,
                patch: None,
            });
        }
        let tree = parse_source(&source).map_err(|cause| ApplyError::Reparse {
            detail: cause.to_string(),
        })?;
        if tree.root_node().has_error() {
            return Err(ApplyError::ProducesParseError);
        }
        Ok(PendingApply {
            source,
            tree,
            splices: Vec::new(),
            changed: true,
            patch: None,
        })
    }

    pub fn commit(&mut self, pending: PendingApply) -> ApplyOutcome {
        if !pending.changed {
            return ApplyOutcome {
                revision: self.revision,
                splices: pending.splices,
            };
        }
        self.source = pending.source;
        self.tree = pending.tree;
        self.revision += 1;
        ApplyOutcome {
            revision: self.revision,
            splices: pending.splices,
        }
    }

    pub fn reload(&mut self, text: impl Into<String>) -> Result<(u64, bool), ApplyError> {
        self.swap_source(text.into())?;
        Ok((self.revision, self.has_errors()))
    }

    fn swap_source(&mut self, text: String) -> Result<String, ApplyError> {
        let tree = parse_source(&text).map_err(|cause| ApplyError::Reparse {
            detail: cause.to_string(),
        })?;
        self.tree = tree;
        self.revision += 1;
        Ok(std::mem::replace(&mut self.source, text))
    }
}

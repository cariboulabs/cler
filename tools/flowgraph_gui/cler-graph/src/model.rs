use serde::Serialize;

pub const SCHEMA_VERSION: &str = "0.3.0";

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
pub struct Span {
    pub start: usize,
    pub end: usize,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum Reason {
    NamedRunnerVariable,
    UnsupportedBlockExpression,
    OptionalEmplaceDeclaration,
    ConditionalConstruction,
    UnsupportedDeclaration,
    BraceInitDeclaration,
    AmbiguousDeclaration,
    ConcatenatedDisplayName,
    UnsupportedDisplayName,
    UndeclaredBlock,
    MethodCallPort,
    UnresolvedPortIndex,
    UnresolvedRunnerArgument,
    UnsupportedRunnerExpression,
    InlineConfigTemporary,
    FactoryConfig,
    UnsupportedConfigDeclaration,
    UnsupportedConfigTarget,
    AmbiguousConfigAssignment,
    UnboundFlowgraph,
    MultiSiteFunction,
    ParseError,
    SiteHasReadOnlyElements,
}

#[derive(Debug, Clone, Serialize)]
pub struct TemplateArg {
    pub text: String,
    pub resolved: Option<String>,
    pub span: Span,
}

#[derive(Debug, Clone, Serialize)]
pub struct CtorArg {
    pub text: String,
    pub span: Span,
}

#[derive(Debug, Clone, Serialize)]
pub struct Block {
    pub var: String,
    pub type_text: String,
    pub type_name: String,
    pub alias: Option<String>,
    pub template_args: Vec<TemplateArg>,
    pub ctor_args: Vec<CtorArg>,
    pub display_name: Option<String>,
    pub in_graph: bool,
    pub span: Span,
    pub editable: bool,
    pub read_only_reason: Option<Reason>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum RunnerForm {
    Inline,
    NamedVariable,
}

#[derive(Debug, Clone, Serialize)]
pub struct Runner {
    pub index: usize,
    pub block: Option<String>,
    pub block_expr: String,
    pub may_block: bool,
    pub form: RunnerForm,
    pub span: Span,
    pub editable: bool,
    pub read_only_reason: Option<Reason>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum PortKind {
    Field,
    IndexedField,
    MethodCall,
}

#[derive(Debug, Clone, Serialize)]
pub struct Port {
    pub name: String,
    pub index: Option<usize>,
    pub kind: PortKind,
}

#[derive(Debug, Clone, Serialize)]
pub struct Edge {
    pub from: String,
    pub to: String,
    pub port: Port,
    pub runner_index: usize,
    pub arg_index: usize,
    pub text: String,
    pub span: Span,
    pub editable: bool,
    pub read_only_reason: Option<Reason>,
    pub sample_type: Option<String>,
    pub source_type: Option<String>,
    pub type_conflict: bool,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum ConfigSource {
    Variable,
    InlineTemporary,
}

#[derive(Debug, Clone, Serialize)]
pub struct ConfigAssignment {
    pub path: String,
    pub value: String,
    pub span: Span,
    pub value_span: Span,
    pub editable: bool,
    pub read_only_reason: Option<Reason>,
}

#[derive(Debug, Clone, Serialize)]
pub struct Config {
    pub var: Option<String>,
    pub source: ConfigSource,
    pub assignments: Vec<ConfigAssignment>,
    pub run_call_span: Span,
    pub editable: bool,
    pub read_only_reason: Option<Reason>,
}

#[derive(Debug, Clone, Serialize)]
pub struct Unresolved {
    pub text: String,
    pub span: Span,
    pub runner_index: usize,
    pub arg_index: usize,
    pub reason: Reason,
}

#[derive(Debug, Clone, Serialize)]
pub struct Site {
    pub function: String,
    pub call_offset: usize,
    pub span: Span,
    pub flowgraph_var: Option<String>,
    pub blocks: Vec<Block>,
    pub runners: Vec<Runner>,
    pub edges: Vec<Edge>,
    pub config: Option<Config>,
    pub unresolved: Vec<Unresolved>,
    pub editable: bool,
    pub read_only_reason: Option<Reason>,
}

#[derive(Debug, Clone, Serialize)]
pub struct FileModel {
    pub version: &'static str,
    pub file: Option<String>,
    pub has_errors: bool,
    pub errors: Vec<Span>,
    pub sites: Vec<Site>,
}

impl Site {
    pub fn block(&self, var: &str) -> Option<&Block> {
        self.blocks.iter().find(|b| b.var == var)
    }

    pub fn edges_between<'a>(&'a self, from: &str, to: &str) -> Vec<&'a Edge> {
        self.edges
            .iter()
            .filter(|e| e.from == from && e.to == to)
            .collect()
    }

    pub fn read_only_elements(&self) -> usize {
        self.blocks.iter().filter(|b| !b.editable).count()
            + self.runners.iter().filter(|r| !r.editable).count()
            + self.edges.iter().filter(|e| !e.editable).count()
            + self.config.iter().filter(|c| !c.editable).count()
            + self.unresolved.len()
    }
}

impl FileModel {
    pub fn site_in(&self, function: &str) -> Option<&Site> {
        self.sites.iter().find(|s| s.function == function)
    }
}

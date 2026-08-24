use serde::Serialize;

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum TemplateParamKind {
    Type,
    NonType { param_type: String },
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct TemplateParam {
    pub name: String,
    pub kind: TemplateParamKind,
    pub default: Option<String>,
    pub pack: bool,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct CtorParam {
    pub name: String,
    pub param_type: String,
    pub default: Option<String>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum Direction {
    Input,
    Output,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct Port {
    pub name: String,
    pub direction: Direction,
    pub element_type: String,
    pub variable: bool,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum PortCount {
    Fixed(usize),
    TemplateArg(usize),
    CtorArg(usize),
    CtorArgLen(usize),
    Unknown,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct Synonym {
    pub name: String,
    pub template_args: Vec<String>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct BlockSpec {
    pub name: String,
    pub origin: String,
    pub synonyms: Vec<Synonym>,
    pub template_params: Vec<TemplateParam>,
    pub ctor_params: Vec<CtorParam>,
    pub may_block: bool,
    pub is_gui: bool,
    pub conditional_members: bool,
    pub ports: Vec<Port>,
    pub input_count: PortCount,
    pub output_count: PortCount,
}

impl BlockSpec {
    pub fn inputs(&self) -> impl Iterator<Item = &Port> {
        self.ports
            .iter()
            .filter(|p| p.direction == Direction::Input)
    }

    pub fn outputs(&self) -> impl Iterator<Item = &Port> {
        self.ports
            .iter()
            .filter(|p| p.direction == Direction::Output)
    }

    pub fn port(&self, name: &str) -> Option<&Port> {
        self.ports.iter().find(|p| p.name == name)
    }
}

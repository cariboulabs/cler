mod apply;
pub mod model;
mod palette;
pub mod palette_types;
mod parse;

use std::fmt;
use std::path::{Path, PathBuf};

use tree_sitter::{Parser, Tree};

pub use apply::{ApplyError, ApplyOutcome, Command, Splice, Transaction};
pub use model::{FileModel, Site, SCHEMA_VERSION};
pub use palette::extract_specs;
pub use palette_types::BlockSpec;

#[derive(Debug)]
pub enum Error {
    Language(tree_sitter::LanguageError),
    Unparsable,
    Read {
        path: PathBuf,
        cause: std::io::Error,
    },
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Error::Language(cause) => write!(f, "tree-sitter C++ grammar rejected: {cause}"),
            Error::Unparsable => write!(f, "tree-sitter produced no tree for the source"),
            Error::Read { path, cause } => write!(f, "cannot read {}: {cause}", path.display()),
        }
    }
}

impl std::error::Error for Error {}

pub struct DocumentSession {
    source: String,
    tree: Tree,
    revision: u64,
    file: Option<String>,
    undo: Vec<String>,
    redo: Vec<String>,
}

pub(crate) fn parse_source(source: &str) -> Result<Tree, Error> {
    let mut parser = Parser::new();
    parser
        .set_language(&tree_sitter_cpp::LANGUAGE.into())
        .map_err(Error::Language)?;
    parser.parse(source, None).ok_or(Error::Unparsable)
}

impl DocumentSession {
    pub fn load(text: impl Into<String>) -> Result<Self, Error> {
        let source = text.into();
        let tree = parse_source(&source)?;
        Ok(DocumentSession {
            source,
            tree,
            revision: 0,
            file: None,
            undo: Vec::new(),
            redo: Vec::new(),
        })
    }

    pub fn open(path: impl AsRef<Path>) -> Result<Self, Error> {
        let path = path.as_ref();
        let text = std::fs::read_to_string(path).map_err(|cause| Error::Read {
            path: path.to_path_buf(),
            cause,
        })?;
        let mut session = Self::load(text)?;
        session.file = Some(path.display().to_string());
        Ok(session)
    }

    pub fn parse(&self) -> FileModel {
        let (sites, errors) = parse::extract(self.tree.root_node(), &self.source);
        FileModel {
            version: SCHEMA_VERSION,
            file: self.file.clone(),
            has_errors: !errors.is_empty(),
            errors,
            sites,
        }
    }

    pub fn source(&self) -> &str {
        &self.source
    }

    pub fn tree(&self) -> &Tree {
        &self.tree
    }

    pub fn revision(&self) -> u64 {
        self.revision
    }

    pub fn has_errors(&self) -> bool {
        self.tree.root_node().has_error()
    }
}

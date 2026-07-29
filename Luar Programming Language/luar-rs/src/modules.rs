use crate::lexer::{Lexer, Token, TokenKind};
use std::collections::HashSet;
use std::fs;
use std::path::Path;

#[derive(Debug, Clone)]
pub struct ModuleDefinition {
    pub name: String,
    pub members: HashSet<String>,
    pub globals: HashSet<String>,
}

#[derive(Debug, Clone)]
pub struct ModuleError {
    pub message: String,
    pub line: usize,
}

pub fn load_definition(
    module_name: &str,
    source_path: &Path,
) -> Result<ModuleDefinition, ModuleError> {
    let directory = source_path.parent().unwrap_or_else(|| Path::new(""));
    let definition_path = directory.join(format!("{module_name}.luard"));
    let bytes = fs::read(&definition_path).map_err(|error| ModuleError {
        message: format!(
            "cannot read module definition '{}': {error}",
            definition_path.display()
        ),
        line: 0,
    })?;
    let source = String::from_utf8(bytes).map_err(|error| ModuleError {
        message: format!(
            "module definition '{}' is not valid UTF-8: {error}",
            definition_path.display()
        ),
        line: 0,
    })?;
    parse_definition(module_name, &definition_path, &source)
}

pub fn parse_definition(
    module_name: &str,
    definition_path: &Path,
    source: &str,
) -> Result<ModuleDefinition, ModuleError> {
    let mut lexer = Lexer::new(source);
    let tokens = lexer.tokenize().map_err(|message| ModuleError {
        message: format!("{}: {message}", definition_path.display()),
        line: 0,
    })?;
    let mut parser = DefinitionParser {
        tokens,
        pos: 0,
        path: definition_path,
    };
    let mut members = HashSet::new();
    let mut globals = HashSet::new();
    let mut all_names = HashSet::new();

    while !parser.at(TokenKind::Eof) {
        parser.expect(TokenKind::Declare, "'declare'")?;
        let is_global = parser.take(TokenKind::Global);
        let (name, line) = parser.expect_ident()?;
        parser.expect(TokenKind::Colon, "':'")?;
        parser.parse_type()?;
        if !all_names.insert(name.clone()) {
            return Err(parser.error(line, format!("name '{name}' is declared more than once")));
        }
        if is_global {
            globals.insert(name);
        } else {
            members.insert(name);
        }
    }

    Ok(ModuleDefinition {
        name: module_name.to_string(),
        members,
        globals,
    })
}

struct DefinitionParser<'a> {
    tokens: Vec<Token>,
    pos: usize,
    path: &'a Path,
}

impl DefinitionParser<'_> {
    fn token(&self) -> &Token {
        &self.tokens[self.pos]
    }

    fn at(&self, kind: TokenKind) -> bool {
        self.token().kind == kind
    }

    fn take(&mut self, kind: TokenKind) -> bool {
        if self.at(kind) {
            self.pos += 1;
            true
        } else {
            false
        }
    }

    fn expect(&mut self, kind: TokenKind, expected: &str) -> Result<(), ModuleError> {
        if self.take(kind) {
            Ok(())
        } else {
            Err(self.error(
                self.token().line,
                format!("expected {expected}, got '{}'", self.token().value),
            ))
        }
    }

    fn expect_ident(&mut self) -> Result<(String, usize), ModuleError> {
        if self.at(TokenKind::Ident) {
            let token = self.token().clone();
            self.pos += 1;
            Ok((token.value, token.line))
        } else {
            Err(self.error(
                self.token().line,
                format!("expected identifier, got '{}'", self.token().value),
            ))
        }
    }

    fn parse_type(&mut self) -> Result<(), ModuleError> {
        if self.take(TokenKind::LParen) {
            if !self.at(TokenKind::RParen) {
                self.parse_type()?;
                while self.take(TokenKind::Comma) {
                    self.parse_type()?;
                }
            }
            self.expect(TokenKind::RParen, "')'")?;
        } else {
            self.expect_ident()?;
        }
        self.take(TokenKind::Question);
        Ok(())
    }

    fn error(&self, line: usize, message: String) -> ModuleError {
        ModuleError {
            message: format!("{}:{line}: {message}", self.path.display()),
            line,
        }
    }
}

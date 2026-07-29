use crate::ast::*;
use crate::lexer::{Lexer, Token, TokenKind};

#[derive(Debug)]
pub struct ParseError(pub String);

pub struct Parser {
    tokens: Vec<Token>,
    pos: usize,
}

impl Parser {
    pub fn new(src: &str) -> Result<Self, ParseError> {
        let mut lex = Lexer::new(src);
        let tokens = lex.tokenize().map_err(ParseError)?;
        Ok(Parser { tokens, pos: 0 })
    }

    fn peek(&self) -> &Token {
        &self.tokens[self.pos]
    }
    fn peek_kind(&self) -> &TokenKind {
        &self.tokens[self.pos].kind
    }
    fn peek_n_kind(&self, offset: usize) -> &TokenKind {
        &self.tokens[(self.pos + offset).min(self.tokens.len() - 1)].kind
    }
    fn is_at_end(&self) -> bool {
        matches!(self.peek_kind(), TokenKind::Eof)
    }
    fn advance(&mut self) -> &Token {
        let t = &self.tokens[self.pos];
        self.pos += 1;
        t
    }
    fn match_tok(&mut self, k: &TokenKind) -> bool {
        if self.peek_kind() == k {
            self.pos += 1;
            true
        } else {
            false
        }
    }
    fn eat(&mut self, k: &TokenKind) -> Result<&Token, ParseError> {
        if self.peek_kind() == k {
            Ok(self.advance())
        } else {
            let t = self.peek();
            Err(ParseError(format!(
                "[{}] expected {:?}, got {:?}",
                t.line, k, t.kind
            )))
        }
    }
    fn eat_ident(&mut self) -> Result<String, ParseError> {
        if matches!(self.peek_kind(), TokenKind::Ident) {
            Ok(self.advance().value.clone())
        } else {
            let t = self.peek();
            Err(ParseError(format!(
                "[{}] expected identifier, got {:?}",
                t.line, t.kind
            )))
        }
    }

    fn is_contextual(&self, value: &str) -> bool {
        matches!(self.peek_kind(), TokenKind::Ident) && self.peek().value == value
    }

    fn starts_const_declaration(&self) -> bool {
        self.is_contextual("const")
            && matches!(self.peek_n_kind(1), TokenKind::Function | TokenKind::Ident)
    }

    pub fn parse(&mut self) -> Result<Program, ParseError> {
        let stmts = self.parse_block(&[TokenKind::Eof])?;
        Ok(Program { stmts })
    }

    fn parse_block(&mut self, terminators: &[TokenKind]) -> Result<Vec<Stmt>, ParseError> {
        let mut stmts = Vec::new();
        while !self.is_at_end() && !terminators.contains(self.peek_kind()) {
            if self.match_tok(&TokenKind::Semicolon) {
                continue;
            }
            stmts.push(self.parse_stmt()?);
        }
        Ok(stmts)
    }

    fn parse_stmt(&mut self) -> Result<Stmt, ParseError> {
        if self.starts_const_declaration() {
            return self.parse_const();
        }
        match self.peek_kind().clone() {
            TokenKind::Class => self.parse_class_decl(),
            TokenKind::Local => self.parse_local(),
            TokenKind::Do => self.parse_do(),
            TokenKind::While => self.parse_while(),
            TokenKind::Repeat => self.parse_repeat(),
            TokenKind::If => self.parse_if(),
            TokenKind::For => self.parse_for(),
            TokenKind::Return => self.parse_return(),
            TokenKind::Break => {
                self.advance();
                Ok(Stmt::Break)
            }
            TokenKind::Continue => {
                self.advance();
                Ok(Stmt::Continue)
            }
            TokenKind::Import => self.parse_import(),
            TokenKind::Declare => self.parse_declare(),
            _ => self.parse_expr_or_assign(),
        }
    }

    // ─── import / declare ─────────────────────────────────────────────────────

    fn parse_import(&mut self) -> Result<Stmt, ParseError> {
        self.advance(); // import
        let module_name = self.eat_ident()?;
        Ok(Stmt::ImportDecl { module_name })
    }

    fn parse_declare(&mut self) -> Result<Stmt, ParseError> {
        self.advance(); // declare
        let is_global = self.match_tok(&TokenKind::Global);
        let name = self.eat_ident()?;
        self.eat(&TokenKind::Colon)?;
        let ty = self.parse_type_expr()?;
        Ok(Stmt::DeclareStmt {
            is_global,
            name,
            ty,
            module_name: None,
        })
    }

    fn parse_const(&mut self) -> Result<Stmt, ParseError> {
        let line = self.advance().line;
        if self.match_tok(&TokenKind::Function) {
            let name = self.eat_ident()?;
            self.eat(&TokenKind::LParen)?;
            let params = self.parse_params()?;
            self.eat(&TokenKind::RParen)?;
            let return_type = if self.match_tok(&TokenKind::Colon) {
                Some(self.parse_type_expr()?)
            } else {
                None
            };
            let body = self.parse_block(&[TokenKind::End])?;
            self.eat(&TokenKind::End)?;
            return Ok(Stmt::FunctionDecl {
                name,
                params,
                return_type,
                body,
                is_const: true,
            });
        }

        let mut names = vec![self.eat_ident()?];
        let mut types = vec![self.try_parse_type_annotation()?];
        while self.match_tok(&TokenKind::Comma) {
            names.push(self.eat_ident()?);
            types.push(self.try_parse_type_annotation()?);
        }
        if !self.match_tok(&TokenKind::Eq) {
            return Err(ParseError(format!(
                "[{line}] const declaration must have an initializer"
            )));
        }
        let values = self.parse_expr_list()?;
        let final_value_can_expand = matches!(
            values.last(),
            Some(Expr::Call { .. } | Expr::MethodCall { .. } | Expr::Vararg)
        );
        if values.len() < names.len() && !final_value_can_expand {
            return Err(ParseError(format!(
                "[{line}] every const binding must have an initializer"
            )));
        }
        Ok(Stmt::Const {
            names,
            types,
            values,
        })
    }

    // ─── Local / Assign ───────────────────────────────────────────────────────

    fn parse_local(&mut self) -> Result<Stmt, ParseError> {
        self.eat(&TokenKind::Local)?;
        let mut names = vec![self.eat_ident()?];
        let mut types = vec![self.try_parse_type_annotation()?];
        while self.match_tok(&TokenKind::Comma) {
            names.push(self.eat_ident()?);
            types.push(self.try_parse_type_annotation()?);
        }
        let values = if self.match_tok(&TokenKind::Eq) {
            self.parse_expr_list()?
        } else {
            vec![]
        };
        Ok(Stmt::Local {
            names,
            types,
            values,
        })
    }

    fn parse_expr_or_assign(&mut self) -> Result<Stmt, ParseError> {
        let first = self.parse_expr()?;
        let mut targets = vec![first];
        while self.match_tok(&TokenKind::Comma) {
            targets.push(self.parse_expr()?);
        }
        if self.match_tok(&TokenKind::Eq) {
            let values = self.parse_expr_list()?;
            Ok(Stmt::Assign { targets, values })
        } else if targets.len() == 1 {
            Ok(Stmt::ExprStmt(targets.remove(0)))
        } else {
            Err(ParseError(format!(
                "[{}] expected '=' after expression list",
                self.peek().line
            )))
        }
    }

    fn parse_expr_list(&mut self) -> Result<Vec<Expr>, ParseError> {
        let mut list = vec![self.parse_expr()?];
        while self.match_tok(&TokenKind::Comma) {
            list.push(self.parse_expr()?);
        }
        Ok(list)
    }

    // ─── Control flow ─────────────────────────────────────────────────────────

    fn parse_do(&mut self) -> Result<Stmt, ParseError> {
        self.eat(&TokenKind::Do)?;
        let body = self.parse_block(&[TokenKind::End])?;
        self.eat(&TokenKind::End)?;
        Ok(Stmt::Do { body })
    }

    fn parse_while(&mut self) -> Result<Stmt, ParseError> {
        self.eat(&TokenKind::While)?;
        let cond = self.parse_expr()?;
        self.eat(&TokenKind::Do)?;
        let body = self.parse_block(&[TokenKind::End])?;
        self.eat(&TokenKind::End)?;
        Ok(Stmt::While { cond, body })
    }

    fn parse_repeat(&mut self) -> Result<Stmt, ParseError> {
        self.eat(&TokenKind::Repeat)?;
        let body = self.parse_block(&[TokenKind::Until])?;
        self.eat(&TokenKind::Until)?;
        let cond = self.parse_expr()?;
        Ok(Stmt::Repeat { body, cond })
    }

    fn parse_if(&mut self) -> Result<Stmt, ParseError> {
        self.eat(&TokenKind::If)?;
        let mut clauses = Vec::new();
        let cond = self.parse_expr()?;
        self.eat(&TokenKind::Then)?;
        let body = self.parse_block(&[TokenKind::Else, TokenKind::ElseIf, TokenKind::End])?;
        clauses.push(IfClause { cond, body });
        while self.match_tok(&TokenKind::ElseIf) {
            let c = self.parse_expr()?;
            self.eat(&TokenKind::Then)?;
            let b = self.parse_block(&[TokenKind::Else, TokenKind::ElseIf, TokenKind::End])?;
            clauses.push(IfClause { cond: c, body: b });
        }
        let else_body = if self.match_tok(&TokenKind::Else) {
            let b = self.parse_block(&[TokenKind::End])?;
            Some(b)
        } else {
            None
        };
        self.eat(&TokenKind::End)?;
        Ok(Stmt::If { clauses, else_body })
    }

    fn parse_for(&mut self) -> Result<Stmt, ParseError> {
        self.eat(&TokenKind::For)?;
        let first = self.eat_ident()?;
        if self.match_tok(&TokenKind::Eq) {
            // numeric for
            let start = self.parse_expr()?;
            self.eat(&TokenKind::Comma)?;
            let limit = self.parse_expr()?;
            let step = if self.match_tok(&TokenKind::Comma) {
                Some(self.parse_expr()?)
            } else {
                None
            };
            self.eat(&TokenKind::Do)?;
            let body = self.parse_block(&[TokenKind::End])?;
            self.eat(&TokenKind::End)?;
            Ok(Stmt::NumericFor {
                name: first,
                start,
                limit,
                step,
                body,
            })
        } else {
            // generic for
            let mut names = vec![first];
            while self.match_tok(&TokenKind::Comma) {
                names.push(self.eat_ident()?);
            }
            self.eat(&TokenKind::In)?;
            let iters = self.parse_expr_list()?;
            self.eat(&TokenKind::Do)?;
            let body = self.parse_block(&[TokenKind::End])?;
            self.eat(&TokenKind::End)?;
            Ok(Stmt::GenericFor { names, iters, body })
        }
    }

    fn parse_return(&mut self) -> Result<Stmt, ParseError> {
        self.eat(&TokenKind::Return)?;
        if self.is_at_end()
            || matches!(
                self.peek_kind(),
                TokenKind::End
                    | TokenKind::Else
                    | TokenKind::ElseIf
                    | TokenKind::Until
                    | TokenKind::Semicolon
            )
        {
            return Ok(Stmt::Return(vec![]));
        }
        Ok(Stmt::Return(self.parse_expr_list()?))
    }

    // ─── Class declaration ────────────────────────────────────────────────────

    fn parse_class_decl(&mut self) -> Result<Stmt, ParseError> {
        let line = self.peek().line;
        self.eat(&TokenKind::Class)?;
        let is_abstract = self.match_tok(&TokenKind::Abstract);
        // allow "class Foo is abstract" after name too — but spec puts it before name
        // Actually spec: "class Language is abstract" — "abstract" comes after "is"
        // We'll handle it after parsing the name and parent
        let name = self.eat_ident()?;
        let mut parent = None;
        let mut is_abstract_flag = is_abstract;

        let header_line = self.eat(&TokenKind::Is)?.line;

        // Header modifiers and parent names must be on the class header line.
        // This keeps a first top-level field on the next line from being mistaken
        // for a parent class.
        if self.peek().line == header_line && self.match_tok(&TokenKind::Abstract) {
            is_abstract_flag = true;
        }
        if self.peek().line == header_line && matches!(self.peek_kind(), TokenKind::Ident) {
            parent = Some(self.eat_ident()?);
        }
        if self.peek().line == header_line && self.match_tok(&TokenKind::Abstract) {
            is_abstract_flag = true;
        }

        let mut top_level_members = Vec::new();
        let mut blocks = Vec::new();

        while !self.is_at_end() && !matches!(self.peek_kind(), TokenKind::End) {
            match self.peek_kind().clone() {
                TokenKind::Public | TokenKind::Private => {
                    blocks.push(self.parse_member_block()?);
                }
                _ => {
                    top_level_members.push(self.parse_member()?);
                }
            }
        }
        self.eat(&TokenKind::End)?;

        Ok(Stmt::ClassDecl(ClassDecl {
            name,
            is_abstract: is_abstract_flag,
            parent,
            top_level_members,
            blocks,
            line,
        }))
    }

    fn parse_member_block(&mut self) -> Result<MemberBlock, ParseError> {
        let access = if self.match_tok(&TokenKind::Public) {
            Access::Public
        } else {
            self.advance();
            Access::Private
        };
        self.eat(&TokenKind::Is)?;
        let mut members = Vec::new();
        while !self.is_at_end() && !matches!(self.peek_kind(), TokenKind::End) {
            members.push(self.parse_member()?);
        }
        self.eat(&TokenKind::End)?;
        Ok(MemberBlock { access, members })
    }

    fn parse_member(&mut self) -> Result<Member, ParseError> {
        let is_static = self.match_tok(&TokenKind::Static);

        if matches!(self.peek_kind(), TokenKind::Function) {
            if matches!(self.peek_n_kind(1), TokenKind::Operator) {
                return self.parse_operator_method(is_static);
            }
            return self.parse_method_member(is_static);
        }
        if is_static {
            let t = self.peek();
            return Err(ParseError(format!(
                "[{}] 'static' must be followed by a function declaration",
                t.line
            )));
        }

        // field: name [: type] [= expr]
        let name = self.eat_ident()?;
        let ty = self.try_parse_type_annotation()?;
        let value = if self.match_tok(&TokenKind::Eq) {
            Some(self.parse_expr()?)
        } else {
            None
        };
        Ok(Member::Field(FieldMember { name, ty, value }))
    }

    fn parse_trailing_modifiers(&mut self) -> (bool, bool, bool) {
        let mut is_abstract = false;
        let mut is_override = false;
        let mut is_final = false;
        loop {
            if self.match_tok(&TokenKind::Abstract) {
                is_abstract = true;
            } else if self.match_tok(&TokenKind::Override) {
                is_override = true;
            } else if self.match_tok(&TokenKind::Final) {
                is_final = true;
            } else {
                break;
            }
        }
        (is_abstract, is_override, is_final)
    }

    fn parse_method_member(&mut self, is_static: bool) -> Result<Member, ParseError> {
        self.eat(&TokenKind::Function)?;
        let name = self.eat_ident()?;
        self.eat(&TokenKind::LParen)?;
        let params = self.parse_params()?;
        self.eat(&TokenKind::RParen)?;
        let return_type = if self.match_tok(&TokenKind::Colon) {
            Some(self.parse_type_expr()?)
        } else {
            None
        };
        let (is_abstract, is_override, is_final) = self.parse_trailing_modifiers();

        let body = if is_abstract {
            None
        } else {
            let b = self.parse_block(&[TokenKind::End])?;
            self.eat(&TokenKind::End)?;
            Some(b)
        };

        Ok(Member::Method(MethodMember {
            name,
            is_operator: false,
            operator_op: String::new(),
            is_static,
            is_abstract,
            is_override,
            is_final,
            params,
            return_type,
            body,
        }))
    }

    fn parse_operator_method(&mut self, is_static: bool) -> Result<Member, ParseError> {
        self.eat(&TokenKind::Function)?;
        self.eat(&TokenKind::Operator)?;
        let op = match self.peek_kind() {
            TokenKind::EqEq => "==",
            TokenKind::Lt => "<",
            TokenKind::LtEq => "<=",
            TokenKind::Plus => "+",
            TokenKind::Minus => "-",
            TokenKind::Star => "*",
            TokenKind::Slash => "/",
            TokenKind::SlashSlash => "//",
            TokenKind::Percent => "%",
            TokenKind::Caret => "^",
            TokenKind::DotDot => "..",
            TokenKind::Hash => "#",
            _ => {
                let t = self.peek();
                return Err(ParseError(format!(
                    "[{}] unsupported overloaded operator '{}'",
                    t.line, t.value
                )));
            }
        }
        .to_string();
        self.advance();
        self.eat(&TokenKind::LParen)?;
        let params = self.parse_params()?;
        self.eat(&TokenKind::RParen)?;
        let return_type = if self.match_tok(&TokenKind::Colon) {
            Some(self.parse_type_expr()?)
        } else {
            None
        };
        let (is_abstract, is_override, is_final) = self.parse_trailing_modifiers();
        let body = if is_abstract {
            None
        } else {
            let b = self.parse_block(&[TokenKind::End])?;
            self.eat(&TokenKind::End)?;
            Some(b)
        };
        Ok(Member::Method(MethodMember {
            name: format!("operator{op}"),
            is_operator: true,
            operator_op: op,
            is_static,
            is_abstract,
            is_override,
            is_final,
            params,
            return_type,
            body,
        }))
    }

    fn parse_params(&mut self) -> Result<Vec<Param>, ParseError> {
        let mut params = Vec::new();
        if matches!(self.peek_kind(), TokenKind::RParen) {
            return Ok(params);
        }
        loop {
            if matches!(self.peek_kind(), TokenKind::DotDotDot) {
                self.advance();
                params.push(Param::Vararg);
            } else {
                let name = self.eat_ident()?;
                let ty = self.try_parse_type_annotation()?;
                params.push(Param::Named { name, ty });
            }
            if !self.match_tok(&TokenKind::Comma) {
                break;
            }
        }
        Ok(params)
    }

    // ─── Type expressions ─────────────────────────────────────────────────────

    fn try_parse_type_annotation(&mut self) -> Result<Option<TypeExpr>, ParseError> {
        if self.match_tok(&TokenKind::Colon) {
            Ok(Some(self.parse_type_expr()?))
        } else {
            Ok(None)
        }
    }

    fn parse_type_expr(&mut self) -> Result<TypeExpr, ParseError> {
        if self.match_tok(&TokenKind::LParen) {
            let mut types = Vec::new();
            if !matches!(self.peek_kind(), TokenKind::RParen) {
                types.push(self.parse_type_expr()?);
                while self.match_tok(&TokenKind::Comma) {
                    types.push(self.parse_type_expr()?);
                }
            }
            self.eat(&TokenKind::RParen)?;
            return Ok(TypeExpr::Tuple(types));
        }
        let name = self.eat_ident()?;
        let mut ty = TypeExpr::Name(name);
        if self.match_tok(&TokenKind::Question) {
            ty = TypeExpr::Optional(Box::new(ty));
        }
        Ok(ty)
    }

    // ─── Expressions ──────────────────────────────────────────────────────────

    fn parse_expr(&mut self) -> Result<Expr, ParseError> {
        self.parse_binop(0)
    }

    fn binop_prec(op: &str) -> Option<(u8, u8)> {
        match op {
            "or" => Some((1, 1)),
            "and" => Some((2, 2)),
            "<" | ">" | "<=" | ">=" | "==" | "~=" => Some((3, 3)),
            ".." => Some((5, 4)),
            "+" | "-" => Some((6, 6)),
            "*" | "/" | "//" | "%" => Some((7, 7)),
            "^" => Some((10, 9)),
            _ => None,
        }
    }

    fn parse_binop(&mut self, min_prec: u8) -> Result<Expr, ParseError> {
        let mut left = self.parse_unop()?;
        loop {
            let op = self.current_binop();
            if op.is_none() {
                break;
            }
            let op = op.unwrap();
            let (lp, rp) = Self::binop_prec(&op).unwrap();
            if lp < min_prec {
                break;
            }
            self.advance();
            let right = self.parse_binop(rp)?;
            left = Expr::Binop {
                op,
                left: Box::new(left),
                right: Box::new(right),
            };
        }
        Ok(left)
    }

    fn current_binop(&self) -> Option<String> {
        let op = match self.peek_kind() {
            TokenKind::And => "and",
            TokenKind::Or => "or",
            TokenKind::Lt => "<",
            TokenKind::Gt => ">",
            TokenKind::LtEq => "<=",
            TokenKind::GtEq => ">=",
            TokenKind::EqEq => "==",
            TokenKind::TildeEq => "~=",
            TokenKind::DotDot => "..",
            TokenKind::Plus => "+",
            TokenKind::Minus => "-",
            TokenKind::Star => "*",
            TokenKind::Slash => "/",
            TokenKind::SlashSlash => "//",
            TokenKind::Percent => "%",
            TokenKind::Caret => "^",
            _ => return None,
        };
        Some(op.to_string())
    }

    fn parse_unop(&mut self) -> Result<Expr, ParseError> {
        match self.peek_kind().clone() {
            TokenKind::Not => {
                self.advance();
                Ok(Expr::Unop {
                    op: "not".into(),
                    expr: Box::new(self.parse_unop()?),
                })
            }
            TokenKind::Minus => {
                self.advance();
                Ok(Expr::Unop {
                    op: "-".into(),
                    expr: Box::new(self.parse_unop()?),
                })
            }
            TokenKind::Hash => {
                self.advance();
                Ok(Expr::Unop {
                    op: "#".into(),
                    expr: Box::new(self.parse_unop()?),
                })
            }
            _ => self.parse_postfix(),
        }
    }

    fn parse_postfix(&mut self) -> Result<Expr, ParseError> {
        let mut expr = self.parse_primary()?;
        loop {
            match self.peek_kind().clone() {
                TokenKind::Dot => {
                    self.advance();
                    let name = self.eat_ident()?;
                    if matches!(self.peek_kind(), TokenKind::LParen) {
                        self.advance();
                        let args = self.parse_call_args()?;
                        self.eat(&TokenKind::RParen)?;
                        expr = Expr::Call {
                            callee: Box::new(Expr::Field {
                                obj: Box::new(expr),
                                name,
                            }),
                            args,
                        };
                    } else {
                        expr = Expr::Field {
                            obj: Box::new(expr),
                            name,
                        };
                    }
                }
                TokenKind::Colon => {
                    self.advance();
                    let method = self.eat_ident()?;
                    self.eat(&TokenKind::LParen)?;
                    let args = self.parse_call_args()?;
                    self.eat(&TokenKind::RParen)?;
                    expr = Expr::MethodCall {
                        obj: Box::new(expr),
                        method,
                        args,
                    };
                }
                TokenKind::LBracket => {
                    self.advance();
                    let key = self.parse_expr()?;
                    self.eat(&TokenKind::RBracket)?;
                    expr = Expr::Index {
                        obj: Box::new(expr),
                        key: Box::new(key),
                    };
                }
                TokenKind::LParen => {
                    self.advance();
                    let args = self.parse_call_args()?;
                    self.eat(&TokenKind::RParen)?;
                    expr = Expr::Call {
                        callee: Box::new(expr),
                        args,
                    };
                }
                TokenKind::LuaString => {
                    // f"string" call syntax
                    let s = self.advance().value.clone();
                    expr = Expr::Call {
                        callee: Box::new(expr),
                        args: vec![Expr::Str(s)],
                    };
                }
                TokenKind::LBrace => {
                    // f{} call syntax
                    let tbl = self.parse_table()?;
                    expr = Expr::Call {
                        callee: Box::new(expr),
                        args: vec![tbl],
                    };
                }
                _ => break,
            }
        }
        Ok(expr)
    }

    fn parse_call_args(&mut self) -> Result<Vec<Expr>, ParseError> {
        let mut args = Vec::new();
        if matches!(self.peek_kind(), TokenKind::RParen) {
            return Ok(args);
        }
        args.push(self.parse_expr()?);
        while self.match_tok(&TokenKind::Comma) {
            args.push(self.parse_expr()?);
        }
        Ok(args)
    }

    fn parse_primary(&mut self) -> Result<Expr, ParseError> {
        match self.peek_kind().clone() {
            TokenKind::Nil => {
                self.advance();
                Ok(Expr::Nil)
            }
            TokenKind::True => {
                self.advance();
                Ok(Expr::True)
            }
            TokenKind::False => {
                self.advance();
                Ok(Expr::False)
            }
            TokenKind::Number => {
                let v = self.advance().value.clone();
                Ok(Expr::Number(v))
            }
            TokenKind::LuaString => {
                let v = self.advance().value.clone();
                Ok(Expr::Str(v))
            }
            TokenKind::DotDotDot => {
                self.advance();
                Ok(Expr::Vararg)
            }
            TokenKind::Self_ => {
                self.advance();
                Ok(Expr::SelfExpr)
            }
            TokenKind::Super => {
                self.advance();
                Ok(Expr::SuperExpr)
            }
            TokenKind::Ident => {
                let n = self.advance().value.clone();
                Ok(Expr::Ident(n))
            }
            TokenKind::LParen => {
                self.advance();
                let e = self.parse_expr()?;
                self.eat(&TokenKind::RParen)?;
                Ok(e)
            }
            TokenKind::LBrace => self.parse_table(),
            TokenKind::Function => {
                self.advance();
                self.eat(&TokenKind::LParen)?;
                let params = self.parse_params()?;
                self.eat(&TokenKind::RParen)?;
                let return_type = if self.match_tok(&TokenKind::Arrow) {
                    Some(self.parse_type_expr()?)
                } else {
                    None
                };
                let body = self.parse_block(&[TokenKind::End])?;
                self.eat(&TokenKind::End)?;
                Ok(Expr::Function {
                    params,
                    return_type,
                    body,
                })
            }
            _ => {
                let t = self.peek();
                Err(ParseError(format!(
                    "[{}] unexpected token {:?} in expression",
                    t.line, t.kind
                )))
            }
        }
    }

    fn parse_table(&mut self) -> Result<Expr, ParseError> {
        self.eat(&TokenKind::LBrace)?;
        let mut fields = Vec::new();
        while !matches!(self.peek_kind(), TokenKind::RBrace) && !self.is_at_end() {
            if self.match_tok(&TokenKind::Semicolon) || self.match_tok(&TokenKind::Comma) {
                continue;
            }
            fields.push(self.parse_table_field()?);
        }
        self.eat(&TokenKind::RBrace)?;
        Ok(Expr::Table(fields))
    }

    fn parse_table_field(&mut self) -> Result<TableField, ParseError> {
        if matches!(self.peek_kind(), TokenKind::LBracket) {
            self.advance();
            let key = self.parse_expr()?;
            self.eat(&TokenKind::RBracket)?;
            self.eat(&TokenKind::Eq)?;
            let value = self.parse_expr()?;
            return Ok(TableField::Index { key, value });
        }
        if matches!(self.peek_kind(), TokenKind::Ident)
            && self.tokens.get(self.pos + 1).map(|t| &t.kind) == Some(&TokenKind::Eq)
        {
            let name = self.eat_ident()?;
            self.eat(&TokenKind::Eq)?;
            let value = self.parse_expr()?;
            return Ok(TableField::Name { name, value });
        }
        Ok(TableField::Value(self.parse_expr()?))
    }
}

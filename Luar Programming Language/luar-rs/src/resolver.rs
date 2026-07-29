use crate::ast::*;
use crate::modules::ModuleDefinition;
use std::collections::{HashMap, HashSet};

#[derive(Debug, Clone)]
pub struct ResolveError {
    pub message: String,
    pub line: usize,
}

pub struct Resolver {
    errors: Vec<ResolveError>,
    module_members: HashMap<String, Vec<String>>,
    module_globals: HashSet<String>,
    imported_modules: HashSet<String>,
    scopes: Vec<HashMap<String, bool>>,
}

impl Resolver {
    pub fn new(definitions: Vec<ModuleDefinition>) -> Self {
        let mut module_members: HashMap<String, Vec<String>> = HashMap::new();
        let mut module_globals = HashSet::new();
        let mut imported_modules = HashSet::new();
        for definition in definitions {
            imported_modules.insert(definition.name.clone());
            for member in definition.members {
                module_members
                    .entry(member)
                    .or_default()
                    .push(definition.name.clone());
            }
            module_globals.extend(definition.globals);
        }
        for modules in module_members.values_mut() {
            modules.sort();
        }
        Self {
            errors: Vec::new(),
            module_members,
            module_globals,
            imported_modules,
            scopes: vec![HashMap::new()],
        }
    }

    pub fn resolve(mut self, program: &mut Program) -> Vec<ResolveError> {
        for module in &self.imported_modules {
            self.scopes[0].insert(module.clone(), false);
        }
        for stmt in &program.stmts {
            if let Stmt::ClassDecl(decl) = stmt {
                self.scopes[0].insert(decl.name.clone(), false);
            }
        }
        self.visit_stmts(&mut program.stmts, true);
        self.errors
    }

    fn visit_stmts(&mut self, stmts: &mut [Stmt], top_level: bool) {
        for stmt in stmts {
            self.visit_stmt(stmt, top_level);
        }
    }

    fn visit_stmt(&mut self, stmt: &mut Stmt, top_level: bool) {
        match stmt {
            Stmt::Local { names, values, .. } => {
                for value in values {
                    self.visit_expr(value);
                }
                for name in names {
                    self.declare(name, false);
                }
            }
            Stmt::Const { names, values, .. } => {
                for value in values {
                    self.visit_expr(value);
                }
                for name in names {
                    self.declare(name, true);
                }
            }
            Stmt::FunctionDecl {
                name, params, body, ..
            } => {
                self.declare(name, true);
                self.push_scope();
                self.declare_params(params);
                self.visit_stmts(body, false);
                self.pop_scope();
            }
            Stmt::Assign { targets, values } => {
                for target in targets {
                    if let Expr::Ident(name) = target {
                        if self.is_const(name) {
                            self.error(format!("cannot assign to const binding '{name}'"));
                        }
                    }
                    self.visit_expr(target);
                }
                for value in values {
                    self.visit_expr(value);
                }
            }
            Stmt::Do { body } => self.visit_scoped_block(body),
            Stmt::While { cond, body } => {
                self.visit_expr(cond);
                self.visit_scoped_block(body);
            }
            Stmt::Repeat { body, cond } => {
                self.push_scope();
                self.visit_stmts(body, false);
                self.visit_expr(cond);
                self.pop_scope();
            }
            Stmt::If { clauses, else_body } => {
                for clause in clauses {
                    self.visit_expr(&mut clause.cond);
                    self.visit_scoped_block(&mut clause.body);
                }
                if let Some(body) = else_body {
                    self.visit_scoped_block(body);
                }
            }
            Stmt::NumericFor {
                name,
                start,
                limit,
                step,
                body,
            } => {
                self.visit_expr(start);
                self.visit_expr(limit);
                if let Some(step) = step {
                    self.visit_expr(step);
                }
                self.push_scope();
                self.declare(name, false);
                self.visit_stmts(body, false);
                self.pop_scope();
            }
            Stmt::GenericFor { names, iters, body } => {
                for iter in iters {
                    self.visit_expr(iter);
                }
                self.push_scope();
                for name in names {
                    self.declare(name, false);
                }
                self.visit_stmts(body, false);
                self.pop_scope();
            }
            Stmt::Return(values) => {
                for value in values {
                    self.visit_expr(value);
                }
            }
            Stmt::ExprStmt(expr) => self.visit_expr(expr),
            Stmt::ClassDecl(decl) => self.visit_class(decl),
            Stmt::ImportDecl { .. } if !top_level => {
                self.error("import declarations are only allowed at the top level".to_string());
            }
            Stmt::ImportDecl { .. } => {}
            Stmt::DeclareStmt { .. } => self
                .error("'declare' is only allowed in .luard module definition files".to_string()),
            Stmt::Break | Stmt::Continue => {}
        }
    }

    fn visit_class(&mut self, decl: &mut ClassDecl) {
        self.push_scope();
        for member in decl
            .top_level_members
            .iter()
            .chain(decl.blocks.iter().flat_map(|block| &block.members))
        {
            if let Member::Method(method) = member {
                self.declare(&method.name, false);
            }
        }
        for member in &mut decl.top_level_members {
            self.visit_member(member);
        }
        for block in &mut decl.blocks {
            for member in &mut block.members {
                self.visit_member(member);
            }
        }
        self.pop_scope();
    }

    fn visit_member(&mut self, member: &mut Member) {
        match member {
            Member::Field(field) => {
                if let Some(value) = &mut field.value {
                    self.visit_expr(value);
                }
            }
            Member::Method(method) => {
                if let Some(body) = &mut method.body {
                    self.push_scope();
                    self.declare_params(&method.params);
                    self.visit_stmts(body, false);
                    self.pop_scope();
                }
            }
        }
    }

    fn visit_expr(&mut self, expr: &mut Expr) {
        match expr {
            Expr::Ident(name) => {
                if self.lookup(name).is_some() || self.module_globals.contains(name) {
                    return;
                }
                let Some(modules) = self.module_members.get(name).cloned() else {
                    return;
                };
                if modules.len() == 1 {
                    let field = name.clone();
                    *expr = Expr::Field {
                        obj: Box::new(Expr::Ident(modules[0].clone())),
                        name: field,
                    };
                } else {
                    self.error(format!(
                        "ambiguous unqualified name '{}': declared in modules [{}]. Use qualified access (e.g. {}.{})",
                        name,
                        modules.join(", "),
                        modules[0],
                        name
                    ));
                }
            }
            Expr::Field { obj, .. } => self.visit_expr(obj),
            Expr::Index { obj, key } => {
                self.visit_expr(obj);
                self.visit_expr(key);
            }
            Expr::Call { callee, args } => {
                self.visit_expr(callee);
                for arg in args {
                    self.visit_expr(arg);
                }
            }
            Expr::MethodCall { obj, args, .. } => {
                self.visit_expr(obj);
                for arg in args {
                    self.visit_expr(arg);
                }
            }
            Expr::Unop { expr, .. } => self.visit_expr(expr),
            Expr::Binop { left, right, .. } => {
                self.visit_expr(left);
                self.visit_expr(right);
            }
            Expr::Table(fields) => {
                for field in fields {
                    match field {
                        TableField::Index { key, value } => {
                            self.visit_expr(key);
                            self.visit_expr(value);
                        }
                        TableField::Name { value, .. } | TableField::Value(value) => {
                            self.visit_expr(value);
                        }
                    }
                }
            }
            Expr::Function { params, body, .. } => {
                self.push_scope();
                self.declare_params(params);
                self.visit_stmts(body, false);
                self.pop_scope();
            }
            Expr::Nil
            | Expr::True
            | Expr::False
            | Expr::Number(_)
            | Expr::Str(_)
            | Expr::Vararg
            | Expr::SelfExpr
            | Expr::SuperExpr => {}
        }
    }

    fn visit_scoped_block(&mut self, body: &mut [Stmt]) {
        self.push_scope();
        self.visit_stmts(body, false);
        self.pop_scope();
    }

    fn declare_params(&mut self, params: &[Param]) {
        for param in params {
            if let Param::Named { name, .. } = param {
                self.declare(name, false);
            }
        }
    }

    fn declare(&mut self, name: &str, is_const: bool) {
        let current = self.scopes.last_mut().expect("scope stack is never empty");
        if current
            .get(name)
            .is_some_and(|existing_const| *existing_const || is_const)
        {
            self.error(format!(
                "binding '{name}' is already declared in this scope"
            ));
            return;
        }
        current.insert(name.to_string(), is_const);
    }

    fn lookup(&self, name: &str) -> Option<bool> {
        self.scopes
            .iter()
            .rev()
            .find_map(|scope| scope.get(name).copied())
    }

    fn is_const(&self, name: &str) -> bool {
        self.lookup(name).unwrap_or(false)
    }

    fn push_scope(&mut self) {
        self.scopes.push(HashMap::new());
    }

    fn pop_scope(&mut self) {
        self.scopes.pop();
    }

    fn error(&mut self, message: String) {
        self.errors.push(ResolveError { message, line: 0 });
    }
}

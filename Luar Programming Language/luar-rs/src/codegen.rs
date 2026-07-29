use crate::ast::*;
use std::collections::HashMap;

const OPERATOR_META: &[(&str, &str)] = &[
    ("==", "__eq"),
    ("<", "__lt"),
    ("<=", "__le"),
    ("+", "__add"),
    ("-", "__sub"),
    ("*", "__mul"),
    ("/", "__div"),
    ("//", "__idiv"),
    ("%", "__mod"),
    ("^", "__pow"),
    ("..", "__concat"),
    ("#", "__len"),
];

#[derive(Clone)]
enum MethodKind {
    Instance,
    Static,
    Operator,
}

#[derive(Clone)]
struct MethodRecord {
    kind: MethodKind,
    access: Access,
    owner: String,
}

struct ClassRegistry {
    methods: HashMap<String, MethodRecord>,
    parent: Option<String>,
    new_params: Option<Vec<Param>>,
    new_access: Option<Access>,
}

pub struct Codegen {
    out: Vec<String>,
    indent: usize,
    type_env: Vec<HashMap<String, String>>,
    current_class: Option<String>,
    registry: HashMap<String, ClassRegistry>,
}

impl Codegen {
    pub fn new() -> Self {
        Codegen {
            out: Vec::new(),
            indent: 0,
            type_env: vec![HashMap::new()],
            current_class: None,
            registry: HashMap::new(),
        }
    }

    pub fn generate(&mut self, program: &Program) -> String {
        self.out.clear();
        self.indent = 0;
        self.type_env = vec![HashMap::new()];
        self.registry = self.build_registry(program);

        for stmt in &program.stmts {
            self.emit_stmt(stmt);
        }
        self.out.join("\n")
    }

    // ─── Class registry ───────────────────────────────────────────────────────

    fn build_registry(&self, program: &Program) -> HashMap<String, ClassRegistry> {
        let mut reg = HashMap::new();
        for stmt in &program.stmts {
            let Stmt::ClassDecl(decl) = stmt else {
                continue;
            };
            let mut methods = HashMap::new();
            let mut new_params = None;
            let mut new_access = None;

            for (access, member) in Self::flatten_members(decl) {
                if let Member::Method(m) = member {
                    let kind = if m.is_operator {
                        MethodKind::Operator
                    } else if m.is_static {
                        MethodKind::Static
                    } else {
                        MethodKind::Instance
                    };
                    if m.name == "new" && m.is_static {
                        new_params = Some(m.params.clone());
                        new_access = Some(access.clone());
                    }
                    methods.insert(
                        m.name.clone(),
                        MethodRecord {
                            kind,
                            access,
                            owner: decl.name.clone(),
                        },
                    );
                }
            }
            reg.insert(
                decl.name.clone(),
                ClassRegistry {
                    methods,
                    parent: decl.parent.clone(),
                    new_params,
                    new_access,
                },
            );
        }
        reg
    }

    fn flatten_members(decl: &ClassDecl) -> Vec<(Access, Member)> {
        let mut result = Vec::new();
        for m in &decl.top_level_members {
            result.push((Access::Private, m.clone()));
        }
        for block in &decl.blocks {
            for m in &block.members {
                result.push((block.access.clone(), m.clone()));
            }
        }
        result
    }

    fn parent_name(&self) -> Option<String> {
        self.current_class
            .as_ref()
            .and_then(|c| self.registry.get(c))
            .and_then(|r| r.parent.clone())
    }

    fn lookup_method(&self, class_name: &str, method_name: &str) -> Option<MethodRecord> {
        let mut current = Some(class_name.to_string());
        while let Some(name) = current {
            if let Some(reg) = self.registry.get(&name) {
                if let Some(k) = reg.methods.get(method_name) {
                    return Some(k.clone());
                }
                current = reg.parent.clone();
            } else {
                break;
            }
        }
        None
    }

    fn private_method_call(
        &self,
        class_name: &str,
        method_name: &str,
        receiver: &str,
        args: &str,
    ) -> Option<String> {
        let method = self.lookup_method(class_name, method_name)?;
        if method.access != Access::Private
            || self.current_class.as_deref() != Some(method.owner.as_str())
        {
            return None;
        }
        let all_args = match method.kind {
            MethodKind::Instance | MethodKind::Operator => {
                if args.is_empty() {
                    receiver.to_string()
                } else {
                    format!("{receiver}, {args}")
                }
            }
            MethodKind::Static => args.to_string(),
        };
        Some(format!("{method_name}({all_args})"))
    }

    // ─── Type environment ─────────────────────────────────────────────────────

    fn push_scope(&mut self) {
        self.type_env.push(HashMap::new());
    }
    fn pop_scope(&mut self) {
        self.type_env.pop();
    }
    fn set_type(&mut self, name: &str, class: &str) {
        if let Some(top) = self.type_env.last_mut() {
            top.insert(name.to_string(), class.to_string());
        }
    }
    fn resolve_type(&self, expr: &Expr) -> Option<String> {
        match expr {
            Expr::Ident(n) => {
                for frame in self.type_env.iter().rev() {
                    if let Some(t) = frame.get(n) {
                        return Some(t.clone());
                    }
                }
                None
            }
            Expr::SelfExpr => self.current_class.clone(),
            Expr::Call { callee, .. } => {
                if let Expr::Field { obj, name } = callee.as_ref() {
                    if name == "new" {
                        if let Expr::Ident(cn) = obj.as_ref() {
                            if self.registry.contains_key(cn) {
                                return Some(cn.clone());
                            }
                        }
                    }
                }
                None
            }
            _ => None,
        }
    }

    // ─── Output helpers ───────────────────────────────────────────────────────

    fn line(&mut self, s: &str) {
        if s.is_empty() {
            self.out.push(String::new());
        } else {
            self.out.push(format!("{}{s}", "    ".repeat(self.indent)));
        }
    }
    fn blank(&mut self) {
        self.out.push(String::new());
    }

    fn indented(&mut self, f: impl FnOnce(&mut Self)) {
        self.indent += 1;
        f(self);
        self.indent -= 1;
    }

    // ─── Statements ───────────────────────────────────────────────────────────

    fn emit_stmt(&mut self, stmt: &Stmt) {
        match stmt {
            Stmt::ClassDecl(d) => self.emit_class_decl(d),
            Stmt::Local { names, values, .. } => self.emit_local(names, values),
            Stmt::Const { names, values, .. } => {
                let ns = names.join(", ");
                let vs = values
                    .iter()
                    .map(|e| self.emit_expr(e))
                    .collect::<Vec<_>>()
                    .join(", ");
                self.line(&format!("const {ns} = {vs}"));
            }
            Stmt::FunctionDecl {
                name,
                params,
                body,
                is_const,
                ..
            } => {
                let prefix = if *is_const { "const " } else { "" };
                let ps = Self::emit_params_vec(params);
                self.line(&format!("{prefix}function {name}({ps})"));
                self.indented(|s| {
                    s.push_scope();
                    for st in body {
                        s.emit_stmt(st);
                    }
                    s.pop_scope();
                });
                self.line("end");
            }
            Stmt::Assign { targets, values } => {
                let ts = targets
                    .iter()
                    .map(|e| self.emit_expr(e))
                    .collect::<Vec<_>>()
                    .join(", ");
                let vs = values
                    .iter()
                    .map(|e| self.emit_expr(e))
                    .collect::<Vec<_>>()
                    .join(", ");
                self.line(&format!("{ts} = {vs}"));
            }
            Stmt::Do { body } => {
                self.line("do");
                self.indented(|s| {
                    for st in body {
                        s.emit_stmt(st);
                    }
                });
                self.line("end");
            }
            Stmt::While { cond, body } => {
                let c = self.emit_expr(cond);
                self.line(&format!("while {c} do"));
                self.indented(|s| {
                    for st in body {
                        s.emit_stmt(st);
                    }
                });
                self.line("end");
            }
            Stmt::Repeat { body, cond } => {
                self.line("repeat");
                self.indented(|s| {
                    for st in body {
                        s.emit_stmt(st);
                    }
                });
                let c = self.emit_expr(cond);
                self.line(&format!("until {c}"));
            }
            Stmt::If { clauses, else_body } => {
                for (i, clause) in clauses.iter().enumerate() {
                    let kw = if i == 0 { "if" } else { "elseif" };
                    let c = self.emit_expr(&clause.cond);
                    self.line(&format!("{kw} {c} then"));
                    self.indented(|s| {
                        for st in &clause.body {
                            s.emit_stmt(st);
                        }
                    });
                }
                if let Some(eb) = else_body {
                    self.line("else");
                    self.indented(|s| {
                        for st in eb {
                            s.emit_stmt(st);
                        }
                    });
                }
                self.line("end");
            }
            Stmt::NumericFor {
                name,
                start,
                limit,
                step,
                body,
            } => {
                let s = self.emit_expr(start);
                let l = self.emit_expr(limit);
                let st = step
                    .as_ref()
                    .map(|e| format!(", {}", self.emit_expr(e)))
                    .unwrap_or_default();
                self.line(&format!("for {name} = {s}, {l}{st} do"));
                self.indented(|s| {
                    for st in body {
                        s.emit_stmt(st);
                    }
                });
                self.line("end");
            }
            Stmt::GenericFor { names, iters, body } => {
                let ns = names.join(", ");
                let is = iters
                    .iter()
                    .map(|e| self.emit_expr(e))
                    .collect::<Vec<_>>()
                    .join(", ");
                self.line(&format!("for {ns} in {is} do"));
                self.indented(|s| {
                    for st in body {
                        s.emit_stmt(st);
                    }
                });
                self.line("end");
            }
            Stmt::Return(vals) => {
                if vals.is_empty() {
                    self.line("return");
                } else {
                    let vs = vals
                        .iter()
                        .map(|e| self.emit_expr(e))
                        .collect::<Vec<_>>()
                        .join(", ");
                    self.line(&format!("return {vs}"));
                }
            }
            Stmt::Break => self.line("break"),
            Stmt::Continue => self.line("continue"),
            Stmt::ExprStmt(e) => {
                let s = self.emit_expr(e);
                self.line(&s);
            }
            Stmt::ImportDecl { .. } | Stmt::DeclareStmt { .. } => {} // handled by preamble or no output
        }
    }

    fn emit_local(&mut self, names: &[String], values: &[Expr]) {
        let ns = names.join(", ");
        if values.is_empty() {
            self.line(&format!("local {ns}"));
        } else {
            let vs = values
                .iter()
                .map(|e| self.emit_expr(e))
                .collect::<Vec<_>>()
                .join(", ");
            self.line(&format!("local {ns} = {vs}"));
            if names.len() == 1 && values.len() == 1 {
                if let Some(t) = self.resolve_type(&values[0]) {
                    self.set_type(&names[0], &t);
                }
            }
        }
    }

    // ─── Class declaration ────────────────────────────────────────────────────

    fn emit_class_decl(&mut self, decl: &ClassDecl) {
        let name = &decl.name;
        let prev = self.current_class.clone();
        self.current_class = Some(name.clone());

        let all = Self::flatten_members(decl);
        let fields: Vec<_> = all
            .iter()
            .filter_map(|(a, m)| {
                if let Member::Field(f) = m {
                    Some((a.clone(), f.clone()))
                } else {
                    None
                }
            })
            .collect();
        let methods: Vec<_> = all
            .iter()
            .filter_map(|(a, m)| {
                if let Member::Method(m) = m {
                    Some((a.clone(), m.clone()))
                } else {
                    None
                }
            })
            .collect();

        let private_methods: Vec<_> = methods
            .iter()
            .filter(|(a, _)| a == &Access::Private)
            .collect();
        let public_methods: Vec<_> = methods
            .iter()
            .filter(|(a, _)| a == &Access::Public)
            .collect();
        let operator_methods: Vec<_> = public_methods
            .iter()
            .filter(|(_, m)| m.is_operator)
            .collect();
        let normal_methods: Vec<_> = public_methods
            .iter()
            .filter(|(_, m)| !m.is_operator)
            .collect();
        let ctor = normal_methods.iter().find(|(_, m)| m.name == "new");
        let dtor = normal_methods.iter().find(|(_, m)| m.name == "free");
        let others: Vec<_> = normal_methods
            .iter()
            .filter(|(_, m)| m.name != "new" && m.name != "free")
            .collect();

        // class table
        if let Some(parent) = &decl.parent {
            self.line(&format!(
                "local {name} = setmetatable({{}}, {{ __index = {parent} }})"
            ));
        } else {
            self.line(&format!("local {name} = {{}}"));
        }
        self.line(&format!("{name}.__index = {name}"));

        // operator metamethods
        for (_, m) in operator_methods {
            let meta = OPERATOR_META
                .iter()
                .find(|(op, _)| op == &m.operator_op)
                .map(|(_, k)| *k)
                .unwrap_or("__unknown");
            let self_param = Param::Named {
                name: "self".into(),
                ty: None,
            };
            let all_params: Vec<_> = std::iter::once(&self_param)
                .chain(m.params.iter())
                .collect();
            let ps = Self::emit_params_ref(&all_params);
            self.blank();
            self.line(&format!("{name}.{meta} = function({ps})"));
            self.indented(|s| {
                if let Some(body) = &m.body {
                    for st in body {
                        s.emit_stmt(st);
                    }
                }
            });
            self.line("end");
        }

        // private methods as local functions
        for (_, m) in &private_methods {
            if m.is_abstract {
                continue;
            }
            self.blank();
            if m.name == "new" {
                let ps = Self::emit_params_vec(&m.params);
                self.line(&format!("local function new({ps})"));
                self.indented(|s| {
                    s.line(&format!("local self = setmetatable({{}}, {name})"));
                    for (_, f) in &fields {
                        let val = f
                            .value
                            .as_ref()
                            .map(|v| s.emit_expr(v))
                            .unwrap_or_else(|| "nil".to_string());
                        s.line(&format!("self.{} = {val}", f.name));
                    }
                    if let Some(body) = &m.body {
                        s.push_scope();
                        for st in body {
                            s.emit_stmt(st);
                        }
                        s.pop_scope();
                    }
                    s.line("return self");
                });
            } else {
                let ps = if m.is_static {
                    Self::emit_params_vec(&m.params)
                } else {
                    let self_param = Param::Named {
                        name: "self".into(),
                        ty: None,
                    };
                    let all_params: Vec<_> = std::iter::once(&self_param)
                        .chain(m.params.iter())
                        .collect();
                    Self::emit_params_ref(&all_params)
                };
                self.line(&format!("local function {}({ps})", m.name));
                self.indented(|s| {
                    if let Some(body) = &m.body {
                        s.push_scope();
                        for st in body {
                            s.emit_stmt(st);
                        }
                        s.pop_scope();
                    }
                });
            }
            self.line("end");
        }

        // constructor
        if ctor.is_none() {
            if let Some(parent) = &decl.parent {
                self.emit_inherited_new(name, parent, &fields);
            }
        }
        if let Some((_, m)) = ctor {
            let ps = Self::emit_params_vec(&m.params);
            self.blank();
            self.line(&format!("function {name}.new({ps})"));
            self.indented(|s| {
                s.line(&format!("local self = setmetatable({{}}, {name})"));
                for (_, f) in &fields {
                    let val = f
                        .value
                        .as_ref()
                        .map(|v| s.emit_expr(v))
                        .unwrap_or_else(|| "nil".to_string());
                    s.line(&format!("self.{} = {val}", f.name));
                }
                if let Some(body) = &m.body {
                    s.push_scope();
                    for st in body {
                        s.emit_stmt(st);
                    }
                    s.pop_scope();
                }
                s.line("return self");
            });
            self.line("end");
        }

        // static methods
        for (_, m) in others.iter().filter(|(_, m)| m.is_static) {
            let ps = Self::emit_params_vec(&m.params);
            self.blank();
            self.line(&format!("function {name}.{}({ps})", m.name));
            self.indented(|s| {
                if let Some(body) = &m.body {
                    s.push_scope();
                    for st in body {
                        s.emit_stmt(st);
                    }
                    s.pop_scope();
                }
            });
            self.line("end");
        }

        // instance methods
        for (_, m) in others
            .iter()
            .filter(|(_, m)| !m.is_static && !m.is_abstract)
        {
            let self_param = Param::Named {
                name: "self".into(),
                ty: None,
            };
            let all_params: Vec<_> = std::iter::once(&self_param)
                .chain(m.params.iter())
                .collect();
            let ps = Self::emit_params_ref(&all_params);
            self.blank();
            self.line(&format!("function {name}.{}({ps})", m.name));
            self.indented(|s| {
                if let Some(body) = &m.body {
                    s.push_scope();
                    for st in body {
                        s.emit_stmt(st);
                    }
                    s.pop_scope();
                }
            });
            self.line("end");
        }

        // destructor
        if let Some((_, m)) = dtor {
            let self_param = Param::Named {
                name: "self".into(),
                ty: None,
            };
            let all_params: Vec<_> = std::iter::once(&self_param)
                .chain(m.params.iter())
                .collect();
            let ps = Self::emit_params_ref(&all_params);
            self.blank();
            self.line(&format!("function {name}.free({ps})"));
            self.indented(|s| {
                if let Some(body) = &m.body {
                    s.push_scope();
                    for st in body {
                        s.emit_stmt(st);
                    }
                    s.pop_scope();
                }
            });
            self.line("end");
        }

        self.current_class = prev;
    }

    fn emit_inherited_new(&mut self, name: &str, parent: &str, fields: &[(Access, FieldMember)]) {
        // find ancestor new params
        let mut ancestor_params: Option<Vec<Param>> = None;
        let mut cur = Some(parent.to_string());
        while let Some(c) = cur {
            if let Some(reg) = self.registry.get(&c) {
                if let Some(p) = &reg.new_params {
                    if reg.new_access == Some(Access::Public) {
                        ancestor_params = Some(p.clone());
                    }
                    break;
                }
                cur = reg.parent.clone();
            } else {
                break;
            }
        }
        let params = match ancestor_params {
            Some(p) => p,
            None => return,
        };
        let ps = Self::emit_params_vec(&params);
        let args = params
            .iter()
            .map(|p| match p {
                Param::Named { name, .. } => name.clone(),
                Param::Vararg => "...".to_string(),
            })
            .collect::<Vec<_>>()
            .join(", ");
        self.blank();
        self.line(&format!("function {name}.new({ps})"));
        self.indented(|s| {
            s.line(&format!("local self = {parent}.new({args})"));
            s.line(&format!("setmetatable(self, {name})"));
            for (_, f) in fields {
                let val = f
                    .value
                    .as_ref()
                    .map(|v| s.emit_expr(v))
                    .unwrap_or_else(|| "nil".to_string());
                s.line(&format!("self.{} = {val}", f.name));
            }
            s.line("return self");
        });
        self.line("end");
    }

    fn emit_params_vec(params: &[Param]) -> String {
        params
            .iter()
            .map(|p| match p {
                Param::Named { name, .. } => name.clone(),
                Param::Vararg => "...".to_string(),
            })
            .collect::<Vec<_>>()
            .join(", ")
    }

    fn emit_params_ref(params: &[&Param]) -> String {
        params
            .iter()
            .map(|p| match p {
                Param::Named { name, .. } => name.clone(),
                Param::Vararg => "...".to_string(),
            })
            .collect::<Vec<_>>()
            .join(", ")
    }

    // ─── Expressions ──────────────────────────────────────────────────────────

    fn emit_expr(&mut self, expr: &Expr) -> String {
        match expr {
            Expr::Nil => "nil".to_string(),
            Expr::True => "true".to_string(),
            Expr::False => "false".to_string(),
            Expr::Number(v) => v.clone(),
            Expr::Str(v) => {
                if v.starts_with('`') {
                    v.clone()
                } else {
                    let escaped = v.replace('\\', "\\\\").replace('"', "\\\"");
                    format!("\"{escaped}\"")
                }
            }
            Expr::Vararg => "...".to_string(),
            Expr::Ident(n) => n.clone(),
            Expr::SelfExpr => "self".to_string(),
            Expr::SuperExpr => self.parent_name().unwrap_or_else(|| "nil".to_string()),
            Expr::Field { obj, name } => {
                if matches!(obj.as_ref(), Expr::SuperExpr) {
                    let parent = self.parent_name().unwrap_or_else(|| "nil".to_string());
                    return format!("{parent}.{name}");
                }
                let o = self.emit_expr(obj);
                format!("{o}.{name}")
            }
            Expr::Index { obj, key } => {
                let o = self.emit_expr(obj);
                let k = self.emit_expr(key);
                format!("{o}[{k}]")
            }
            Expr::Call { callee, args } => {
                let arg_strs: Vec<_> = args.iter().map(|a| self.emit_expr(a)).collect();
                let args_str = arg_strs.join(", ");
                if let Expr::Field { obj, name } = callee.as_ref() {
                    // super.method(args) → ParentName.method(self, args)
                    if matches!(obj.as_ref(), Expr::SuperExpr) {
                        let parent = self.parent_name().unwrap_or_else(|| "nil".to_string());
                        let all_args = if args_str.is_empty() {
                            "self".to_string()
                        } else {
                            format!("self, {args_str}")
                        };
                        return format!("{parent}.{name}({all_args})");
                    }
                    let receiver = self.emit_expr(obj);
                    if let Expr::Ident(class_name) = obj.as_ref() {
                        if self.registry.contains_key(class_name) {
                            if let Some(call) =
                                self.private_method_call(class_name, name, &receiver, &args_str)
                            {
                                return call;
                            }
                        }
                    }
                    // dot→colon for instance method calls
                    let obj_type = self.resolve_type(obj);
                    if let Some(class_name) = obj_type {
                        if let Some(call) =
                            self.private_method_call(&class_name, name, &receiver, &args_str)
                        {
                            return call;
                        }
                        if let Some(method) = self.lookup_method(&class_name, name) {
                            if matches!(method.kind, MethodKind::Instance) {
                                return format!("{receiver}:{name}({args_str})");
                            }
                        }
                    }
                }
                let callee_str = self.emit_expr(callee);
                format!("{callee_str}({args_str})")
            }
            Expr::MethodCall { obj, method, args } => {
                let o = self.emit_expr(obj);
                let args_str = args
                    .iter()
                    .map(|a| self.emit_expr(a))
                    .collect::<Vec<_>>()
                    .join(", ");
                if let Some(class_name) = self.resolve_type(obj) {
                    if let Some(call) = self.private_method_call(&class_name, method, &o, &args_str)
                    {
                        return call;
                    }
                }
                format!("{o}:{method}({args_str})")
            }
            Expr::Unop { op, expr } => {
                let e = self.emit_expr(expr);
                format!("{op} {e}")
            }
            Expr::Binop { op, left, right } => {
                let l = self.emit_expr(left);
                let r = self.emit_expr(right);
                format!("{l} {op} {r}")
            }
            Expr::Table(fields) => {
                if fields.is_empty() {
                    return "{}".to_string();
                }
                let parts: Vec<_> = fields
                    .iter()
                    .map(|f| match f {
                        TableField::Index { key, value } => {
                            let k = self.emit_expr(key);
                            let v = self.emit_expr(value);
                            format!("[{k}] = {v}")
                        }
                        TableField::Name { name, value } => {
                            let v = self.emit_expr(value);
                            format!("{name} = {v}")
                        }
                        TableField::Value(e) => self.emit_expr(e),
                    })
                    .collect();
                format!("{{ {} }}", parts.join(", "))
            }
            Expr::Function { params, body, .. } => {
                // inline function: capture output at current indent
                let saved = std::mem::take(&mut self.out);
                self.indent += 1;
                self.push_scope();
                for st in body {
                    self.emit_stmt(st);
                }
                self.pop_scope();
                let body_lines = std::mem::replace(&mut self.out, saved);
                self.indent -= 1;
                let body_str = body_lines.join("\n");
                let ps = Self::emit_params_vec(params);
                format!(
                    "function({ps})\n{body_str}\n{}end",
                    "    ".repeat(self.indent)
                )
            }
        }
    }
}

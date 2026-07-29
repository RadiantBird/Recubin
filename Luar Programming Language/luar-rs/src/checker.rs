use crate::ast::*;
use std::collections::{HashMap, HashSet};

#[derive(Debug, Clone)]
pub struct CheckError {
    pub message: String,
    pub line: usize,
}

#[derive(Clone)]
struct ClassInfo {
    name: String,
    is_abstract: bool,
    parent_name: Option<String>,
    methods: HashMap<String, MethodInfo>,
    fields: HashMap<String, FieldInfo>,
}

#[derive(Clone)]
struct MethodInfo {
    method: MethodMember,
    access: Access,
    class_name: String,
}

#[derive(Clone)]
struct FieldInfo {
    #[allow(dead_code)]
    field: FieldMember,
    access: Access,
    class_name: String,
}

#[derive(Clone)]
enum ReceiverKind {
    Class(String),
    Instance(String),
}

pub struct Checker {
    errors: Vec<CheckError>,
    classes: HashMap<String, ClassInfo>,
}

impl Checker {
    pub fn new() -> Self {
        Checker {
            errors: Vec::new(),
            classes: HashMap::new(),
        }
    }

    pub fn check(&mut self, program: &mut Program) -> Vec<CheckError> {
        self.errors.clear();
        self.classes.clear();
        // Pass 1: register classes
        for stmt in &program.stmts {
            if let Stmt::ClassDecl(decl) = stmt {
                self.register_class(decl);
            }
        }

        // Pass 2: validate classes
        let decls: Vec<ClassDecl> = program
            .stmts
            .iter()
            .filter_map(|s| {
                if let Stmt::ClassDecl(d) = s {
                    Some(d.clone())
                } else {
                    None
                }
            })
            .collect();
        for decl in &decls {
            self.check_class(decl);
        }

        // Pass 3: access control
        self.check_access_control(program);

        std::mem::take(&mut self.errors)
    }

    // ─── Pass 1: Registration ─────────────────────────────────────────────────

    fn register_class(&mut self, decl: &ClassDecl) {
        if self.classes.contains_key(&decl.name) {
            self.err(
                format!("class '{}' is already defined", decl.name),
                decl.line,
            );
            return;
        }
        let mut info = ClassInfo {
            name: decl.name.clone(),
            is_abstract: decl.is_abstract,
            parent_name: decl.parent.clone(),
            methods: HashMap::new(),
            fields: HashMap::new(),
        };
        let mut member_names = HashSet::new();
        for (access, member) in Self::flatten_members(decl) {
            let member_name = match &member {
                Member::Method(m) => &m.name,
                Member::Field(f) => &f.name,
            };
            if !member_names.insert(member_name.clone()) {
                self.err(
                    format!(
                        "member '{}' is defined more than once in class '{}'",
                        member_name, decl.name
                    ),
                    decl.line,
                );
                continue;
            }
            match member {
                Member::Method(m) => {
                    info.methods.insert(
                        m.name.clone(),
                        MethodInfo {
                            method: m,
                            access,
                            class_name: decl.name.clone(),
                        },
                    );
                }
                Member::Field(f) => {
                    info.fields.insert(
                        f.name.clone(),
                        FieldInfo {
                            field: f,
                            access,
                            class_name: decl.name.clone(),
                        },
                    );
                }
            }
        }
        self.classes.insert(decl.name.clone(), info);
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

    // ─── Pass 2: Validation ───────────────────────────────────────────────────

    fn check_class(&mut self, decl: &ClassDecl) {
        self.check_inheritance(decl);
        let members = Self::flatten_members(decl);
        let info_clone = self.classes.get(&decl.name).cloned();
        if let Some(info) = info_clone {
            for (access, member) in &members {
                if let Member::Method(m) = member {
                    if m.is_operator && access == &Access::Private {
                        self.err(
                            format!("operator method '{}' must be public", m.name),
                            decl.line,
                        );
                    }
                    self.check_method(m, &info, decl);
                }
            }
        }
        if !decl.is_abstract {
            if let Some(name) = self.first_unimplemented_abstract_method(&decl.name) {
                self.err(
                    format!(
                        "class '{}' must be abstract or implement abstract method '{}'",
                        decl.name, name
                    ),
                    decl.line,
                );
            }
        }
    }

    fn check_inheritance(&mut self, decl: &ClassDecl) {
        let parent = match &decl.parent {
            Some(p) => p.clone(),
            None => return,
        };
        if !self.classes.contains_key(&parent) {
            self.err(format!("unknown parent class '{}'", parent), decl.line);
            return;
        }
        // circular detection
        let mut visited = HashSet::new();
        visited.insert(decl.name.clone());
        let mut current = Some(parent.clone());
        while let Some(cur) = current {
            if visited.contains(&cur) {
                self.err(
                    format!(
                        "circular inheritance detected: '{}' -> '{}'",
                        decl.name, cur
                    ),
                    decl.line,
                );
                return;
            }
            visited.insert(cur.clone());
            current = self.classes.get(&cur).and_then(|i| i.parent_name.clone());
        }
    }

    fn check_method(&mut self, method: &MethodMember, class_info: &ClassInfo, decl: &ClassDecl) {
        let line = decl.line;

        if method.name == "new" && !method.is_static {
            self.err("constructor 'new' must be static".to_string(), line);
        }
        if method.name == "free" && method.is_static {
            self.err("destructor 'free' cannot be static".to_string(), line);
        }
        if method.is_operator && method.is_static {
            self.err(
                format!("operator method '{}' cannot be static", method.name),
                line,
            );
        }
        if method.is_abstract && method.is_final {
            self.err(
                format!("abstract method '{}' cannot be final", method.name),
                line,
            );
        }
        if method.is_abstract && !class_info.is_abstract {
            self.err(
                format!(
                    "method '{}' is abstract but class '{}' is not abstract",
                    method.name, class_info.name
                ),
                line,
            );
        }

        let parent_name = match &class_info.parent_name {
            Some(p) => p.clone(),
            None => {
                if method.is_override {
                    self.err(
                        format!(
                            "method '{}' uses 'override' but class '{}' has no parent",
                            method.name, class_info.name
                        ),
                        line,
                    );
                }
                return;
            }
        };

        let parent_method = self.lookup_method_in_ancestors(&method.name, &parent_name);

        if method.is_override {
            match &parent_method {
                None => {
                    self.err(format!("method '{}' uses 'override' but no such method exists in parent classes", method.name), line);
                }
                Some(pm) => {
                    if pm.method.is_final {
                        self.err(
                            format!(
                                "cannot override final method '{}' from class '{}'",
                                method.name, pm.class_name
                            ),
                            line,
                        );
                    }
                    if !Self::signatures_match(method, &pm.method) {
                        self.err(
                            format!(
                                "override of '{}' does not exactly match the parent signature",
                                method.name
                            ),
                            line,
                        );
                    }
                }
            }
        } else if parent_method.is_some() {
            self.err(
                format!(
                    "method '{}' shadows parent method but is missing 'override' keyword",
                    method.name
                ),
                line,
            );
        }
    }

    fn signatures_match(method: &MethodMember, parent: &MethodMember) -> bool {
        if method.is_static != parent.is_static
            || method.params.len() != parent.params.len()
            || method.return_type != parent.return_type
        {
            return false;
        }

        method
            .params
            .iter()
            .zip(&parent.params)
            .all(|(left, right)| match (left, right) {
                (Param::Vararg, Param::Vararg) => true,
                (Param::Named { ty: left_ty, .. }, Param::Named { ty: right_ty, .. }) => {
                    left_ty == right_ty
                }
                _ => false,
            })
    }

    fn first_unimplemented_abstract_method(&self, class_name: &str) -> Option<String> {
        let mut chain = Vec::new();
        let mut current = Some(class_name.to_string());
        let mut visited = HashSet::new();
        while let Some(name) = current {
            if !visited.insert(name.clone()) {
                return None;
            }
            let info = self.classes.get(&name)?;
            chain.push(info.clone());
            current = info.parent_name.clone();
        }
        chain.reverse();

        let mut methods: HashMap<String, bool> = HashMap::new();
        for info in chain {
            for (name, method) in info.methods {
                methods.insert(name, method.method.is_abstract);
            }
        }
        methods
            .into_iter()
            .find_map(|(name, is_abstract)| is_abstract.then_some(name))
    }

    fn lookup_method_in_ancestors(&self, name: &str, start: &str) -> Option<MethodInfo> {
        let mut current = Some(start.to_string());
        let mut visited = HashSet::new();
        while let Some(class_name) = current {
            if !visited.insert(class_name.clone()) {
                return None;
            }
            let info = self.classes.get(&class_name)?;
            if let Some(m) = info.methods.get(name) {
                return Some(m.clone());
            }
            current = info.parent_name.clone();
        }
        None
    }

    // ─── Pass 3: Access control ───────────────────────────────────────────────

    fn check_access_control(&mut self, program: &Program) {
        let stmts = program.stmts.clone();
        let mut global_env = HashMap::new();
        for stmt in &stmts {
            match stmt {
                Stmt::ClassDecl(decl) => self.check_class_body_access(decl),
                _ => self.check_stmt_access(stmt, &mut global_env, None, false, 0),
            }
        }
    }

    fn check_class_body_access(&mut self, decl: &ClassDecl) {
        let members = Self::flatten_members(decl);
        for (_, member) in members {
            if let Member::Method(m) = member {
                if let Some(body) = &m.body {
                    let mut env = HashMap::new();
                    for p in &m.params {
                        if let Param::Named {
                            name,
                            ty: Some(TypeExpr::Name(type_name)),
                        } = p
                        {
                            if self.classes.contains_key(type_name) {
                                env.insert(name.clone(), type_name.clone());
                            }
                        }
                    }
                    let body = body.clone();
                    let can_use_super = !m.is_static;
                    self.check_body_access(
                        &body,
                        &mut env,
                        Some(&decl.name),
                        can_use_super,
                        decl.line,
                    );
                }
            }
        }
    }

    fn check_body_access(
        &mut self,
        stmts: &[Stmt],
        env: &mut HashMap<String, String>,
        current_class: Option<&str>,
        can_use_super: bool,
        line: usize,
    ) {
        for stmt in stmts {
            self.check_stmt_access(stmt, env, current_class, can_use_super, line);
        }
    }

    fn check_stmt_access(
        &mut self,
        stmt: &Stmt,
        env: &mut HashMap<String, String>,
        current_class: Option<&str>,
        can_use_super: bool,
        line: usize,
    ) {
        match stmt {
            Stmt::Local { names, values, .. } => {
                for v in values {
                    self.check_expr_access(v, env, current_class, can_use_super, line);
                }
                if names.len() == 1 && values.len() == 1 {
                    if let Some(ReceiverKind::Instance(t)) =
                        self.infer_receiver(&values[0], env, current_class)
                    {
                        env.insert(names[0].clone(), t);
                    }
                }
            }
            Stmt::Const { names, values, .. } => {
                for v in values {
                    self.check_expr_access(v, env, current_class, can_use_super, line);
                }
                if names.len() == 1 && values.len() == 1 {
                    if let Some(ReceiverKind::Instance(t)) =
                        self.infer_receiver(&values[0], env, current_class)
                    {
                        env.insert(names[0].clone(), t);
                    }
                }
            }
            Stmt::FunctionDecl { params, body, .. } => {
                let mut function_env = env.clone();
                for param in params {
                    if let Param::Named {
                        name,
                        ty: Some(TypeExpr::Name(type_name)),
                    } = param
                    {
                        if self.classes.contains_key(type_name) {
                            function_env.insert(name.clone(), type_name.clone());
                        }
                    }
                }
                self.check_body_access(body, &mut function_env, current_class, can_use_super, line);
            }
            Stmt::Assign { targets, values } => {
                for e in targets.iter().chain(values.iter()) {
                    self.check_expr_access(e, env, current_class, can_use_super, line);
                }
            }
            Stmt::Return(vals) => {
                for e in vals {
                    self.check_expr_access(e, env, current_class, can_use_super, line);
                }
            }
            Stmt::ExprStmt(e) => {
                self.check_expr_access(e, env, current_class, can_use_super, line);
            }
            Stmt::Do { body } => {
                self.check_body_access(body, &mut env.clone(), current_class, can_use_super, line);
            }
            Stmt::While { cond, body } => {
                self.check_expr_access(cond, env, current_class, can_use_super, line);
                self.check_body_access(body, &mut env.clone(), current_class, can_use_super, line);
            }
            Stmt::Repeat { body, cond } => {
                self.check_body_access(body, &mut env.clone(), current_class, can_use_super, line);
                self.check_expr_access(cond, env, current_class, can_use_super, line);
            }
            Stmt::If { clauses, else_body } => {
                for c in clauses {
                    self.check_expr_access(&c.cond, env, current_class, can_use_super, line);
                    self.check_body_access(
                        &c.body,
                        &mut env.clone(),
                        current_class,
                        can_use_super,
                        line,
                    );
                }
                if let Some(eb) = else_body {
                    self.check_body_access(
                        eb,
                        &mut env.clone(),
                        current_class,
                        can_use_super,
                        line,
                    );
                }
            }
            Stmt::NumericFor {
                start,
                limit,
                step,
                body,
                ..
            } => {
                self.check_expr_access(start, env, current_class, can_use_super, line);
                self.check_expr_access(limit, env, current_class, can_use_super, line);
                if let Some(s) = step {
                    self.check_expr_access(s, env, current_class, can_use_super, line);
                }
                self.check_body_access(body, &mut env.clone(), current_class, can_use_super, line);
            }
            Stmt::GenericFor { iters, body, .. } => {
                for e in iters {
                    self.check_expr_access(e, env, current_class, can_use_super, line);
                }
                self.check_body_access(body, &mut env.clone(), current_class, can_use_super, line);
            }
            _ => {}
        }
    }

    fn check_expr_access(
        &mut self,
        expr: &Expr,
        env: &HashMap<String, String>,
        current_class: Option<&str>,
        can_use_super: bool,
        line: usize,
    ) {
        match expr {
            Expr::Field { obj, name } => {
                if matches!(obj.as_ref(), Expr::SuperExpr) {
                    self.err("'super' must be used as 'super.method()'".to_string(), line);
                    return;
                }
                if let Some(receiver) = self.infer_receiver(obj, env, current_class) {
                    self.check_member_use(&receiver, name, current_class, line);
                }
                self.check_expr_access(obj, env, current_class, can_use_super, line);
            }
            Expr::Call { callee, args } => {
                if let Expr::Field { obj, name } = callee.as_ref() {
                    if matches!(obj.as_ref(), Expr::SuperExpr) {
                        self.check_super_call(name, current_class, can_use_super, line);
                    } else {
                        if let Some(ReceiverKind::Class(class_name)) =
                            self.infer_receiver(obj, env, current_class)
                        {
                            if name == "new"
                                && self
                                    .classes
                                    .get(&class_name)
                                    .is_some_and(|info| info.is_abstract)
                            {
                                self.err(
                                    format!("cannot instantiate abstract class '{}'", class_name),
                                    line,
                                );
                            }
                        }
                        if let Some(receiver) = self.infer_receiver(obj, env, current_class) {
                            self.check_member_use(&receiver, name, current_class, line);
                        }
                        self.check_expr_access(obj, env, current_class, can_use_super, line);
                    }
                } else {
                    self.check_expr_access(callee, env, current_class, can_use_super, line);
                }
                for a in args {
                    self.check_expr_access(a, env, current_class, can_use_super, line);
                }
            }
            Expr::MethodCall { obj, method, args } => {
                if let Some(receiver) = self.infer_receiver(obj, env, current_class) {
                    self.check_member_use(&receiver, method, current_class, line);
                }
                self.check_expr_access(obj, env, current_class, can_use_super, line);
                for a in args {
                    self.check_expr_access(a, env, current_class, can_use_super, line);
                }
            }
            Expr::Index { obj, key } => {
                self.check_expr_access(obj, env, current_class, can_use_super, line);
                self.check_expr_access(key, env, current_class, can_use_super, line);
            }
            Expr::Unop { expr, .. } => {
                self.check_expr_access(expr, env, current_class, can_use_super, line);
            }
            Expr::Binop { left, right, .. } => {
                self.check_expr_access(left, env, current_class, can_use_super, line);
                self.check_expr_access(right, env, current_class, can_use_super, line);
            }
            Expr::Table(fields) => {
                for f in fields {
                    match f {
                        TableField::Value(v) | TableField::Name { value: v, .. } => {
                            self.check_expr_access(v, env, current_class, can_use_super, line);
                        }
                        TableField::Index { key, value } => {
                            self.check_expr_access(key, env, current_class, can_use_super, line);
                            self.check_expr_access(value, env, current_class, can_use_super, line);
                        }
                    }
                }
            }
            Expr::Function { body, .. } => {
                self.check_body_access(body, &mut env.clone(), current_class, can_use_super, line);
            }
            Expr::SuperExpr => {
                self.err("'super' must be used as 'super.method()'".to_string(), line);
            }
            _ => {}
        }
    }

    fn infer_receiver(
        &self,
        expr: &Expr,
        env: &HashMap<String, String>,
        current_class: Option<&str>,
    ) -> Option<ReceiverKind> {
        match expr {
            Expr::Ident(n) => {
                if let Some(class_name) = env.get(n) {
                    Some(ReceiverKind::Instance(class_name.clone()))
                } else if self.classes.contains_key(n) {
                    Some(ReceiverKind::Class(n.clone()))
                } else {
                    None
                }
            }
            Expr::SelfExpr => current_class.map(|name| ReceiverKind::Instance(name.to_string())),
            Expr::Call { callee, .. } => {
                if let Expr::Field { obj, name } = callee.as_ref() {
                    if name == "new" {
                        if let Expr::Ident(class_name) = obj.as_ref() {
                            if self.classes.contains_key(class_name) {
                                return Some(ReceiverKind::Instance(class_name.clone()));
                            }
                        }
                    }
                }
                None
            }
            _ => None,
        }
    }

    fn lookup_field_in_ancestors(&self, name: &str, start: &str) -> Option<FieldInfo> {
        let mut current = Some(start.to_string());
        let mut visited = HashSet::new();
        while let Some(class_name) = current {
            if !visited.insert(class_name.clone()) {
                return None;
            }
            let info = self.classes.get(&class_name)?;
            if let Some(field) = info.fields.get(name) {
                return Some(field.clone());
            }
            current = info.parent_name.clone();
        }
        None
    }

    fn check_member_use(
        &mut self,
        receiver: &ReceiverKind,
        member_name: &str,
        current_class: Option<&str>,
        line: usize,
    ) {
        let (class_name, receiver_is_class) = match receiver {
            ReceiverKind::Class(name) => (name, true),
            ReceiverKind::Instance(name) => (name, false),
        };

        if let Some(method) = self.lookup_method_in_ancestors(member_name, class_name) {
            if method.access == Access::Private && current_class != Some(method.class_name.as_str())
            {
                self.err(
                    format!(
                        "cannot access private method '{}' of class '{}' from outside",
                        member_name, method.class_name
                    ),
                    line,
                );
            }
            if receiver_is_class && !method.method.is_static {
                self.err(
                    format!(
                        "instance method '{}' cannot be called on class '{}'",
                        member_name, class_name
                    ),
                    line,
                );
            } else if !receiver_is_class && method.method.is_static {
                self.err(
                    format!(
                        "static method '{}' cannot be called on an instance of '{}'",
                        member_name, class_name
                    ),
                    line,
                );
            }
            return;
        }

        if let Some(field) = self.lookup_field_in_ancestors(member_name, class_name) {
            if field.access == Access::Private && current_class != Some(field.class_name.as_str()) {
                self.err(
                    format!(
                        "cannot access private field '{}' of class '{}' from outside",
                        member_name, field.class_name
                    ),
                    line,
                );
            }
            if receiver_is_class {
                self.err(
                    format!(
                        "instance field '{}' cannot be accessed on class '{}'",
                        member_name, class_name
                    ),
                    line,
                );
            }
        }
    }

    fn check_super_call(
        &mut self,
        method_name: &str,
        current_class: Option<&str>,
        can_use_super: bool,
        line: usize,
    ) {
        let Some(class_name) = current_class else {
            self.err("'super' cannot be used outside a class".to_string(), line);
            return;
        };
        if !can_use_super {
            self.err(
                "'super' cannot be used in a static method".to_string(),
                line,
            );
            return;
        }
        let Some(parent_name) = self
            .classes
            .get(class_name)
            .and_then(|info| info.parent_name.clone())
        else {
            self.err(
                format!("class '{}' has no parent for 'super'", class_name),
                line,
            );
            return;
        };
        let Some(parent) = self.classes.get(&parent_name) else {
            return;
        };
        let Some(method) = parent.methods.get(method_name).cloned() else {
            self.err(
                format!(
                    "method '{}' does not exist on direct parent class '{}'",
                    method_name, parent_name
                ),
                line,
            );
            return;
        };
        if method.method.is_static {
            self.err(
                format!(
                    "static parent method '{}' cannot be called through 'super'",
                    method_name
                ),
                line,
            );
        }
        if method.access == Access::Private {
            self.err(
                format!(
                    "cannot access private method '{}' of parent class '{}'",
                    method_name, parent_name
                ),
                line,
            );
        }
    }

    fn err(&mut self, message: String, line: usize) {
        self.errors.push(CheckError { message, line });
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::parser::Parser;

    fn compile_errors(src: &str) -> Vec<String> {
        let mut p = Parser::new(src).unwrap();
        let mut program = p.parse().unwrap();
        Checker::new()
            .check(&mut program)
            .into_iter()
            .map(|e| e.message)
            .collect()
    }

    #[test]
    fn private_new_outside_class_is_error() {
        let src = r#"
class Main is
    static function new()
        print("Hello, Luar!")
    end
end
local lMain = Main.new()
"#;
        let errors = compile_errors(src);
        assert!(
            errors
                .iter()
                .any(|e| e.contains("private") && e.contains("new")),
            "expected private access error for new, got: {:?}",
            errors
        );
    }

    #[test]
    fn public_new_outside_class_is_ok() {
        let src = r#"
class Main is
    public is
        static function new()
            print("Hello, Luar!")
        end
    end
end
local lMain = Main.new()
"#;
        let errors = compile_errors(src);
        assert!(errors.is_empty(), "expected no errors, got: {:?}", errors);
    }
}

#[derive(Debug, Clone, PartialEq)]
pub enum TypeExpr {
    Name(String),
    Optional(Box<TypeExpr>),
    Tuple(Vec<TypeExpr>),
}

#[derive(Debug, Clone)]
pub enum Expr {
    Nil,
    True,
    False,
    Number(String),
    Str(String), // includes template strings
    Vararg,
    Ident(String),
    SelfExpr,
    SuperExpr,
    Field {
        obj: Box<Expr>,
        name: String,
    },
    Index {
        obj: Box<Expr>,
        key: Box<Expr>,
    },
    Call {
        callee: Box<Expr>,
        args: Vec<Expr>,
    },
    MethodCall {
        obj: Box<Expr>,
        method: String,
        args: Vec<Expr>,
    },
    Unop {
        op: String,
        expr: Box<Expr>,
    },
    Binop {
        op: String,
        left: Box<Expr>,
        right: Box<Expr>,
    },
    Table(Vec<TableField>),
    Function {
        params: Vec<Param>,
        return_type: Option<TypeExpr>,
        body: Vec<Stmt>,
    },
}

#[derive(Debug, Clone)]
pub enum TableField {
    Index { key: Expr, value: Expr },
    Name { name: String, value: Expr },
    Value(Expr),
}

#[derive(Debug, Clone, PartialEq)]
pub enum Param {
    Named { name: String, ty: Option<TypeExpr> },
    Vararg,
}

#[derive(Debug, Clone)]
pub enum Stmt {
    Local {
        names: Vec<String>,
        types: Vec<Option<TypeExpr>>,
        values: Vec<Expr>,
    },
    Assign {
        targets: Vec<Expr>,
        values: Vec<Expr>,
    },
    Do {
        body: Vec<Stmt>,
    },
    While {
        cond: Expr,
        body: Vec<Stmt>,
    },
    Repeat {
        body: Vec<Stmt>,
        cond: Expr,
    },
    If {
        clauses: Vec<IfClause>,
        else_body: Option<Vec<Stmt>>,
    },
    NumericFor {
        name: String,
        start: Expr,
        limit: Expr,
        step: Option<Expr>,
        body: Vec<Stmt>,
    },
    GenericFor {
        names: Vec<String>,
        iters: Vec<Expr>,
        body: Vec<Stmt>,
    },
    Return(Vec<Expr>),
    Break,
    Continue,
    ExprStmt(Expr),
    ClassDecl(ClassDecl),
    ImportDecl {
        module_name: String,
    },
    DeclareStmt {
        is_global: bool,
        name: String,
        ty: TypeExpr,
        module_name: Option<String>,
    },
}

#[derive(Debug, Clone)]
pub struct IfClause {
    pub cond: Expr,
    pub body: Vec<Stmt>,
}

#[derive(Debug, Clone)]
pub struct ClassDecl {
    pub name: String,
    pub is_abstract: bool,
    pub parent: Option<String>,
    pub top_level_members: Vec<Member>,
    pub blocks: Vec<MemberBlock>,
    pub line: usize,
}

#[derive(Debug, Clone)]
pub struct MemberBlock {
    pub access: Access,
    pub members: Vec<Member>,
}

#[derive(Debug, Clone, PartialEq)]
pub enum Access {
    Public,
    Private,
}

#[derive(Debug, Clone)]
pub enum Member {
    Field(FieldMember),
    Method(MethodMember),
}

#[derive(Debug, Clone)]
pub struct FieldMember {
    pub name: String,
    pub ty: Option<TypeExpr>,
    pub value: Option<Expr>,
}

#[derive(Debug, Clone)]
pub struct MethodMember {
    pub name: String,
    pub is_operator: bool,
    pub operator_op: String,
    pub is_static: bool,
    pub is_abstract: bool,
    pub is_override: bool,
    pub is_final: bool,
    pub params: Vec<Param>,
    pub return_type: Option<TypeExpr>,
    pub body: Option<Vec<Stmt>>,
}

#[derive(Debug, Clone)]
pub struct Program {
    pub stmts: Vec<Stmt>,
}

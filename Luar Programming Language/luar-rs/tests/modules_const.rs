use luar_rs::compile_source;
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};

static TEMP_ID: AtomicU64 = AtomicU64::new(0);

struct TempProject {
    path: PathBuf,
}

impl TempProject {
    fn new() -> Self {
        let id = TEMP_ID.fetch_add(1, Ordering::Relaxed);
        let path = std::env::temp_dir().join(format!(
            "luar-rs-{}-{}-{id}",
            std::process::id(),
            std::thread::current().name().unwrap_or("test")
        ));
        fs::create_dir_all(&path).unwrap();
        Self { path }
    }

    fn source_path(&self) -> PathBuf {
        self.path.join("main.luar")
    }

    fn definition(&self, module: &str, source: &str) {
        fs::write(self.path.join(format!("{module}.luard")), source).unwrap();
    }

    fn definition_bytes(&self, module: &str, source: &[u8]) {
        fs::write(self.path.join(format!("{module}.luard")), source).unwrap();
    }
}

impl Drop for TempProject {
    fn drop(&mut self) {
        let _ = fs::remove_dir_all(&self.path);
    }
}

fn compile_at(source: &str, path: &Path) -> Result<String, Vec<String>> {
    compile_source(source, Some(path))
}

fn assert_error(errors: &[String], expected: &str) {
    assert!(
        errors.iter().any(|error| error.contains(expected)),
        "expected an error containing {expected:?}, got {errors:#?}"
    );
}

#[test]
fn adjacent_definition_rewrites_unqualified_reads_writes_and_calls() {
    let project = TempProject::new();
    project.definition(
        "numbers",
        r#"
-- declarations can contain comments and blank lines
declare answer: number
declare calculate: (number, number)
"#,
    );

    let output = compile_at(
        r#"
import numbers
print(answer)
answer = calculate(answer, 1)
local record = { answer = answer, [answer] = calculate }
"#,
        &project.source_path(),
    )
    .unwrap();

    assert!(output.contains("print(numbers.answer)"));
    assert!(output.contains("numbers.answer = numbers.calculate(numbers.answer, 1)"));
    assert!(output.contains("{ answer = numbers.answer, [numbers.answer] = numbers.calculate }"));
    assert!(!output.contains("import"));
}

#[test]
fn ambiguity_is_reported_only_when_unqualified_name_is_used() {
    let project = TempProject::new();
    project.definition("first", "declare shared: string");
    project.definition("second", "declare shared: number");

    let unused = compile_at(
        "import first\nimport second\nprint(first.shared, second.shared)",
        &project.source_path(),
    )
    .unwrap();
    assert!(unused.contains("print(first.shared, second.shared)"));

    let errors = compile_at(
        "import first\nimport second\nprint(shared)",
        &project.source_path(),
    )
    .unwrap_err();
    assert_error(&errors, "ambiguous unqualified name 'shared'");
    assert_error(&errors, "first, second");
}

#[test]
fn globals_unknown_names_and_qualified_names_remain_unqualified() {
    let project = TempProject::new();
    project.definition(
        "engine",
        "declare value: number\ndeclare global workspace: workspace",
    );

    let output = compile_at(
        "import engine\nprint(engine.value, workspace, unknownGlobal)",
        &project.source_path(),
    )
    .unwrap();
    assert!(output.contains("print(engine.value, workspace, unknownGlobal)"));
    assert!(!output.contains("engine.workspace"));
}

#[test]
fn lexical_bindings_shadow_module_members() {
    let project = TempProject::new();
    project.definition("module", "declare value: number");

    let output = compile_at(
        r#"
import module
local value = 1
do
    const value = 2
    print(value)
end
local callback = function(value)
    print(value)
end
for value = 1, 2 do
    print(value)
end
for value in values do
    print(value)
end
"#,
        &project.source_path(),
    )
    .unwrap();

    assert!(!output.contains("module.value"));
}

#[test]
fn class_and_function_names_shadow_module_members() {
    let project = TempProject::new();
    project.definition("module", "declare Worker: string\ndeclare recurse: string");

    let output = compile_at(
        r#"
import module
class Worker is
    function recurse()
        recurse()
    end
end
const function recurse()
    recurse()
end
"#,
        &project.source_path(),
    )
    .unwrap();

    assert!(!output.contains("module.Worker"));
    assert!(!output.contains("module.recurse"));
}

#[test]
fn definition_and_import_failures_include_actionable_diagnostics() {
    let project = TempProject::new();

    let missing = compile_at("import missing\nprint(value)", &project.source_path()).unwrap_err();
    assert_error(&missing, "cannot read module definition");
    assert_error(&missing, "missing.luard");

    project.definition("bad", "declare okay: number\nthis is not valid");
    let invalid = compile_at("import bad", &project.source_path()).unwrap_err();
    assert_error(&invalid, "bad.luard:2");
    assert_error(&invalid, "expected 'declare'");

    project.definition("duplicate", "declare value: number\ndeclare value: string");
    let duplicate = compile_at("import duplicate", &project.source_path()).unwrap_err();
    assert_error(&duplicate, "duplicate.luard:2");
    assert_error(&duplicate, "declared more than once");

    project.definition_bytes("utf8", &[0xff, 0xfe]);
    let utf8 = compile_at("import utf8", &project.source_path()).unwrap_err();
    assert_error(&utf8, "not valid UTF-8");
}

#[test]
fn duplicate_imports_old_api_and_source_declare_are_rejected() {
    let project = TempProject::new();
    project.definition("module", "declare value: number");

    let duplicate = compile_at(
        "import module\nimport module\nprint(value)",
        &project.source_path(),
    )
    .unwrap_err();
    assert_error(&duplicate, "imported more than once");

    let no_path = compile_source("import module\nprint(value)", None).unwrap_err();
    assert_error(&no_path, "require luar_compile_with_path");

    let empty_path =
        compile_source("import module\nprint(value)", Some(Path::new(""))).unwrap_err();
    assert_error(&empty_path, "require a non-empty source file path");

    let source_declare = compile_source("declare value: number\nprint(value)", None).unwrap_err();
    assert_error(
        &source_declare,
        "'declare' is only allowed in .luard module definition files",
    );
}

#[test]
fn const_declarations_and_const_functions_generate_luau_syntax() {
    let output = compile_source(
        r#"
const first: number, second = 1, 2
const function add(value: number): number
    return first + value
end
local const = "contextual"
print(first, second, add(3), const)
"#,
        None,
    )
    .unwrap();

    assert!(output.contains("const first, second = 1, 2"));
    assert!(output.contains("const function add(value)"));
    assert!(output.contains("return first + value"));
    assert!(output.contains("local const = \"contextual\""));
}

#[test]
fn const_remains_an_identifier_outside_declaration_context() {
    let output = compile_source(
        r#"
local const = function()
    return 1
end
const()
const = function()
    return 2
end
"#,
        None,
    )
    .unwrap();

    assert!(output.contains("const()"));
    assert!(output.contains("const = function()"));
}

#[test]
fn const_requires_initialization_and_cannot_be_reassigned() {
    let parse_error = compile_source("const value", None).unwrap_err();
    assert_error(&parse_error, "const declaration must have an initializer");

    let missing_value = compile_source("const first, second = 1", None).unwrap_err();
    assert_error(
        &missing_value,
        "every const binding must have an initializer",
    );

    let reassigned = compile_source("const value = 1\nvalue = 2", None).unwrap_err();
    assert_error(&reassigned, "cannot assign to const binding 'value'");

    let captured = compile_source(
        r#"
const value = 1
local callback = function()
    value = 2
end
"#,
        None,
    )
    .unwrap_err();
    assert_error(&captured, "cannot assign to const binding 'value'");
}

#[test]
fn final_call_method_call_or_vararg_can_initialize_multiple_const_bindings() {
    let output = compile_source(
        r#"
const first, second = getValues()
const third, fourth = source:getValues()
local callback = function(...)
    const fifth, sixth = ...
    return fifth, sixth
end
"#,
        None,
    )
    .unwrap();

    assert!(output.contains("const first, second = getValues()"));
    assert!(output.contains("const third, fourth = source:getValues()"));
    assert!(output.contains("const fifth, sixth = ..."));
}

#[test]
fn const_value_mutation_and_inner_scope_shadowing_are_allowed() {
    let output = compile_source(
        r#"
const value = { count = 1 }
value.count = 2
do
    const value = { count = 3 }
    value.count = 4
end
"#,
        None,
    )
    .unwrap();

    assert!(output.contains("value.count = 2"));
    assert!(output.contains("value.count = 4"));
}

#[test]
fn const_redeclaration_in_the_same_scope_is_rejected() {
    let errors = compile_source("const value = 1\nlocal value = 2", None).unwrap_err();
    assert_error(&errors, "binding 'value' is already declared in this scope");

    let errors = compile_source("local value = 1\nconst value = 2", None).unwrap_err();
    assert_error(&errors, "binding 'value' is already declared in this scope");
}

#[test]
fn const_binding_shadows_a_module_member_before_rewrite() {
    let project = TempProject::new();
    project.definition("module", "declare value: number");

    let output = compile_at(
        "import module\nconst value = 10\nprint(value)",
        &project.source_path(),
    )
    .unwrap();
    assert!(output.contains("print(value)"));
    assert!(!output.contains("module.value"));
}

use luar_rs::checker::Checker;
use luar_rs::codegen::Codegen;
use luar_rs::parser::Parser;

fn parse(source: &str) -> luar_rs::ast::Program {
    Parser::new(source)
        .expect("lexer should accept test source")
        .parse()
        .expect("parser should accept test source")
}

fn check(source: &str) -> Vec<String> {
    let mut program = parse(source);
    Checker::new()
        .check(&mut program)
        .into_iter()
        .map(|error| error.message)
        .collect()
}

fn compile(source: &str) -> Result<String, Vec<String>> {
    let mut program = parse(source);
    let errors = Checker::new().check(&mut program);
    if errors.is_empty() {
        Ok(Codegen::new().generate(&program))
    } else {
        Err(errors.into_iter().map(|error| error.message).collect())
    }
}

fn assert_error(source: &str, expected: &str) {
    let errors = check(source);
    assert!(
        errors.iter().any(|error| error.contains(expected)),
        "expected an error containing {expected:?}, got {errors:#?}"
    );
}

#[test]
fn lpl_class_example_generates_oop_runtime_shape() {
    let output = compile(
        r#"
class Lua is
    private is
        Year = 0
        function explode()
            print("Boom!!")
        end
    end

    public is
        static function new(year: number)
            self.Year = year
        end

        static function panic()
            print("panic")
        end

        function greet()
            self.explode()
            print(`Hello from {self.Year}!`)
        end

        function operator==(other: Lua)
            return false
        end

        function free()
            print("Goodbye")
        end
    end
end

local value = Lua.new(1993)
value.greet()
value.free()
Lua.panic()
"#,
    )
    .expect("LPL class example should compile");

    assert!(output.contains("local Lua = {}"));
    assert!(output.contains("local function explode(self)"));
    assert!(output.contains("explode(self)"));
    assert!(output.contains("Lua.__eq = function(self, other)"));
    assert!(output.contains("value:greet()"));
    assert!(output.contains("value:free()"));
    assert!(output.contains("Lua.panic()"));
}

#[test]
fn private_constructor_can_be_called_by_a_public_factory() {
    let output = compile(
        r#"
class Secret is
    static function new(value: number)
        self.value = value
    end

    public is
        static function create(value: number)
            return Secret.new(value)
        end
    end
end

local secret = Secret.create(42)
"#,
    )
    .expect("private constructor should be callable inside its owner");

    assert!(output.contains("local function new(value)"));
    assert!(output.contains("return new(value)"));
    assert!(!output.contains("function Secret.new"));
}

#[test]
fn inheritance_abstract_override_final_and_super_compile() {
    let output = compile(
        r#"
class Language is abstract
    year = 1901
    public is
        function explain(): string abstract
    end
end

class Binary is Language
    year = 1930
    public is
        function explain(): string override
            return "binary"
        end
    end
end

class ASM is Binary
    public is
        function explain(): string override
            return "assembly"
        end

        function assemble() final
            print("mov")
        end
    end
end

class C is ASM
    public is
        function explain(): string override
            super.explain()
            return "C"
        end
    end
end
"#,
    )
    .expect("LPL inheritance example should compile");

    assert!(output.contains("local Binary = setmetatable({}, { __index = Language })"));
    assert!(output.contains("local C = setmetatable({}, { __index = ASM })"));
    assert!(output.contains("ASM.explain(self)"));
    assert!(!output.contains("function Language.explain"));
}

#[test]
fn inherited_constructor_initializes_child_fields() {
    let output = compile(
        r#"
class Base is
    public is
        static function new(value: number)
            self.value = value
        end
    end
end

class Child is Base
    label = "child"
end

local child = Child.new(7)
"#,
    )
    .expect("child should inherit its parent's constructor");

    assert!(output.contains("function Child.new(value)"));
    assert!(output.contains("local self = Base.new(value)"));
    assert!(output.contains("setmetatable(self, Child)"));
    assert!(output.contains("self.label = \"child\""));
}

#[test]
fn concrete_class_must_implement_inherited_abstract_method() {
    assert_error(
        r#"
class Base is abstract
    public is
        function run(value: number): string abstract
    end
end

class Child is Base
end
"#,
        "must be abstract or implement abstract method 'run'",
    );
}

#[test]
fn override_signature_requires_matching_annotations_and_varargs() {
    assert_error(
        r#"
class Base is
    public is
        function run(value: number, ...): string
            return "base"
        end
    end
end

class Child is Base
    public is
        function run(value, ...): string override
            return "child"
        end
    end
end
"#,
        "does not exactly match the parent signature",
    );

    assert_error(
        r#"
class Base is
    public is
        function run(): string
            return "base"
        end
    end
end

class Child is Base
    public is
        function run() override
        end
    end
end
"#,
        "does not exactly match the parent signature",
    );
}

#[test]
fn inheritance_and_member_declaration_errors_are_reported() {
    assert_error(
        "class Child is Missing\nend",
        "unknown parent class 'Missing'",
    );
    assert_error(
        "class A is B\nend\nclass B is A\nend",
        "circular inheritance",
    );
    assert_error(
        r#"
class Duplicate is
    value = 1
    value = 2
end
"#,
        "defined more than once",
    );
    assert_error(
        r#"
class Base is
    public is
        function run()
        end
    end
end
class Child is Base
    public is
        function run()
        end
    end
end
"#,
        "missing 'override' keyword",
    );
    assert_error(
        r#"
class Base is
    public is
        function run() final
        end
    end
end
class Child is Base
    public is
        function run() override
        end
    end
end
"#,
        "cannot override final",
    );
}

#[test]
fn private_access_including_inherited_members_is_rejected() {
    assert_error(
        r#"
class Base is
    secret = 1
    public is
        static function new()
        end
    end
end

class Child is Base
    public is
        function reveal()
            return self.secret
        end
    end
end
"#,
        "cannot access private field 'secret'",
    );

    assert_error(
        r#"
class Value is
    hidden = 1
    public is
        static function new()
        end
    end
end
local value = Value.new()
print(value.hidden)
"#,
        "cannot access private field 'hidden'",
    );
}

#[test]
fn static_instance_and_reserved_method_rules_are_enforced() {
    assert_error(
        r#"
class Value is
    public is
        function new()
        end
    end
end
"#,
        "constructor 'new' must be static",
    );
    assert_error(
        r#"
class Value is
    public is
        static function free()
        end
    end
end
"#,
        "destructor 'free' cannot be static",
    );
    assert_error(
        r#"
class Value is
    public is
        static function new()
        end
        static function utility()
        end
        function instance()
        end
    end
end
local value = Value.new()
value.utility()
"#,
        "static method 'utility' cannot be called on an instance",
    );
    assert_error(
        r#"
class Value is
    public is
        function instance()
        end
    end
end
Value.instance()
"#,
        "instance method 'instance' cannot be called on class",
    );
}

#[test]
fn abstract_instantiation_and_invalid_super_calls_are_rejected() {
    assert_error(
        r#"
class Base is abstract
    public is
        function run() abstract
    end
end
local value = Base.new()
"#,
        "cannot instantiate abstract class 'Base'",
    );
    assert_error(
        r#"
class NoParent is
    public is
        function run()
            super.run()
        end
    end
end
"#,
        "has no parent for 'super'",
    );
    assert_error(
        r#"
class Base is
    public is
        function run()
        end
    end
end
class Child is Base
    public is
        function run() override
            super.missing()
        end
    end
end
"#,
        "does not exist on direct parent class",
    );
}

#[test]
fn operator_rules_and_supported_operator_set_are_enforced() {
    assert_error(
        r#"
class Value is
    public is
        static function operator==(other: Value)
            return false
        end
    end
end
"#,
        "cannot be static",
    );
    assert_error(
        r#"
class Value is
    function operator==(other: Value)
        return false
    end
end
"#,
        "must be public",
    );

    let parse_error = Parser::new(
        r#"
class Value is
    public is
        function operator~=(other: Value)
            return false
        end
    end
end
"#,
    )
    .expect("lexer should accept ~=")
    .parse()
    .expect_err("~= is not a supported overloaded operator");
    assert!(parse_error.0.contains("unsupported overloaded operator"));
}

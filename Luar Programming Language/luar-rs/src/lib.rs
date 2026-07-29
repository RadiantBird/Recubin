pub mod ast;
pub mod checker;
pub mod codegen;
pub mod lexer;
pub mod modules;
pub mod parser;
pub mod resolver;

use std::cell::RefCell;
use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int};
use std::path::Path;
use std::ptr;

// Thread-local error buffer for luar_get_errors
thread_local! {
    static LAST_ERRORS: RefCell<String> = RefCell::new(String::new());
}

/// Compile Luar source to Luau source.
///
/// Returns 0 on success, -1 on error.
/// On success, `out_buf` is filled with the null-terminated Luau source.
/// On error, call `luar_get_errors` to retrieve error messages.
#[unsafe(no_mangle)]
pub extern "C" fn luar_compile(src: *const c_char, out_buf: *mut c_char, out_len: usize) -> c_int {
    if src.is_null() || out_buf.is_null() || out_len == 0 {
        return -1;
    }

    let source = unsafe {
        match CStr::from_ptr(src).to_str() {
            Ok(s) => s.to_string(),
            Err(_) => {
                store_error("invalid UTF-8 in source");
                return -1;
            }
        }
    };

    let output = match compile_source(&source, None) {
        Ok(output) => output,
        Err(errors) => {
            store_error(&errors.join("\n"));
            return -1;
        }
    };

    write_output(output, out_buf, out_len)
}

/// Compile Luar source with its file path available for adjacent `.luard` lookup.
///
/// The argument order is kept C-friendly and matches `luar_compile`, with
/// `source_path` inserted immediately after `src`.
#[unsafe(no_mangle)]
pub extern "C" fn luar_compile_with_path(
    src: *const c_char,
    source_path: *const c_char,
    out_buf: *mut c_char,
    out_len: usize,
) -> c_int {
    if src.is_null() || source_path.is_null() || out_buf.is_null() || out_len == 0 {
        return -1;
    }

    let source = unsafe {
        match CStr::from_ptr(src).to_str() {
            Ok(source) => source,
            Err(_) => {
                store_error("invalid UTF-8 in source");
                return -1;
            }
        }
    };
    let source_path = unsafe {
        match CStr::from_ptr(source_path).to_str() {
            Ok(path) => path,
            Err(_) => {
                store_error("invalid UTF-8 in source path");
                return -1;
            }
        }
    };
    let output = match compile_source(source, Some(Path::new(source_path))) {
        Ok(output) => output,
        Err(errors) => {
            store_error(&errors.join("\n"));
            return -1;
        }
    };

    write_output(output, out_buf, out_len)
}

pub fn compile_source(source: &str, source_path: Option<&Path>) -> Result<String, Vec<String>> {
    let mut parser = parser::Parser::new(source).map_err(|error| vec![error.0])?;
    let mut program = parser.parse().map_err(|error| vec![error.0])?;

    let mut imports = Vec::new();
    let mut seen_imports = std::collections::HashSet::new();
    let mut errors = Vec::new();
    for stmt in &program.stmts {
        if let ast::Stmt::ImportDecl { module_name } = stmt {
            if seen_imports.insert(module_name.clone()) {
                imports.push(module_name.clone());
            } else {
                errors.push(format!(
                    "[0] module '{module_name}' is imported more than once"
                ));
            }
        }
    }

    let mut definitions = Vec::new();
    if !imports.is_empty() {
        let Some(source_path) = source_path else {
            errors.push(
                "[0] import declarations require luar_compile_with_path and a source file path"
                    .to_string(),
            );
            return Err(errors);
        };
        if source_path.as_os_str().is_empty() {
            errors.push("[0] import declarations require a non-empty source file path".to_string());
            return Err(errors);
        }
        for module_name in imports {
            match modules::load_definition(&module_name, source_path) {
                Ok(definition) => definitions.push(definition),
                Err(error) => errors.push(format!("[{}] {}", error.line, error.message)),
            }
        }
    }

    let resolver_errors = resolver::Resolver::new(definitions).resolve(&mut program);
    errors.extend(
        resolver_errors
            .into_iter()
            .map(|error| format!("[{}] {}", error.line, error.message)),
    );
    let checker_errors = checker::Checker::new().check(&mut program);
    errors.extend(
        checker_errors
            .into_iter()
            .map(|error| format!("[{}] {}", error.line, error.message)),
    );
    if !errors.is_empty() {
        return Err(errors);
    }

    Ok(codegen::Codegen::new().generate(&program))
}

fn write_output(output: String, out_buf: *mut c_char, out_len: usize) -> c_int {
    let c_output = match CString::new(output) {
        Ok(s) => s,
        Err(_) => {
            store_error("codegen output contains null byte");
            return -1;
        }
    };
    let bytes = c_output.as_bytes_with_nul();
    if bytes.len() > out_len {
        store_error("output buffer too small");
        return -1;
    }

    unsafe {
        ptr::copy_nonoverlapping(bytes.as_ptr(), out_buf as *mut u8, bytes.len());
    }
    0
}

/// Retrieve the last error message(s) into `buf`.
/// Returns the number of bytes written (excluding null terminator), or -1 on failure.
#[unsafe(no_mangle)]
pub extern "C" fn luar_get_errors(buf: *mut c_char, buf_len: usize) -> c_int {
    if buf.is_null() || buf_len == 0 {
        return -1;
    }
    LAST_ERRORS.with(|e| {
        let s = e.borrow();
        let bytes = s.as_bytes();
        let n = bytes.len().min(buf_len - 1);
        unsafe {
            ptr::copy_nonoverlapping(bytes.as_ptr(), buf as *mut u8, n);
            *buf.add(n) = 0;
        }
        n as c_int
    })
}

fn store_error(msg: &str) {
    LAST_ERRORS.with(|e| *e.borrow_mut() = msg.to_string());
}

pub mod ast;
pub mod checker;
pub mod codegen;
pub mod lexer;
pub mod parser;

use std::cell::RefCell;
use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int};
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

    let mut p = match parser::Parser::new(&source) {
        Ok(p) => p,
        Err(e) => {
            store_error(&e.0);
            return -1;
        }
    };

    let mut program = match p.parse() {
        Ok(prog) => prog,
        Err(e) => {
            store_error(&e.0);
            return -1;
        }
    };

    let mut chk = checker::Checker::new();
    let errors = chk.check(&mut program);
    if !errors.is_empty() {
        let msgs: Vec<_> = errors
            .iter()
            .map(|e| format!("[{}] {}", e.line, e.message))
            .collect();
        store_error(&msgs.join("\n"));
        return -1;
    }

    let mut generator = codegen::Codegen::new();
    let output = generator.generate(&program);

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

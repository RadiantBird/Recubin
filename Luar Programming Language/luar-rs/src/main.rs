use std::env;
use std::ffi::OsStr;
use std::fs;
use std::io::{self, Write};
use std::path::Path;
use std::process::ExitCode;

const HELP: &str = "luar - Luar to Luau transpiler v0.1.0

Usage:
  luar compile <input.luar> [output.luau]   Compile Luar source to Luau
  luar check   <input.luar>                 Type-check without emitting output
  luar help                                 Show this help

Examples:
  luar compile hello.luar               # prints Luau to stdout
  luar compile hello.luar hello.luau    # writes hello.luau
  luar check   hello.luar               # exits 0 on success, 1 on error";

fn main() -> ExitCode {
    match run() {
        Ok(()) => ExitCode::SUCCESS,
        Err(()) => ExitCode::FAILURE,
    }
}

fn run() -> Result<(), ()> {
    let mut args = env::args_os().skip(1);
    let Some(command) = args.next() else {
        println!("{HELP}");
        return Ok(());
    };

    if command == OsStr::new("help")
        || command == OsStr::new("--help")
        || command == OsStr::new("-h")
    {
        println!("{HELP}");
        return Ok(());
    }

    let is_compile = command == OsStr::new("compile");
    let is_check = command == OsStr::new("check");
    if !is_compile && !is_check {
        eprintln!("luar: unknown command '{}'", command.to_string_lossy());
        eprintln!("Run 'luar help' for usage.");
        return Err(());
    }

    let Some(input) = args.next() else {
        eprintln!("luar {}: missing input file", command.to_string_lossy());
        eprintln!("Run 'luar help' for usage.");
        return Err(());
    };
    let input = Path::new(&input);

    let source = fs::read_to_string(input).map_err(|_| {
        eprintln!("luar: cannot read '{}'", input.display());
    })?;

    let output = luar_rs::compile_source(&source, Some(input)).map_err(|errors| {
        for error in errors {
            eprintln!("{}: error: {error}", input.display());
        }
    })?;

    if is_check {
        println!("{}: ok", input.display());
        return Ok(());
    }

    if let Some(output_path) = args.next() {
        let output_path = Path::new(&output_path);
        fs::write(output_path, output).map_err(|_| {
            eprintln!("luar: cannot write '{}'", output_path.display());
        })?;
        println!("wrote {}", output_path.display());
    } else {
        io::stdout().write_all(output.as_bytes()).map_err(|_| {
            eprintln!("luar: cannot write to stdout");
        })?;
    }

    Ok(())
}

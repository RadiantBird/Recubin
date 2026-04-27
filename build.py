import shutil
import subprocess
import sys
from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parent
BUILD_DIR = ROOT_DIR / "build"
DLL_DIR = ROOT_DIR / "dlls"


def run_command(args: list[str]) -> int:
    return subprocess.call(args, cwd=ROOT_DIR)


def normalize_config(value: str | None) -> str:
    if value is None:
        return "Release"

    lowered = value.lower()
    if lowered in ("debug", "d"):
        return "Debug"
    if lowered in ("release", "r"):
        return "Release"
    raise ValueError(f"Unknown configuration: {value}")


def copy_dlls(config: str) -> None:
    if not DLL_DIR.exists():
        print("[WARNING] dlls folder missing.")
        return

    target_dir = BUILD_DIR / config
    target_dir.mkdir(parents=True, exist_ok=True)

    copied = 0
    for dll_path in DLL_DIR.glob("*.dll"):
        shutil.copy2(dll_path, target_dir / dll_path.name)
        copied += 1

    if copied > 0:
        print(f"[SUCCESS] Copied {copied} DLL file(s) to {target_dir}.")
    else:
        print("[WARNING] No DLL files found in dlls folder.")


def build(config: str) -> int:
    BUILD_DIR.mkdir(exist_ok=True)

    print(f"[INFO] Configuring {config} build...")
    result = run_command(["cmake", "-S", ".", "-B", "build", "-A", "x64", "-D", "GLEW_STATIC=ON"])
    if result != 0:
        print("[ERROR] CMake configuration failed.")
        return result

    print(f"[INFO] Building {config}...")
    result = run_command(["cmake", "--build", "build", "--config", config, "--parallel"])
    if result != 0:
        print("[ERROR] Build execution failed.")
        return result

    copy_dlls(config)
    print("[SUCCESS] Build process completed.")
    return 0


def run_binary(config: str) -> int:
    exe_path = BUILD_DIR / config / "Recubin.exe"
    if not exe_path.exists():
        print(f"[ERROR] Executable not found: {exe_path}")
        return 1

    copy_dlls(config)
    return subprocess.call([str(exe_path)], cwd=ROOT_DIR)


def run_watchsnake(exit_code: int) -> int:
    watchsnake_path = ROOT_DIR / "watchSnake.py"
    if not watchsnake_path.exists():
        return exit_code

    python_cmd = shutil.which("py") or shutil.which("python") or shutil.which("python3")
    if not python_cmd:
        print("[WARNING] Python launcher not found. Skipping watchSnake.py.")
        return exit_code

    return subprocess.call([python_cmd, str(watchsnake_path), str(exit_code)], cwd=ROOT_DIR)


def main() -> int:
    if len(sys.argv) < 2:
        print("Usage: <python> build.py <build|run|brun> [Debug|Release]")
        return 1

    action = sys.argv[1].lower()

    try:
        config = normalize_config(sys.argv[2] if len(sys.argv) >= 3 else None)
    except ValueError as exc:
        print(f"[ERROR] {exc}")
        return 1

    if action == "build":
        return build(config)

    if action == "run":
        exit_code = run_binary(config)
        return run_watchsnake(exit_code)

    if action == "brun":
        result = build(config)
        if result != 0:
            return result
        exit_code = run_binary(config)
        return run_watchsnake(exit_code)

    print(f"[ERROR] Unknown action: {action}")
    return 1


if __name__ == "__main__":
    sys.exit(main())

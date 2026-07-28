import datetime
import platform
import shutil
import subprocess
import sys
from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parent
BUILD_DIR = ROOT_DIR / "build"
DLL_DIR = ROOT_DIR / "dlls"
DIST_DIR = ROOT_DIR / "dist"
VC_REDIST_PATH = ROOT_DIR / "redist" / "vc_redist.x64.exe"


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

    target_dir = BUILD_DIR / config / "dlls"
    target_dir.mkdir(parents=True, exist_ok=True)

    copied = 0
    for dll_path in DLL_DIR.glob("*.dll"):
        legacy_dll_path = BUILD_DIR / config / dll_path.name
        if legacy_dll_path.exists():
            legacy_dll_path.unlink()
        shutil.copy2(dll_path, target_dir / dll_path.name)
        copied += 1

    if copied > 0:
        print(f"[SUCCESS] Copied {copied} DLL file(s) to {target_dir}.")
    else:
        print("[WARNING] No DLL files found in dlls folder.")


def build(config: str) -> int:
    BUILD_DIR.mkdir(exist_ok=True)

    is_windows = platform.system() == "Windows"

    print(f"[INFO] Configuring {config} build...")
    configure_args = ["cmake", "-S", ".", "-B", "build"]
    if is_windows:
        configure_args += ["-A", "x64", "-D", "GLEW_STATIC=ON"]
    result = run_command(configure_args)
    if result != 0:
        print("[ERROR] CMake configuration failed.")
        return result

    print(f"[INFO] Building {config}...")
    result = run_command(["cmake", "--build", "build", "--config", config, "--parallel"])
    if result != 0:
        print("[ERROR] Build execution failed.")
        return result

    if is_windows:
        copy_dlls(config)

        # ランチャーも自動ビルド（失敗してもメインビルドは成功扱い）
        try:
            launcher_result = build_launcher(config)
            if launcher_result != 0:
                print("[WARNING] launcher.exe build failed — packaging will skip it.")
        except FileNotFoundError:
            print("[WARNING] cl.exe not found - skipping launcher build. Run from Developer Command Prompt to build it.")
    else:
        print("[INFO] Non-Windows platform detected - skipping DLL copy and launcher build (Mac版ランチャーは未対応).")

    print("[SUCCESS] Build process completed.")
    return 0


def run_binary(config: str) -> int:
    exe_path = BUILD_DIR / config / "Recubin.exe"
    if not exe_path.exists():
        print(f"[ERROR] Executable not found: {exe_path}")
        return 1

    copy_dlls(config)
    return subprocess.call([str(exe_path)], cwd=ROOT_DIR)


def run_test(config: str, scene_path: str | None) -> int:
    exe_path = BUILD_DIR / config / "RecubinTest.exe"
    if not exe_path.exists():
        print(f"[ERROR] Executable not found: {exe_path}")
        return 1

    copy_dlls(config)
    args = [str(exe_path)]
    if scene_path:
        args.append(scene_path)
    return subprocess.call(args, cwd=ROOT_DIR)


def run_watchsnake(exit_code: int) -> int:
    watchsnake_path = ROOT_DIR / "watchSnake.py"
    if not watchsnake_path.exists():
        return exit_code

    python_cmd = shutil.which("py") or shutil.which("python") or shutil.which("python3")
    if not python_cmd:
        print("[WARNING] Python launcher not found. Skipping watchSnake.py.")
        return exit_code

    return subprocess.call([python_cmd, str(watchsnake_path), str(exit_code)], cwd=ROOT_DIR)


def build_launcher(config: str) -> int:
    src = ROOT_DIR / "launcher" / "main.cpp"
    if not src.exists():
        print(f"[ERROR] launcher/main.cpp not found.")
        return 1

    out_dir = BUILD_DIR / config
    out_dir.mkdir(parents=True, exist_ok=True)
    out_exe = out_dir / "launcher.exe"

    # Compile with MSVC cl.exe (assumes Developer Command Prompt or vcvars in PATH)
    args = [
        "cl.exe",
        "/std:c++17",
        "/EHsc",
        "/W3",
        "/utf-8",
        "/Zi",
        f"/Fe{out_exe}",
        str(src),
        "Shell32.lib", "Ole32.lib", "user32.lib",
        "/link", "/SUBSYSTEM:WINDOWS",
        "/DEBUG"
    ]
    result = subprocess.call(args, cwd=ROOT_DIR)
    if result == 0:
        print(f"[SUCCESS] launcher.exe built at {out_exe}")
    else:
        print("[ERROR] Launcher build failed.")
    return result


def package_editor(config: str) -> int:
    result = build(config)
    if result != 0:
        return result

    if not VC_REDIST_PATH.exists():
        print(f"[ERROR] Visual C++ Redistributable installer not found: {VC_REDIST_PATH}")
        print("[ERROR] Download the official vc_redist.x64.exe and place it in redist before packaging.")
        return 1

    build_dir = BUILD_DIR / config
    pkg_dir = DIST_DIR / "RecubinStudio"
    if pkg_dir.exists():
        shutil.rmtree(pkg_dir)
    pkg_dir.mkdir(parents=True)

    def copy_executable(src: Path, dst: Path) -> bool:
        if not src.exists():
            print(f"[ERROR] Executable not found: {src}")
            return False
        shutil.copy2(src, dst)
        return True
    
    # exe コピー
    recubin_exe = build_dir / "Recubin.exe"
    engine_exe = build_dir / "RecubinEngine.exe"
    launcher_exe = build_dir / "launcher.exe"
    watcher_exe = build_dir / "Watcher.exe"

    if not copy_executable(recubin_exe, pkg_dir / "Recubin.exe"):
        print("[ERROR] Recubin.exe not found - in-editor game Packager will not work!")
        return 1

    if not copy_executable(engine_exe, pkg_dir / "RecubinEngine.exe"):
        print("[ERROR] RecubinEngine.exe not found - in-editor game Packager will not work!")
        return 1

    if not copy_executable(launcher_exe, pkg_dir / "launcher.exe"):
        print("[ERROR] launcher.exe not found")
        return 1

    if not copy_executable(watcher_exe, pkg_dir / "Watcher.exe"):
        print("[ERROR] Watcher.exe not found")
        return 1

    # DLL コピー
    dll_dir = pkg_dir / "dlls"
    dll_dir.mkdir(parents=True, exist_ok=True)

    dll_copied = 0
    for dll_path in DLL_DIR.glob("*.dll"):
        shutil.copy2(dll_path, dll_dir / dll_path.name)
        dll_copied += 1
    if dll_copied == 0:
        print("[WARNING] No DLL files found in dlls folder.")

    redist_dir = pkg_dir / "redist"
    redist_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(VC_REDIST_PATH, redist_dir / VC_REDIST_PATH.name)

    # シェーダーコピー
    shaders_src = ROOT_DIR / "shaders"
    shaders_dst = pkg_dir / "shaders"
    shaders_dst.mkdir(parents=True, exist_ok=True)
    shader_copied = 0
    for shader_path in shaders_src.glob("*.glsl"):
        shutil.copy2(shader_path, shaders_dst / shader_path.name)
        shader_copied += 1
    if shader_copied == 0:
        print("[ERROR] No shader files found in shaders folder.")
        return 1

    # フォントコピー
    fonts_src = ROOT_DIR / "assets" / "fonts"
    fonts_dst = pkg_dir / "assets" / "fonts"
    if fonts_src.exists():
        shutil.copytree(fonts_src, fonts_dst)
    else:
        print("[WARNING] assets/fonts folder missing - skipping font copy.")

    # 空ディレクトリ作成
    for dir_name in ("scenes", "image", "models", "scripts"):
        (pkg_dir / "assets" / dir_name).mkdir(parents=True, exist_ok=True)

    # imgui.ini コピー
    imgui_ini = ROOT_DIR / "imgui.ini"
    if imgui_ini.exists():
        shutil.copy2(imgui_ini, pkg_dir / "imgui.ini")
    else:
        print("[WARNING] imgui.ini not found - skipping.")

    with open(pkg_dir / "readme.txt", "w", encoding="utf-8") as f:
        f.write("""Recubin.exeがエディター(スタジオ)なので、それをクリックすれば始められます。
RecubinEngine.exeはランタイム用なので触らずにそのままにしておいてください。
                
Recubin.exe is the editor (Studio), so you can start by clicking on it.
Recubin.exeが起動しない場合は、先にredist/vc_redist.x64.exeを実行してからRecubin.exeを起動してください。

RecubinEngine.exe is for the runtime, so please leave it as is and do not touch it.
If Recubin.exe does not start, run redist/vc_redist.x64.exe before launching Recubin.exe.
""")
        f.flush()

    # zip 生成
    date_str = datetime.date.today().strftime("%Y%m%d")
    archive_base = DIST_DIR / f"RecubinStudio-{date_str}"
    zip_path = shutil.make_archive(str(archive_base), "zip", root_dir=DIST_DIR, base_dir="RecubinStudio")

    print(f"[SUCCESS] Packaged studio at {pkg_dir}")
    print(f"[SUCCESS] Created archive at {zip_path}")
    return 0


def main() -> int:
    if len(sys.argv) < 2:
        print("Usage: <python> build.py <build|run|brun|test|launcher|package> [Debug|Release]")
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

    if action == "test":
        scene_path = sys.argv[3] if len(sys.argv) >= 4 else None
        return run_test(config, scene_path)

    if action == "launcher":
        return build_launcher(config)

    if action == "package":
        return package_editor(config)

    print(f"[ERROR] Unknown action: {action}")
    return 1


if __name__ == "__main__":
    sys.exit(main())

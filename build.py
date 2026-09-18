
import filecmp
import os
import platform
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path
from typing import Any


IS_WINDOWS = platform.system() == "Windows"
IS_MACOS = platform.system() == "Darwin"
if IS_WINDOWS:
    import psutil as psutil_module
else:
    # psutil is only used on Windows; keep the platform-specific module out of
    # the non-Windows runtime while giving static analyzers a known symbol.
    psutil_module: Any = None


ROOT_DIR = Path(__file__).resolve().parent
BUILD_DIR = ROOT_DIR / ("build" if IS_WINDOWS else "build-mac")
DLL_DIR = ROOT_DIR / "dlls"
DIST_DIR = ROOT_DIR / "dist"
TESTCASES_DIR = ROOT_DIR / "TestCases"
VC_REDIST_PATH = ROOT_DIR / "redist" / "vc_redist.x64.exe"


def run_command(args: list[str]) -> int:
    return subprocess.call(args, cwd=ROOT_DIR)


def find_cmake_executable() -> str:
    """Resolve CMake without requiring the interactive shell PATH to be configured."""
    path_cmake = shutil.which("cmake")
    if path_cmake:
        return path_cmake

    if IS_WINDOWS:
        program_files = Path(os.environ.get("ProgramFiles", r"C:\Program Files"))
        local_app_data = Path(os.environ.get("LOCALAPPDATA", ""))
        candidates = [
            program_files / "CMake" / "bin" / "cmake.exe",
            local_app_data / "Programs" / "CMake" / "bin" / "cmake.exe",
        ]
        visual_studio_root = program_files / "Microsoft Visual Studio"
        if visual_studio_root.is_dir():
            candidates.extend(sorted(
                visual_studio_root.glob(
                    "*/*/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
                ),
                reverse=True,
            ))

        for candidate in candidates:
            if candidate.is_file():
                print(f"[INFO] CMake was not on PATH; using {candidate}.")
                return str(candidate)

    raise FileNotFoundError(
        "CMake was not found on PATH or in the supported Windows installation locations. "
        "Install CMake or Visual Studio's CMake component."
    )


def files_have_same_content(source: Path, destination: Path) -> bool:
    try:
        if not destination.is_file():
            return False
        if source.stat().st_size != destination.stat().st_size:
            return False
        return filecmp.cmp(source, destination, shallow=False)
    except FileNotFoundError:
        return False


def copy_if_different(source: Path, destination: Path) -> bool:
    if files_have_same_content(source, destination):
        return False
    shutil.copy2(source, destination)
    return True


def prepare_studio_package_directory(pkg_dir: Path) -> bool:
    """Refresh generated Studio files while preserving local autosave data."""
    autosave_dir = pkg_dir / ".autosave"
    if autosave_dir.exists() and not autosave_dir.is_dir():
        print(f"[ERROR] Autosave path is not a directory: {autosave_dir}")
        return False
    if pkg_dir.exists():
        for child in pkg_dir.iterdir():
            if child.name == ".autosave":
                continue
            if child.is_dir() and not child.is_symlink():
                shutil.rmtree(child)
            else:
                child.unlink()
    else:
        pkg_dir.mkdir(parents=True)
    autosave_dir.mkdir(parents=True, exist_ok=True)
    return True


def create_studio_archive(pkg_dir: Path, archive_base: Path) -> Path:
    """Archive Studio without leaking local autosaves, but keep its empty marker."""
    zip_path = archive_base.with_suffix(".zip")
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        package_name = pkg_dir.name
        archive.writestr(f"{package_name}/.autosave/", b"")
        for path in sorted(pkg_dir.rglob("*")):
            relative = path.relative_to(pkg_dir)
            if relative.parts and relative.parts[0] == ".autosave":
                continue
            archive_name = Path(package_name) / relative
            if path.is_dir():
                archive.writestr(archive_name.as_posix().rstrip("/") + "/", b"")
            else:
                archive.write(path, archive_name.as_posix())
    return zip_path


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
    if not IS_WINDOWS:
        return

    if not DLL_DIR.exists():
        print("[WARNING] dlls folder missing.")
        return

    target_dir = BUILD_DIR / config / "dlls"
    target_dir.mkdir(parents=True, exist_ok=True)

    copied = 0
    skipped = 0
    for dll_path in DLL_DIR.glob("*.dll"):
        legacy_dll_path = BUILD_DIR / config / dll_path.name
        if legacy_dll_path.exists():
            legacy_dll_path.unlink()
        if copy_if_different(dll_path, target_dir / dll_path.name):
            copied += 1
        else:
            skipped += 1

    if copied + skipped > 0:
        print(
            f"[SUCCESS] DLL sync completed for {target_dir}: "
            f"copied {copied}, skipped {skipped} unchanged file(s)."
        )
    else:
        print("[WARNING] No DLL files found in dlls folder.")


def sync_network_test_engine(config: str) -> int:
    if IS_WINDOWS:
        source = BUILD_DIR / config / "RecubinEngine.exe"
        destination = ROOT_DIR / "TestCases" / "NetworkTest" / "RecubinEngine.exe"
    elif platform.system() == "Darwin" and platform.machine().lower() in ("arm64", "aarch64"):
        source = BUILD_DIR / "RecubinEngine"
        destination = ROOT_DIR / "TestCases" / "NetworkTest" / "RecubinEngine-macos-arm64"
    else:
        return 0

    if not source.is_file():
        print(f"[ERROR] NetworkTest engine source not found: {source}")
        return 1

    try:
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
    except OSError as exc:
        print(f"[ERROR] Failed to sync NetworkTest engine: {exc}")
        return 1

    print(f"[SUCCESS] Synced NetworkTest engine to {destination}.")
    return 0


def build_outputs_are_up_to_date(config: str) -> bool:
    if IS_WINDOWS:
        output_paths = [
            BUILD_DIR / config / "Recubin.exe",
            BUILD_DIR / config / "RecubinEngine.exe",
            BUILD_DIR / config / "RecubinTest.exe",
        ]
    else:
        output_paths = [
            BUILD_DIR / "Recubin",
            BUILD_DIR / "RecubinEngine",
            BUILD_DIR / "RecubinTest",
        ]

    if not all(path.is_file() for path in output_paths):
        return False

    oldest_output = min(path.stat().st_mtime_ns for path in output_paths)
    input_suffixes = {
        ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx",
        ".inl", ".cmake", ".lib", ".a",
    }
    input_names = {"CMakeLists.txt", "CMakePresets.json"}
    ignored_parts = {".git", ".claude", ".codex", "build", "build-mac", "dist"}
    input_paths = [ROOT_DIR / name for name in ("CMakeLists.txt", "CMakePresets.json")]
    input_roots = [ROOT_DIR / name for name in ("src", "include", "temp_libs", "cmake")]
    candidate_paths = [path for path in input_paths if path.is_file()]
    for input_root in input_roots:
        if input_root.is_dir():
            candidate_paths.extend(
                path for path in input_root.rglob("*")
                if path.is_file() and not any(part in ignored_parts for part in path.parts)
            )

    for path in candidate_paths:
        if path.name not in input_names and path.suffix.lower() not in input_suffixes:
            continue
        try:
            if path.stat().st_mtime_ns > oldest_output:
                return False
        except OSError:
            return False

    return True


def build_executable_targets_in_parallel(config: str, cmake_executable: str) -> int:
    targets = (
        "Recubin",
        "RecubinEngine",
        "RecubinTest",
    )

    # @RadiantBird 2026/09/14:
    # Run all executable targets through one CMake/MSBuild invocation.
    # Multiple concurrent `cmake --build` processes can race on shared
    # dependencies such as Recast and contend for MSBuild .tlog files
    # (for example Recast.lastbuildstate). Let the build system see the
    # complete dependency graph and parallelize safely inside one process.
    args = [
        cmake_executable,
        "--build",
        str(BUILD_DIR),
        "--config",
        config,
        "--parallel",
    ]

    if not IS_WINDOWS:
        args.append("4")

    args += [
        "--target",
        *targets,
    ]

    print(
        "[INFO] Building executable targets in one build graph: "
        + ", ".join(targets)
    )

    result = run_command(args)

    if result != 0:
        print("[ERROR] Executable target build failed.")
        return result

    print("[SUCCESS] All executable targets finished.")
    return 0


def build(config: str) -> int:
    BUILD_DIR.mkdir(exist_ok=True)

    try:
        cmake_executable = find_cmake_executable()
    except FileNotFoundError as exc:
        print(f"[ERROR] {exc}")
        return 1

    if build_outputs_are_up_to_date(config):
        print(f"[INFO] {config} build is up to date; skipping compile and link.")
    else:
        print(f"[INFO] Configuring {config} build...")
        configure_args = [cmake_executable, "-S", ".", "-B", str(BUILD_DIR)]
        if IS_WINDOWS:
            configure_args += ["-A", "x64", "-D", "GLEW_STATIC=ON"]
        else:
            # Unix系は単一構成ジェネレーターのため、Release/Debugを明示する。
            configure_args += [
                f"-DCMAKE_BUILD_TYPE={config}",
            ]
        result = run_command(configure_args)
        if result != 0:
            print("[ERROR] CMake configuration failed.")
            return result

        print(f"[INFO] Building {config}...")
        result = build_executable_targets_in_parallel(config, cmake_executable)
        if result != 0:
            print("[ERROR] Build execution failed.")
            return result

    if IS_WINDOWS:
        copy_dlls(config)

        # ランチャーも自動ビルド（失敗してもメインビルドは成功扱い）
        try:
            launcher_result = build_launcher(config)
            if launcher_result != 0:
                print("[WARNING] launcher.exe build failed — packaging will skip it.")
        except FileNotFoundError:
            print("[WARNING] cl.exe not found - skipping launcher build. Run from Developer Command Prompt to build it.")

        # ネットワークテスト環境のRecubinEngine.exeを更新
        network_test_dir = TESTCASES_DIR / "NetworkTest"
        source = BUILD_DIR / config / "RecubinEngine.exe"
        destination = network_test_dir / "RecubinEngine.exe"
        try:
            if network_test_dir.exists():
                if copy_if_different(source, destination):
                    print(f"[INFO] Updated {destination}")
                else:
                    print(f"[INFO] NetworkTest executable is up to date: {destination}")
        except Exception as e:
            print(f"[WARNING] Failed to update {destination} in NetworkTest: {e}")
            if isinstance(e, OSError) and getattr(e, "winerror", None) == 32:
                print(f"ファイル {destination} をロックしているプロセスを探索中...")

                def normalized_path(path: str | Path) -> str:
                    return os.path.normcase(
                        os.path.normpath(os.path.abspath(path))
                    ).casefold()

                target_path = normalized_path(destination)
                locking_processes = []

                # 実行イメージはopen_files()に表示されない場合がある。
                for proc in psutil_module.process_iter(["pid", "name"]):
                    is_locker = False
                    try:
                        executable_path = proc.exe()
                    except (
                        psutil_module.AccessDenied,
                        psutil_module.NoSuchProcess,
                        psutil_module.ZombieProcess,
                    ):
                        executable_path = None
                    if executable_path:
                        is_locker = normalized_path(executable_path) == target_path

                    if not is_locker:
                        try:
                            is_locker = any(
                                normalized_path(f.path) == target_path
                                for f in proc.open_files()
                            )
                        except (
                            psutil_module.AccessDenied,
                            psutil_module.NoSuchProcess,
                            psutil_module.ZombieProcess,
                        ):
                            # 権限のないシステムプロセスなどはスキップ
                            continue

                    if is_locker:
                        locking_processes.append(proc)

                if not locking_processes:
                    print("ファイルをロックしている外部プロセスが見つかりませんでした。")
                else:
                    terminated_processes = []
                    for proc in locking_processes:
                        ans = input(
                            f"PID {proc.info['pid']} ({proc.info['name']}) がファイルをロックしています。"
                            "強制終了して再度試行しますか？ (y/n): "
                        )
                        if ans.strip().lower() != "y":
                            continue
                        try:
                            proc.terminate()
                            terminated_processes.append(proc)
                            print(f"PID {proc.info['pid']} を強制終了しました。")
                        except (
                            psutil_module.AccessDenied,
                            psutil_module.NoSuchProcess,
                            psutil_module.ZombieProcess,
                        ) as terminate_error:
                            print(
                                f"[WARNING] PID {proc.info['pid']} の終了に失敗しました: "
                                f"{terminate_error}"
                            )

                    if terminated_processes:
                        for proc in terminated_processes:
                            try:
                                proc.wait(timeout=3)
                            except psutil_module.TimeoutExpired:
                                print(
                                    f"[WARNING] PID {proc.info['pid']} の終了待ちが"
                                    "タイムアウトしました。"
                                )
                            except (
                                psutil_module.AccessDenied,
                                psutil_module.NoSuchProcess,
                                psutil_module.ZombieProcess,
                            ):
                                pass

                        try:
                            if copy_if_different(source, destination):
                                print(f"[INFO] Updated {destination}")
                            else:
                                print(f"[INFO] NetworkTest executable is up to date: {destination}")
                        except Exception as retry_error:
                            print(
                                f"[WARNING] Failed to update {destination} in NetworkTest "
                                f"after terminating the locking process: {retry_error}"
                            )

    else:
        print("[INFO] Non-Windows platform detected - skipping DLL copy and launcher build (Mac版ランチャーは未対応).")

    result = sync_network_test_engine(config)
    if result != 0:
        return result

    print("[SUCCESS] Build process completed.")
    return 0


def run_binary(config: str) -> int:
    exe_path = BUILD_DIR / config / "Recubin.exe" if IS_WINDOWS else BUILD_DIR / "Recubin"
    if not exe_path.exists():
        print(f"[ERROR] Executable not found: {exe_path}")
        return 1

    copy_dlls(config)
    return subprocess.call([str(exe_path)], cwd=ROOT_DIR)


def run_test(config: str, scene_path: str | None) -> int:
    exe_path = BUILD_DIR / config / "RecubinTest.exe" if IS_WINDOWS else BUILD_DIR / "RecubinTest"
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
    if not IS_WINDOWS:
        print("[WARNING] launcher is only supported on Windows.")
        return 1

    src = ROOT_DIR / "launcher" / "main.cpp"
    if not src.exists():
        print(f"[ERROR] launcher/main.cpp not found.")
        return 1

    out_dir = BUILD_DIR / config
    out_dir.mkdir(parents=True, exist_ok=True)
    out_exe = out_dir / "launcher.exe"
    build_script = Path(__file__).resolve()
    if out_exe.is_file():
        try:
            launcher_mtime = out_exe.stat().st_mtime_ns
            if all(
                launcher_mtime >= dependency.stat().st_mtime_ns
                for dependency in (src, build_script)
            ):
                print(f"[INFO] launcher.exe is up to date: {out_exe}")
                return 0
        except OSError:
            pass

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


def package_editor_for_windows(config: str) -> int:
    result = build(config)
    if result != 0:
        return result

    if not VC_REDIST_PATH.exists():
        print(f"[ERROR] Visual C++ Redistributable installer not found: {VC_REDIST_PATH}")
        print("[ERROR] Download the official vc_redist.x64.exe and place it in redist before packaging.")
        return 1

    build_dir = BUILD_DIR / config
    pkg_dir = DIST_DIR / "RecubinStudio"
    if not prepare_studio_package_directory(pkg_dir):
        return 1

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

    # ライセンスコピー
    licence_t = ROOT_DIR / "LICENCE"
    licence_3rd = ROOT_DIR / "LICENCE_3RD_PARTY"
    if licence_t.exists():
        shutil.copy2(licence_t, pkg_dir / "LICENSE")
    else:
        print("[WARNING] LICENSE not found - skipping.")
    if licence_3rd.exists():
        shutil.copy2(licence_3rd, pkg_dir / "LICENCE_3RD_PARTY")
    else:
        print("[WARNING] LICENCE_3RD_PARTY not found - skipping.")

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
    zip_path = create_studio_archive(pkg_dir, archive_base)

    print(f"[SUCCESS] Packaged studio at {pkg_dir}")
    print(f"[SUCCESS] Created archive at {zip_path}")
    return 0


def package_editor_for_macos(config: str) -> int:
    result = build(config)
    if result != 0:
        return result

    recubin_executable = BUILD_DIR / "Recubin"
    engine_executable = BUILD_DIR / "RecubinEngine"
    for executable in (recubin_executable, engine_executable):
        if not executable.is_file():
            print(f"[ERROR] Executable not found: {executable}")
            return 1

    pkg_dir = DIST_DIR / "RecubinStudio"
    if not prepare_studio_package_directory(pkg_dir):
        return 1

    for source, destination in (
        (recubin_executable, pkg_dir / "Recubin"),
        (engine_executable, pkg_dir / "RecubinEngine"),
    ):
        shutil.copy2(source, destination)
        destination.chmod(destination.stat().st_mode | 0o111)

    shaders_src = ROOT_DIR / "shaders"
    shaders_dst = pkg_dir / "shaders"
    shaders_dst.mkdir(parents=True, exist_ok=True)
    shader_paths = list(shaders_src.glob("*.glsl"))
    if not shader_paths:
        print("[ERROR] No shader files found in shaders folder.")
        return 1
    for shader_path in shader_paths:
        shutil.copy2(shader_path, shaders_dst / shader_path.name)

    fonts_src = ROOT_DIR / "assets" / "fonts"
    if not fonts_src.is_dir():
        print("[ERROR] assets/fonts folder missing - cannot package macOS Studio.")
        return 1
    (pkg_dir / "assets").mkdir(parents=True, exist_ok=True)
    shutil.copytree(fonts_src, pkg_dir / "assets" / "fonts")

    for dir_name in ("scenes", "image", "models", "scripts"):
        (pkg_dir / "assets" / dir_name).mkdir(parents=True, exist_ok=True)

    optional_files = (
        (ROOT_DIR / "imgui.ini", pkg_dir / "imgui.ini", "imgui.ini not found - skipping."),
        (ROOT_DIR / "LICENCE", pkg_dir / "LICENSE", "LICENSE not found - skipping."),
        (
            ROOT_DIR / "LICENCE_3RD_PARTY",
            pkg_dir / "LICENCE_3RD_PARTY",
            "LICENCE_3RD_PARTY not found - skipping.",
        ),
    )
    for source, destination, warning in optional_files:
        if source.exists():
            shutil.copy2(source, destination)
        else:
            print(f"[WARNING] {warning}")

    (pkg_dir / "readme.txt").write_text(
        "Recubin is the editor (Studio). RecubinEngine is the runtime used by "
        "the in-editor game Packager. Keep both executables in this folder.\n",
        encoding="utf-8",
    )

    try:
        for executable in (pkg_dir / "Recubin", pkg_dir / "RecubinEngine"):
            result = subprocess.call([
                "/usr/bin/codesign", "--force", "--sign", "-", str(executable)
            ], cwd=ROOT_DIR)
            if result != 0:
                print(f"[ERROR] macOS executable signing failed: {executable}")
                return result
            result = subprocess.call([
                "/usr/bin/codesign", "--verify", "--strict", str(executable)
            ], cwd=ROOT_DIR)
            if result != 0:
                print(f"[ERROR] macOS executable signature verification failed: {executable}")
                return result
    except OSError as exc:
        print(f"[ERROR] Failed to run codesign: {exc}")
        return 1
    date_str = datetime.date.today().strftime("%Y%m%d")
    archive_base = DIST_DIR / f"RecubinStudio-macos-{date_str}"
    zip_path = create_studio_archive(pkg_dir, archive_base)

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
        if IS_WINDOWS:
            return package_editor_for_windows(config)
        if IS_MACOS:
            return package_editor_for_macos(config)
        print(f"[ERROR] Unsupported platform for package: {platform.system()}")
        return 1

    print(f"[ERROR] Unknown action: {action}")
    return 1


if __name__ == "__main__":
    sys.exit(main())

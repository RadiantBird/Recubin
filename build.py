import datetime
import filecmp
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path

import psutil


ROOT_DIR = Path(__file__).resolve().parent
IS_WINDOWS = platform.system() == "Windows"
BUILD_DIR = ROOT_DIR / ("build" if IS_WINDOWS else "build-mac")
DLL_DIR = ROOT_DIR / "dlls"
DIST_DIR = ROOT_DIR / "dist"
TESTCASES_DIR = ROOT_DIR / "TestCases"
VC_REDIST_PATH = ROOT_DIR / "redist" / "vc_redist.x64.exe"
PHYSX_MAC_REPOSITORY = "https://github.com/NVIDIA-Omniverse/PhysX.git"
PHYSX_MAC_TAG = "107.3-physx-5.6.1"
PHYSX_MAC_SOURCE_DIR = BUILD_DIR / "_deps" / "physx-src"
PHYSX_MAC_BUILD_DIR = BUILD_DIR / "_deps" / "physx-build"
PHYSX_MAC_LIBRARY_DIR = PHYSX_MAC_BUILD_DIR / "bin" / "UNKNOWN" / "release"
PHYSX_MAC_PATCH = ROOT_DIR / "tools" / "macos" / "physx-5.6.1-macos-arm64.patch"
PHYSX_MAC_LIBRARIES = (
    "libPhysX_static.a",
    "libPhysXCommon_static.a",
    "libPhysXFoundation_static.a",
    "libPhysXExtensions_static.a",
    "libPhysXCooking_static.a",
)


def run_command(args: list[str]) -> int:
    return subprocess.call(args, cwd=ROOT_DIR)


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


def normalize_config(value: str | None) -> str:
    if value is None:
        return "Release"

    lowered = value.lower()
    if lowered in ("debug", "d"):
        return "Debug"
    if lowered in ("release", "r"):
        return "Release"
    raise ValueError(f"Unknown configuration: {value}")


def run_physx_command(args: list[str], cwd: Path) -> int:
    try:
        return subprocess.call(args, cwd=cwd)
    except OSError as exc:
        print(f"[ERROR] Failed to run {args[0]}: {exc}")
        return 1


def apply_physx_mac_patch() -> int:
    check_args = ["git", "apply", "--check", str(PHYSX_MAC_PATCH)]
    reverse_check_args = ["git", "apply", "--reverse", "--check", str(PHYSX_MAC_PATCH)]

    try:
        check_result = subprocess.call(
            check_args,
            cwd=PHYSX_MAC_SOURCE_DIR,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        if check_result == 0:
            print("[INFO] Applying the macOS Apple Silicon PhysX patch...")
            return run_physx_command(
                ["git", "apply", str(PHYSX_MAC_PATCH)],
                PHYSX_MAC_SOURCE_DIR,
            )

        reverse_check_result = subprocess.call(
            reverse_check_args,
            cwd=PHYSX_MAC_SOURCE_DIR,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    except OSError as exc:
        print(f"[ERROR] Failed to check the PhysX patch: {exc}")
        return 1

    if reverse_check_result == 0:
        print("[INFO] The macOS Apple Silicon PhysX patch is already applied.")
        return 0

    print(f"[ERROR] PhysX patch cannot be applied cleanly: {PHYSX_MAC_PATCH}")
    return 1


def bootstrap_physx_mac() -> tuple[int, Path | None]:
    machine = platform.machine().lower()
    if platform.system() != "Darwin" or machine not in ("arm64", "aarch64"):
        print(
            "[ERROR] Automatic PhysX bootstrap is supported only on Apple Silicon "
            "macOS (arm64). Set RECUBIN_PHYSX_MAC_DIR to use a prebuilt PhysX."
        )
        return 1, None

    if not PHYSX_MAC_PATCH.is_file():
        print(f"[ERROR] PhysX patch not found: {PHYSX_MAC_PATCH}")
        return 1, None

    PHYSX_MAC_SOURCE_DIR.parent.mkdir(parents=True, exist_ok=True)
    if not PHYSX_MAC_SOURCE_DIR.exists():
        print(f"[INFO] Cloning PhysX {PHYSX_MAC_TAG}...")
        result = run_physx_command([
            "git", "clone", "--depth", "1", "--branch", PHYSX_MAC_TAG,
            PHYSX_MAC_REPOSITORY, str(PHYSX_MAC_SOURCE_DIR),
        ], ROOT_DIR)
        if result != 0:
            print("[ERROR] PhysX clone failed.")
            return result, None
    elif not (PHYSX_MAC_SOURCE_DIR / ".git").exists():
        print(f"[ERROR] Existing PhysX source directory is not a Git checkout: {PHYSX_MAC_SOURCE_DIR}")
        return 1, None

    result = apply_physx_mac_patch()
    if result != 0:
        return result, None

    if all((PHYSX_MAC_LIBRARY_DIR / name).is_file() for name in PHYSX_MAC_LIBRARIES):
        print(f"[INFO] Using existing PhysX libraries: {PHYSX_MAC_LIBRARY_DIR}")
        return 0, PHYSX_MAC_LIBRARY_DIR

    print("[INFO] Configuring PhysX Release static libraries...")
    configure_args = [
        "cmake",
        "-S", str(PHYSX_MAC_SOURCE_DIR / "physx" / "compiler" / "public"),
        "-B", str(PHYSX_MAC_BUILD_DIR),
        f"-DPHYSX_ROOT_DIR={PHYSX_MAC_SOURCE_DIR / 'physx'}",
        "-DTARGET_BUILD_PLATFORM=linux",
        "-DPX_GENERATE_STATIC_LIBRARIES=ON",
        "-DPX_BUILDSNIPPETS=OFF",
        "-DPX_BUILDPVDRUNTIME=OFF",
        "-DCMAKE_BUILD_TYPE=release",
        f"-DPX_OUTPUT_LIB_DIR={PHYSX_MAC_BUILD_DIR}",
        f"-DPX_OUTPUT_BIN_DIR={PHYSX_MAC_BUILD_DIR}",
    ]
    result = run_physx_command(configure_args, ROOT_DIR)
    if result != 0:
        print("[ERROR] PhysX CMake configuration failed.")
        return result, None

    print("[INFO] Building PhysX Release static libraries...")
    result = run_physx_command([
        "cmake", "--build", str(PHYSX_MAC_BUILD_DIR),
        "--config", "release", "--parallel", "4",
        "--target", "PhysX", "PhysXCommon", "PhysXFoundation",
        "PhysXExtensions", "PhysXCooking",
    ], ROOT_DIR)
    if result != 0:
        print("[ERROR] PhysX build failed.")
        return result, None

    missing = [name for name in PHYSX_MAC_LIBRARIES if not (PHYSX_MAC_LIBRARY_DIR / name).is_file()]
    if missing:
        print(f"[ERROR] PhysX build completed but required libraries are missing: {', '.join(missing)}")
        return 1, None

    return 0, PHYSX_MAC_LIBRARY_DIR


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


def build(config: str) -> int:
    BUILD_DIR.mkdir(exist_ok=True)

    print(f"[INFO] Configuring {config} build...")
    configure_args = ["cmake", "-S", ".", "-B", str(BUILD_DIR)]
    if IS_WINDOWS:
        configure_args += ["-A", "x64", "-D", "GLEW_STATIC=ON"]
    else:
        # Unix系は単一構成ジェネレーターのため、Release/Debugを明示する。
        configure_args += [
            f"-DCMAKE_BUILD_TYPE={config}",
            "-U", "PHYSX_MAC_LIB_*",
        ]
        physx_dir = os.environ.get("RECUBIN_PHYSX_MAC_DIR")
        if not physx_dir:
            result, bootstrapped_dir = bootstrap_physx_mac()
            if result != 0 or bootstrapped_dir is None:
                return result if result != 0 else 1
            physx_dir = str(bootstrapped_dir)
        configure_args += [f"-DRECUBIN_PHYSX_MAC_DIR={physx_dir}"]
    result = run_command(configure_args)
    if result != 0:
        print("[ERROR] CMake configuration failed.")
        return result

    print(f"[INFO] Building {config}...")
    build_args = [
        "cmake", "--build", str(BUILD_DIR), "--config", config,
        "--parallel",
    ]
    if not IS_WINDOWS:
        build_args.append("4")
    build_args += ["--target", "Recubin", "RecubinEngine", "RecubinTest"]
    result = run_command(build_args)
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
                for proc in psutil.process_iter(["pid", "name"]):
                    is_locker = False
                    try:
                        executable_path = proc.exe()
                    except (
                        psutil.AccessDenied,
                        psutil.NoSuchProcess,
                        psutil.ZombieProcess,
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
                            psutil.AccessDenied,
                            psutil.NoSuchProcess,
                            psutil.ZombieProcess,
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
                            psutil.AccessDenied,
                            psutil.NoSuchProcess,
                            psutil.ZombieProcess,
                        ) as terminate_error:
                            print(
                                f"[WARNING] PID {proc.info['pid']} の終了に失敗しました: "
                                f"{terminate_error}"
                            )

                    if terminated_processes:
                        for proc in terminated_processes:
                            try:
                                proc.wait(timeout=3)
                            except psutil.TimeoutExpired:
                                print(
                                    f"[WARNING] PID {proc.info['pid']} の終了待ちが"
                                    "タイムアウトしました。"
                                )
                            except (
                                psutil.AccessDenied,
                                psutil.NoSuchProcess,
                                psutil.ZombieProcess,
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

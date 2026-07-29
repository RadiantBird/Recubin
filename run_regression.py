import re
import subprocess
import sys
import tempfile
from pathlib import Path

# 子プロセス(Recubin.exe/RecubinTest.exe)の出力はUTF-8/CP932が混在しうるため、
# コンソールの既定コードページに関わらず落ちないようにreplaceで寛容にする。
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
sys.stderr.reconfigure(encoding="utf-8", errors="replace")

ROOT_DIR = Path(__file__).resolve().parent
BUILD_DIR = ROOT_DIR / "build"

# 手書きスクリプト(PASS/FAIL付き)を含む既存シーン。
# test_rope/test_rod/test_motor/test_weld/test_weld_chain はスクリプトを持たない
# 目視確認用シーンのため対象外。
FIXED_SCENES = [
    "assets/scenes/physics_test.yaml",
    "assets/scenes/pathfinder.yaml",
    "assets/scenes/void.yaml",
    "assets/scenes/terrain_test.yaml",
    "assets/scenes/test_scene.yaml",
    "assets/scenes/signal_test.yaml",
    "assets/scenes/value_test.yaml",
    "assets/scenes/test_bindings.yaml",
]

RESULT_RE = re.compile(r"\[RecubinTest\]\s+(\d+)\s+passed,\s+(\d+)\s+failed\.")


def normalize_config(value: str | None) -> str:
    if value is None:
        return "Release"
    lowered = value.lower()
    if lowered in ("debug", "d"):
        return "Debug"
    if lowered in ("release", "r"):
        return "Release"
    raise ValueError(f"Unknown configuration: {value}")


def run_scene(test_exe: Path, scene_path: str) -> tuple[int, int, int]:
    """Returns (exit_code, passed, failed). passed/failed are -1 if unparsable."""
    proc = subprocess.run(
        [str(test_exe), scene_path],
        cwd=ROOT_DIR,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    print(proc.stdout, end="")
    if proc.stderr:
        print(proc.stderr, end="", file=sys.stderr)

    match = None
    for line in reversed(proc.stdout.splitlines()):
        match = RESULT_RE.search(line)
        if match:
            break
    if not match:
        return proc.returncode, -1, -1
    return proc.returncode, int(match.group(1)), int(match.group(2))


def main() -> int:
    config = normalize_config(sys.argv[1] if len(sys.argv) >= 2 else None)

    editor_exe = BUILD_DIR / config / "Recubin.exe"
    test_exe = BUILD_DIR / config / "RecubinTest.exe"
    if not editor_exe.exists():
        print(f"[ERROR] Executable not found: {editor_exe} (run `python build.py build` first)")
        return 1
    if not test_exe.exists():
        print(f"[ERROR] Executable not found: {test_exe} (run `python build.py build` first)")
        return 1

    total_passed = 0
    total_failed = 0
    any_crash = False

    print(f"[INFO] Running headless sound stretch regression ...")
    sound_proc = subprocess.run(
        [str(test_exe), "--sound-stretch-regression"],
        cwd=ROOT_DIR,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    print(sound_proc.stdout, end="")
    if sound_proc.stderr:
        print(sound_proc.stderr, end="", file=sys.stderr)
    if sound_proc.returncode != 0:
        print(f"[ERROR] Sound stretch regression failed (exit code {sound_proc.returncode}).")
        return 1

    with tempfile.TemporaryDirectory() as tmp_dir:
        generated_scene = str(Path(tmp_dir) / "gen_test_scene.yaml")

        print(f"[INFO] Generating all-instances scene via {editor_exe.name} --gen-test-scene ...")
        gen_proc = subprocess.run(
            [str(editor_exe), "--gen-test-scene", generated_scene],
            cwd=ROOT_DIR,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        print(gen_proc.stdout, end="")
        if gen_proc.stderr:
            print(gen_proc.stderr, end="", file=sys.stderr)
        if gen_proc.returncode != 0 or not Path(generated_scene).exists():
            print(f"[ERROR] --gen-test-scene failed (exit code {gen_proc.returncode}).")
            return 1

        scenes = [generated_scene] + [str(ROOT_DIR / s) for s in FIXED_SCENES]

        for scene in scenes:
            print(f"\n[INFO] Running {scene} ...")
            exit_code, passed, failed = run_scene(test_exe, scene)
            if exit_code < 0:
                any_crash = True
                print(f"[ERROR] {scene} crashed (exit code {exit_code}).")
                continue
            if passed < 0:
                any_crash = True
                print(f"[ERROR] {scene}: could not parse RecubinTest result line.")
                continue
            total_passed += passed
            total_failed += failed

    print(f"\n[SUMMARY] {total_passed} passed, {total_failed} failed across {len(FIXED_SCENES) + 1} scenes.")
    if any_crash or total_failed > 0:
        print("[SUMMARY] Regression FAILED.")
        return 1
    print("[SUMMARY] Regression OK.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

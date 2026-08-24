import re
import subprocess
import sys
import tempfile
import time
import wave
import struct
import zlib
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
PROCESS_TIMEOUT_SECONDS = 180


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
    try:
        proc = subprocess.run(
            [str(test_exe), scene_path],
        cwd=ROOT_DIR,
        capture_output=True,
        text=True,
        encoding="utf-8",
            errors="replace",
            timeout=PROCESS_TIMEOUT_SECONDS,
        )
    except subprocess.TimeoutExpired:
        print(f"[ERROR] {scene_path}: timed out after {PROCESS_TIMEOUT_SECONDS}s")
        return -124, -1, -1
    print(proc.stdout, end="")
    if proc.stderr:
        print(proc.stderr, end="", file=sys.stderr)

    matches = RESULT_RE.findall(proc.stdout)
    if len(matches) != 1:
        return proc.returncode, -1, -1
    return proc.returncode, int(matches[0][0]), int(matches[0][1])


def list_regressions(test_exe: Path) -> list[str] | None:
    try:
        proc = subprocess.run(
            [str(test_exe), "--list-regressions"], cwd=ROOT_DIR,
            capture_output=True, text=True, encoding="utf-8", errors="replace",
            timeout=PROCESS_TIMEOUT_SECONDS,
        )
    except subprocess.TimeoutExpired:
        print("[ERROR] --list-regressions timed out")
        return None
    if proc.returncode != 0:
        print(proc.stderr, end="", file=sys.stderr)
        return None
    modes = [line.strip() for line in proc.stdout.splitlines() if line.strip()]
    if any(not mode.startswith("--") for mode in modes) or len(modes) != len(set(modes)):
        print("[ERROR] --list-regressions returned an invalid registry")
        return None
    return modes


def run_dedicated(test_exe: Path, mode: str, backend: str) -> tuple[bool, int, int]:
    command = [str(test_exe), mode, f"--physics={backend}"]
    try:
        proc = subprocess.run(command, cwd=ROOT_DIR, capture_output=True, text=True,
                              encoding="utf-8", errors="replace",
                              timeout=PROCESS_TIMEOUT_SECONDS)
    except subprocess.TimeoutExpired:
        print(f"[ERROR] {mode} ({backend}) timed out")
        return False, -1, -1
    print(proc.stdout, end="")
    if proc.stderr:
        print(proc.stderr, end="", file=sys.stderr)
    matches = RESULT_RE.findall(proc.stdout)
    failed = sum(int(fail) for _, fail in matches)
    passed = sum(int(ok) for ok, _ in matches)
    output = proc.stdout + proc.stderr
    if proc.returncode != 0 or len(matches) != 1 or failed > 0 or re.search(r"\bFAIL\b", output):
        print(f"[ERROR] {mode} ({backend}) failed or was not parseable")
        return False, passed, failed
    return True, passed, failed


def run_gui_smoke(editor_exe: Path, temp_dir: Path) -> bool:
    capture_path = temp_dir / "gui_smoke.png"
    scene_path = temp_dir / "gui_smoke.yaml"
    settings_path = temp_dir / "gui_smoke_settings.yaml"
    scene_path.write_text("""Root:
  Children:
    - ClassName: Workspace
      Name: Workspace
      Children:
        - ClassName: Cube
          Name: Target
          Properties:
            Position: [0, 0, 0]
            Size: [2, 2, 2]
            Anchored: true
        - ClassName: Cube
          Name: Other
          Properties:
            Position: [4, 0, 0]
            Size: [2, 2, 2]
            Anchored: true
        - ClassName: Weld
          Name: TargetWeld
          Properties:
            Cube0: Workspace\\Target
            Cube1: Workspace\\Other
        - ClassName: FontFile
          Name: FontFile
          Properties:
            ContentPath: ""
        - ClassName: TextLabel
          Name: FontUser
          Properties:
            UseFontFile: false
            FontFile: Workspace\\FontFile
        - ClassName: Sound
          Name: Sound
        - ClassName: SurfaceMark
          Name: SurfaceMark
""", encoding="utf-8")
    settings_path.write_text("{}\n", encoding="utf-8")
    original_settings = ROOT_DIR / "editor_settings.yaml"
    original_snapshot = (original_settings.read_bytes(), original_settings.stat().st_mtime_ns) if original_settings.exists() else None
    try:
        process = subprocess.Popen(
            [str(editor_exe), "--ui-automation", "--ui-automation-scene", str(scene_path),
             "--ui-automation-settings", str(settings_path)], cwd=ROOT_DIR,
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, encoding="utf-8", errors="replace",
        )
        commands = "\n".join([
            'focus_window "###Explorer"',
            'wait "Explorer/Node/System\\\\"',
            'right_click "Explorer/Node/System\\\\Workspace"',
            'wait "Explorer/Context/InsertObject"',
            'click "Explorer/Context/InsertObject"',
            'wait "Explorer/ClassPicker/Search"',
            'click "Explorer/ClassPicker/Search"',
            'type Sound',
            'wait "Explorer/ClassPicker/Class/Sound"',
            'click "Explorer/ClassPicker/Class/Sound"',
            'wait "Explorer/Node/System\\\\Workspace\\\\Sound1"',
            'key Ctrl+Z',
            'key Ctrl+Shift+Z',
            'right_click "Explorer/Node/System\\\\Workspace"',
            'wait "Explorer/Context/InsertObject"',
            'click "Explorer/Context/InsertObject"',
            'wait "Explorer/ClassPicker/Search"',
            'click "Explorer/ClassPicker/Search"',
            'type SurfaceMark',
            'wait "Explorer/ClassPicker/Class/SurfaceMark"',
            'click "Explorer/ClassPicker/Class/SurfaceMark"',
            'wait "Explorer/Node/System\\\\Workspace\\\\SurfaceMark1"',
            'key Ctrl+Z',
            'key Ctrl+Shift+Z',
            'right_click "Explorer/Node/System\\\\Workspace\\\\FontFile"',
            'wait "Explorer/Context/ReplaceInstance"',
            'click "Explorer/Context/ReplaceInstance"',
            'wait "Explorer/ClassPicker/Search"',
            'click "Explorer/ClassPicker/Search"',
            'type Script',
            'wait "Explorer/ClassPicker/Class/Script"',
            'click "Explorer/ClassPicker/Class/Script"',
            'wait "Explorer/ClassPicker/ConfirmReplace"',
            'click "Explorer/ClassPicker/Search"',
            'key Ctrl+A',
            'type Folder',
            'wait "Explorer/ClassPicker/Class/Folder"',
            'click "Explorer/ClassPicker/Class/Folder"',
            'wait "Explorer/ClassPicker/ConfirmReplace"',
            'click "Explorer/ClassPicker/ConfirmReplace"',
            'key Ctrl+Z',
            'key Ctrl+Shift+Z',
            'capture "' + capture_path.as_posix().replace('\\', '\\\\').replace('"', '\\"') + '"',
            'quit',
            'wait "Editor/UnsavedChanges/QuitWithoutSaving" 10',
            'click "Editor/UnsavedChanges/QuitWithoutSaving"',
        ]) + "\n"
        stdout, stderr = process.communicate(commands, timeout=PROCESS_TIMEOUT_SECONDS)
    except subprocess.TimeoutExpired:
        process.kill()
        stdout, stderr = process.communicate()
        print(stdout, end="")
        if stderr:
            print(stderr, end="", file=sys.stderr)
        print("[ERROR] GUI automation smoke timed out")
        return False
    print(stdout, end="")
    if stderr:
        print(stderr, end="", file=sys.stderr)
    if process.returncode != 0 or re.search(r"\bERROR\b", stdout + stderr):
        print("[ERROR] GUI automation smoke exited with errors")
        return False
    if not capture_path.exists():
        print("[ERROR] GUI automation smoke did not produce a capture")
        return False
    data = capture_path.read_bytes()
    if len(data) < 24 or data[:8] != b"\x89PNG\r\n\x1a\n":
        print("[ERROR] GUI automation capture is not a PNG")
        return False
    position = 8
    idat = bytearray()
    width = height = 0
    while position + 12 <= len(data):
        size = struct.unpack(">I", data[position:position + 4])[0]
        kind = data[position + 4:position + 8]
        payload = data[position + 8:position + 8 + size]
        if kind == b"IHDR": width, height = struct.unpack(">II", payload[:8])
        if kind == b"IDAT": idat.extend(payload)
        position += size + 12
        if kind == b"IEND": break
    try:
        decoded = zlib.decompress(bytes(idat))
    except zlib.error:
        return False
    if width <= 0 or height <= 0 or not decoded:
        print("[ERROR] GUI automation capture has empty PNG content")
        return False
    stride = width * 4
    rows = []
    previous = bytearray(stride)
    offset = 0
    for _ in range(height):
        if offset + stride + 1 > len(decoded):
            return False
        filter_type = decoded[offset]
        encoded = decoded[offset + 1:offset + 1 + stride]
        row = bytearray(stride)
        for index, value in enumerate(encoded):
            left = row[index - 4] if index >= 4 else 0
            up = previous[index]
            up_left = previous[index - 4] if index >= 4 else 0
            if filter_type == 0: predictor = 0
            elif filter_type == 1: predictor = left
            elif filter_type == 2: predictor = up
            elif filter_type == 3: predictor = (left + up) // 2
            elif filter_type == 4:
                estimate = left + up - up_left
                distances = (abs(estimate - left), abs(estimate - up), abs(estimate - up_left))
                predictor = (left, up, up_left)[distances.index(min(distances))]
            else:
                return False
            row[index] = (value + predictor) & 0xFF
        rows.append(bytes(row))
        previous = row
        offset += stride + 1
    pixels = [row[index:index + 4] for row in rows for index in range(0, stride, 4)]
    if len(set(pixels)) < 2:
        print("[ERROR] GUI automation capture is empty or single-color")
        return False
    if original_snapshot is not None and (original_settings.read_bytes(), original_settings.stat().st_mtime_ns) != original_snapshot:
        print("[ERROR] editor_settings.yaml changed during GUI smoke")
        return False
    if original_snapshot is None and original_settings.exists():
        print("[ERROR] GUI smoke unexpectedly created editor_settings.yaml")
        return False
    required_ok = (
        "[UIAUTO] OK wait target Explorer/Node/System\\Workspace\\Sound1" in stdout and
        "[UIAUTO] OK wait target Explorer/Node/System\\Workspace\\SurfaceMark1" in stdout and
        stdout.count("[UIAUTO] OK wait target Explorer/ClassPicker/ConfirmReplace") >= 2 and
        "[UIAUTO] OK capture" in stdout and "[UIAUTO] OK quit" in stdout and
        "[UIAUTO] OK wait target Editor/UnsavedChanges/QuitWithoutSaving" in stdout and
        "[UIAUTO] OK click Editor/UnsavedChanges/QuitWithoutSaving" in stdout
    )
    if not required_ok:
        print("[ERROR] GUI automation smoke did not report every required operation")
        return False
    return True


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

    registered = list_regressions(test_exe)
    if registered is None:
        return 1
    # The runner intentionally has no second hand-maintained mode list: every
    # mode exposed by RecubinTest is a release gate.
    print(f"[INFO] Registry contains {len(registered)} dedicated regressions.")

    total_passed = 0
    total_failed = 0
    any_crash = False

    for backend in ("physx", "box3d"):
        for mode in registered:
            ok, passed, failed = run_dedicated(test_exe, mode, backend)
            total_passed += passed
            total_failed += failed if failed >= 0 else 1
            if not ok:
                any_crash = True

    with tempfile.TemporaryDirectory() as tmp_dir:
        if not run_gui_smoke(editor_exe, Path(tmp_dir)):
            return 1
        generated_scene = str(Path(tmp_dir) / "gen_test_scene.yaml")
        sound_path = Path(tmp_dir) / "regression_sound.wav"
        with wave.open(str(sound_path), "wb") as sound:
            sound.setnchannels(1)
            sound.setsampwidth(2)
            sound.setframerate(22050)
            sound.writeframes(b"\x00\x00" * 2205)
        sound_scene = Path(tmp_dir) / "test_bindings_sound.yaml"
        sound_source = (ROOT_DIR / "assets/scenes/test_bindings.yaml").read_text(encoding="utf-8")
        sound_scene.write_text(sound_source.replace(
            "assets/sound/Flight of the Bumblebee.mp3", sound_path.as_posix()), encoding="utf-8")

        print(f"[INFO] Generating all-instances scene via {editor_exe.name} --gen-test-scene ...")
        try:
            gen_proc = subprocess.run(
                [str(editor_exe), "--gen-test-scene", generated_scene],
                cwd=ROOT_DIR, capture_output=True, text=True,
                encoding="utf-8", errors="replace",
                timeout=PROCESS_TIMEOUT_SECONDS,
            )
        except subprocess.TimeoutExpired:
            print(f"[ERROR] --gen-test-scene timed out after {PROCESS_TIMEOUT_SECONDS}s")
            return 1
        print(gen_proc.stdout, end="")
        if gen_proc.stderr:
            print(gen_proc.stderr, end="", file=sys.stderr)
        if gen_proc.returncode != 0 or not Path(generated_scene).exists():
            print(f"[ERROR] --gen-test-scene failed (exit code {gen_proc.returncode}).")
            return 1

        scenes = [generated_scene] + [str(ROOT_DIR / s) for s in FIXED_SCENES]
        scenes = [str(sound_scene) if scene.endswith("test_bindings.yaml") else scene for scene in scenes]

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

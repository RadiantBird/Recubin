import stat
import tempfile
import unittest
import zipfile
from pathlib import Path

import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

import build


class StudioPackageLayoutTests(unittest.TestCase):
    def test_preserves_local_autosaves_and_archive_only_contains_marker(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            package = root / "RecubinStudio"
            (package / ".autosave" / "Scene.rcbn").mkdir(parents=True)
            (package / ".autosave" / "Scene.rcbn" / "recovery.rcbn").write_text("private")
            (package / "stale.txt").write_text("stale")

            self.assertTrue(build.prepare_studio_package_directory(package))
            self.assertFalse((package / "stale.txt").exists())
            self.assertFalse((package / "Contents").exists())
            self.assertTrue((package / ".autosave" / "Scene.rcbn" / "recovery.rcbn").is_file())

            executable = package / "Recubin"
            executable.write_bytes(b"Mach-O fixture")
            executable.chmod(executable.stat().st_mode | stat.S_IXUSR)
            (package / "RecubinEngine").write_bytes(b"runtime")
            (package / "shaders").mkdir()
            (package / "assets" / "fonts").mkdir(parents=True)
            archive = build.create_studio_archive(package, root / "RecubinStudio-macos-20000101")

            with zipfile.ZipFile(archive) as zipped:
                names = set(zipped.namelist())
                self.assertIn("RecubinStudio/.autosave/", names)
                self.assertIn("RecubinStudio/Recubin", names)
                self.assertNotIn("RecubinStudio.app/Contents/", names)
                self.assertFalse(any("Scene.rcbn" in name for name in names))

            self.assertTrue(executable.stat().st_mode & stat.S_IXUSR)

    def test_rejects_non_directory_autosave_marker(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            package = Path(temporary) / "RecubinStudio"
            package.mkdir()
            (package / ".autosave").write_text("invalid")
            (package / "existing.txt").write_text("keep on failure")
            self.assertFalse(build.prepare_studio_package_directory(package))
            self.assertTrue((package / "existing.txt").is_file())


if __name__ == "__main__":
    unittest.main()

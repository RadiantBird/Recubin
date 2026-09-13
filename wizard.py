from __future__ import annotations

import argparse
import datetime
import difflib
import os
import stat
import shutil
import sys
import tempfile

from dataclasses import dataclass, field, replace
from pathlib import Path


ROOT = Path(__file__).resolve().parent
BACKUP_ROOT = ROOT / ".wizard-backup"

UTF8_BOM = b"\xef\xbb\xbf"


# =============================================================================
# Patch DSL v1
#
# Usage:
#
#   python wizard.py patch.txt
#   python wizard.py patch.txt --check
#
#
# Example:
#
#   PATCH 1
#
#   FILE build.py
#
#   ASSERT_COUNT 1
#   <<<
#   import datetime
#   >>>
#
#   ASSERT_NOT_CONTAINS
#   <<<
#   import concurrent.futures
#   >>>
#
#   INSERT_BEFORE
#   <<<
#   import datetime
#   >>>
#   WITH
#   <<<
#   import concurrent.futures
#   >>>
#
#   REPLACE
#   <<<
#   old code
#   >>>
#   WITH
#   <<<
#   new code
#   >>>
#
#   END_FILE
#   END_PATCH
#
#
# Commands:
#
#   PATCH 1
#
#   FILE path
#
#   ASSERT_COUNT n
#   <<< ... >>>
#
#   ASSERT_CONTAINS
#   <<< ... >>>
#
#   ASSERT_NOT_CONTAINS
#   <<< ... >>>
#
#   REPLACE
#   <<< old >>>
#   WITH
#   <<< new >>>
#
#   REPLACE_ALL
#   <<< old >>>
#   WITH
#   <<< new >>>
#
#   INSERT_BEFORE
#   <<< anchor >>>
#   WITH
#   <<< inserted text >>>
#
#   INSERT_AFTER
#   <<< anchor >>>
#   WITH
#   <<< inserted text >>>
#
#   DELETE
#   <<< text >>>
#
#   CREATE_FILE
#   <<< complete file contents >>>
#
#   DELETE_FILE
#
#   END_FILE
#   END_PATCH
#
#
# Safety:
#
# - REPLACE / INSERT_BEFORE / INSERT_AFTER / DELETE require exactly one match.
# - REPLACE_ALL requires at least one match.
# - All assertions and edits are checked before any source file is written.
# - Absolute paths and ".." escapes are rejected.
# - Existing files are backed up before commit.
# - Writes use temporary files + os.replace().
# - On a commit failure, wizard attempts to restore every original file.
#
# @RadiantBird 2026/09/13:
# wizard.py is intentionally a generic transactional patch runner.
# AI-generated edits belong in patch.txt rather than in one-off Python scripts.
# =============================================================================


class WizardError(RuntimeError):
    pass


@dataclass
class SourceFile:
    path: Path
    existed: bool
    text: str
    newline: str = "\n"
    had_bom: bool = False
    deleted: bool = False


@dataclass
class Operation:
    kind: str
    argument: int | None = None
    first: str | None = None
    second: str | None = None
    line_number: int = 0


@dataclass
class FilePatch:
    relative_path: Path
    operations: list[Operation] = field(default_factory=list)


@dataclass
class PatchDocument:
    version: int
    files: list[FilePatch]


def fail(message: str) -> None:
    raise WizardError(message)


def normalize_newlines(text: str) -> str:
    return text.replace("\r\n", "\n").replace("\r", "\n")


def detect_newline(text: str) -> str:
    if "\r\n" in text:
        return "\r\n"

    if "\r" in text:
        return "\r"

    return "\n"


def resolve_target(relative_path: Path) -> Path:
    if relative_path.is_absolute():
        fail(f"Absolute paths are not allowed: {relative_path}")

    if ".." in relative_path.parts:
        fail(f"Parent path escapes are not allowed: {relative_path}")

    target = (ROOT / relative_path).resolve()

    try:
        target.relative_to(ROOT)
    except ValueError:
        fail(f"Path escapes repository root: {relative_path}")

    return target


def read_source(relative_path: Path) -> SourceFile:
    path = resolve_target(relative_path)

    if not path.exists():
        return SourceFile(
            path=path,
            existed=False,
            text="",
        )

    if not path.is_file():
        fail(f"Target is not a regular file: {relative_path}")

    raw = path.read_bytes()
    had_bom = raw.startswith(UTF8_BOM)

    if had_bom:
        raw = raw[len(UTF8_BOM):]

    try:
        decoded = raw.decode("utf-8")
    except UnicodeDecodeError as exc:
        fail(
            f"File is not valid UTF-8: {relative_path}: "
            f"{exc}"
        )

    return SourceFile(
        path=path,
        existed=True,
        text=normalize_newlines(decoded),
        newline=detect_newline(decoded),
        had_bom=had_bom,
    )


def encode_source(source: SourceFile) -> bytes:
    text = source.text

    if source.newline != "\n":
        text = text.replace("\n", source.newline)

    data = text.encode("utf-8")

    if source.had_bom:
        data = UTF8_BOM + data

    return data


class Parser:
    def __init__(self, text: str) -> None:
        self.lines = normalize_newlines(text).splitlines(keepends=True)
        self.index = 0

    def current_line_number(self) -> int:
        return self.index + 1

    def at_end(self) -> bool:
        return self.index >= len(self.lines)

    def peek_raw(self) -> str:
        if self.at_end():
            return ""

        return self.lines[self.index]

    def peek_command(self) -> str:
        return self.peek_raw().strip()

    def advance(self) -> str:
        if self.at_end():
            fail("Unexpected end of patch file.")

        value = self.lines[self.index]
        self.index += 1
        return value

    def skip_empty_and_comments(self) -> None:
        while not self.at_end():
            stripped = self.peek_command()

            if stripped == "" or stripped.startswith("#"):
                self.index += 1
                continue

            break

    def expect(self, command: str) -> None:
        self.skip_empty_and_comments()

        if self.at_end():
            fail(
                f"Expected '{command}', "
                "but reached end of patch file."
            )

        actual = self.peek_command()

        if actual != command:
            fail(
                f"Line {self.current_line_number()}: "
                f"expected '{command}', got '{actual}'."
            )

        self.index += 1

    def read_block(self) -> str:
        self.skip_empty_and_comments()

        if self.at_end():
            fail("Expected '<<<', but reached end of patch file.")

        if self.peek_command() != "<<<":
            fail(
                f"Line {self.current_line_number()}: "
                f"expected '<<<', got '{self.peek_command()}'."
            )

        self.index += 1
        block_lines: list[str] = []

        while not self.at_end():
            if self.peek_command() == ">>>":
                self.index += 1
                return "".join(block_lines)

            block_lines.append(self.advance())

        fail("Unterminated text block. Missing '>>>'.")

    def parse(self) -> PatchDocument:
        self.skip_empty_and_comments()

        if self.at_end():
            fail("Patch file is empty.")

        header = self.peek_command()

        if not header.startswith("PATCH "):
            fail(
                f"Line {self.current_line_number()}: "
                "patch must begin with 'PATCH 1'."
            )

        parts = header.split()

        if len(parts) != 2:
            fail(
                f"Line {self.current_line_number()}: "
                f"invalid PATCH header: {header}"
            )

        try:
            version = int(parts[1])
        except ValueError:
            fail(
                f"Line {self.current_line_number()}: "
                f"invalid patch version: {parts[1]}"
            )

        if version != 1:
            fail(f"Unsupported patch DSL version: {version}")

        self.index += 1

        files: list[FilePatch] = []
        seen_paths: set[Path] = set()

        while True:
            self.skip_empty_and_comments()

            if self.at_end():
                fail("Missing END_PATCH.")

            command = self.peek_command()

            if command == "END_PATCH":
                self.index += 1
                break

            if not command.startswith("FILE "):
                fail(
                    f"Line {self.current_line_number()}: "
                    f"expected FILE or END_PATCH, got '{command}'."
                )

            relative_text = command[5:].strip()

            if not relative_text:
                fail(
                    f"Line {self.current_line_number()}: "
                    "FILE requires a path."
                )

            relative_path = Path(relative_text)

            if relative_path in seen_paths:
                fail(
                    f"File appears more than once in patch: "
                    f"{relative_path}"
                )

            seen_paths.add(relative_path)
            self.index += 1

            file_patch = FilePatch(
                relative_path=relative_path,
            )

            while True:
                self.skip_empty_and_comments()

                if self.at_end():
                    fail(
                        f"Missing END_FILE for "
                        f"{relative_path}."
                    )

                command_line = self.peek_command()
                line_number = self.current_line_number()

                if command_line == "END_FILE":
                    self.index += 1
                    break

                if command_line.startswith("FILE "):
                    fail(
                        f"Line {line_number}: "
                        f"missing END_FILE before next FILE."
                    )

                if command_line == "END_PATCH":
                    fail(
                        f"Line {line_number}: "
                        f"missing END_FILE for {relative_path}."
                    )

                if command_line.startswith("ASSERT_COUNT "):
                    parts = command_line.split()

                    if len(parts) != 2:
                        fail(
                            f"Line {line_number}: "
                            "ASSERT_COUNT syntax is "
                            "'ASSERT_COUNT n'."
                        )

                    try:
                        count = int(parts[1])
                    except ValueError:
                        fail(
                            f"Line {line_number}: "
                            f"invalid ASSERT_COUNT value: "
                            f"{parts[1]}"
                        )

                    if count < 0:
                        fail(
                            f"Line {line_number}: "
                            "ASSERT_COUNT cannot be negative."
                        )

                    self.index += 1

                    file_patch.operations.append(
                        Operation(
                            kind="ASSERT_COUNT",
                            argument=count,
                            first=self.read_block(),
                            line_number=line_number,
                        )
                    )

                    continue

                if command_line in (
                    "ASSERT_CONTAINS",
                    "ASSERT_NOT_CONTAINS",
                    "DELETE",
                ):
                    self.index += 1

                    file_patch.operations.append(
                        Operation(
                            kind=command_line,
                            first=self.read_block(),
                            line_number=line_number,
                        )
                    )

                    continue

                if command_line in (
                    "REPLACE",
                    "REPLACE_ALL",
                    "INSERT_BEFORE",
                    "INSERT_AFTER",
                ):
                    self.index += 1

                    first = self.read_block()

                    self.expect("WITH")

                    second = self.read_block()

                    file_patch.operations.append(
                        Operation(
                            kind=command_line,
                            first=first,
                            second=second,
                            line_number=line_number,
                        )
                    )

                    continue

                if command_line == "CREATE_FILE":
                    self.index += 1

                    file_patch.operations.append(
                        Operation(
                            kind="CREATE_FILE",
                            first=self.read_block(),
                            line_number=line_number,
                        )
                    )

                    continue

                if command_line == "DELETE_FILE":
                    self.index += 1

                    file_patch.operations.append(
                        Operation(
                            kind="DELETE_FILE",
                            line_number=line_number,
                        )
                    )

                    continue

                fail(
                    f"Line {line_number}: "
                    f"unknown command '{command_line}'."
                )

            if not file_patch.operations:
                fail(
                    f"FILE {relative_path} contains no operations."
                )

            files.append(file_patch)

        self.skip_empty_and_comments()

        if not self.at_end():
            fail(
                f"Line {self.current_line_number()}: "
                "unexpected content after END_PATCH."
            )

        if not files:
            fail("Patch contains no FILE sections.")

        return PatchDocument(
            version=version,
            files=files,
        )


def require_existing(
    source: SourceFile,
    relative_path: Path,
    operation: Operation,
) -> None:
    if not source.existed:
        fail(
            f"{relative_path}:{operation.line_number}: "
            "target file does not exist."
        )

    if source.deleted:
        fail(
            f"{relative_path}:{operation.line_number}: "
            "file was already deleted earlier in this patch."
        )


def require_nonempty_needle(
    relative_path: Path,
    operation: Operation,
    needle: str,
) -> None:
    if needle == "":
        fail(
            f"{relative_path}:{operation.line_number}: "
            f"{operation.kind} cannot use an empty search block."
        )


def require_exactly_one(
    source: SourceFile,
    relative_path: Path,
    operation: Operation,
    needle: str,
) -> None:
    require_nonempty_needle(
        relative_path,
        operation,
        needle,
    )

    count = source.text.count(needle)

    if count != 1:
        fail(
            f"{relative_path}:{operation.line_number}: "
            f"{operation.kind} expected exactly 1 match, "
            f"found {count}."
        )


def apply_file_patch(
    patch: FilePatch,
    original: SourceFile,
) -> SourceFile:
    source = replace(original)
    created_in_patch = False

    for operation in patch.operations:
        kind = operation.kind

        if kind == "CREATE_FILE":
            if source.existed or created_in_patch:
                fail(
                    f"{patch.relative_path}:"
                    f"{operation.line_number}: "
                    "CREATE_FILE requires the file to not exist."
                )

            if source.deleted:
                fail(
                    f"{patch.relative_path}:"
                    f"{operation.line_number}: "
                    "CREATE_FILE cannot follow DELETE_FILE."
                )

            source.text = operation.first or ""
            source.deleted = False
            source.newline = "\n"
            source.had_bom = False
            created_in_patch = True
            continue

        if kind == "DELETE_FILE":
            require_existing(
                source,
                patch.relative_path,
                operation,
            )

            source.deleted = True
            continue

        require_existing(
            source,
            patch.relative_path,
            operation,
        )

        first = operation.first or ""

        if kind == "ASSERT_COUNT":
            require_nonempty_needle(
                patch.relative_path,
                operation,
                first,
            )

            actual = source.text.count(first)
            expected = operation.argument

            if actual != expected:
                fail(
                    f"{patch.relative_path}:"
                    f"{operation.line_number}: "
                    f"ASSERT_COUNT expected {expected}, "
                    f"found {actual}."
                )

            continue

        if kind == "ASSERT_CONTAINS":
            require_nonempty_needle(
                patch.relative_path,
                operation,
                first,
            )

            if first not in source.text:
                fail(
                    f"{patch.relative_path}:"
                    f"{operation.line_number}: "
                    "ASSERT_CONTAINS failed."
                )

            continue

        if kind == "ASSERT_NOT_CONTAINS":
            require_nonempty_needle(
                patch.relative_path,
                operation,
                first,
            )

            if first in source.text:
                fail(
                    f"{patch.relative_path}:"
                    f"{operation.line_number}: "
                    "ASSERT_NOT_CONTAINS failed."
                )

            continue

        if kind == "REPLACE":
            require_exactly_one(
                source,
                patch.relative_path,
                operation,
                first,
            )

            source.text = source.text.replace(
                first,
                operation.second or "",
                1,
            )
            continue

        if kind == "REPLACE_ALL":
            require_nonempty_needle(
                patch.relative_path,
                operation,
                first,
            )

            count = source.text.count(first)

            if count == 0:
                fail(
                    f"{patch.relative_path}:"
                    f"{operation.line_number}: "
                    "REPLACE_ALL found no matches."
                )

            source.text = source.text.replace(
                first,
                operation.second or "",
            )
            continue

        if kind == "INSERT_BEFORE":
            require_exactly_one(
                source,
                patch.relative_path,
                operation,
                first,
            )

            source.text = source.text.replace(
                first,
                (operation.second or "") + first,
                1,
            )
            continue

        if kind == "INSERT_AFTER":
            require_exactly_one(
                source,
                patch.relative_path,
                operation,
                first,
            )

            source.text = source.text.replace(
                first,
                first + (operation.second or ""),
                1,
            )
            continue

        if kind == "DELETE":
            require_exactly_one(
                source,
                patch.relative_path,
                operation,
                first,
            )

            source.text = source.text.replace(
                first,
                "",
                1,
            )
            continue

        fail(
            f"{patch.relative_path}:"
            f"{operation.line_number}: "
            f"unsupported operation '{kind}'."
        )

    return source


def source_changed(
    original: SourceFile,
    patched: SourceFile,
) -> bool:
    if patched.deleted:
        return original.existed

    if not original.existed:
        return True

    return (
        original.text != patched.text
        or original.had_bom != patched.had_bom
        or original.newline != patched.newline
    )


def print_diff(
    relative_path: Path,
    original: SourceFile,
    patched: SourceFile,
) -> None:
    old_text = original.text if original.existed else ""
    new_text = "" if patched.deleted else patched.text

    diff = difflib.unified_diff(
        old_text.splitlines(keepends=True),
        new_text.splitlines(keepends=True),
        fromfile=(
            f"a/{relative_path}"
            if original.existed
            else "/dev/null"
        ),
        tofile=(
            "/dev/null"
            if patched.deleted
            else f"b/{relative_path}"
        ),
    )

    output = "".join(diff)

    if output:
        print(
            output,
            end="" if output.endswith("\n") else "\n",
        )


def create_backup_directory() -> Path:
    timestamp = datetime.datetime.now().strftime(
        "%Y%m%d-%H%M%S-%f"
    )

    backup_dir = BACKUP_ROOT / timestamp

    backup_dir.mkdir(
        parents=True,
        exist_ok=False,
    )

    return backup_dir


def backup_existing_files(
    originals: dict[Path, SourceFile],
    changed_paths: list[Path],
    backup_dir: Path,
) -> None:
    for relative_path in changed_paths:
        source = originals[relative_path]

        if not source.existed:
            continue

        backup_path = backup_dir / relative_path

        backup_path.parent.mkdir(
            parents=True,
            exist_ok=True,
        )

        shutil.copy2(
            source.path,
            backup_path,
        )


def write_backup_manifest(
    originals: dict[Path, SourceFile],
    patched: dict[Path, SourceFile],
    changed_paths: list[Path],
    backup_dir: Path,
) -> None:
    lines = [
        "wizard.py backup manifest",
        "",
    ]

    for relative_path in changed_paths:
        original = originals[relative_path]
        result = patched[relative_path]

        if not original.existed and not result.deleted:
            action = "CREATE"
        elif original.existed and result.deleted:
            action = "DELETE"
        else:
            action = "MODIFY"

        lines.append(
            f"{action} {relative_path.as_posix()}"
        )

    lines.append("")

    (backup_dir / "manifest.txt").write_text(
        "\n".join(lines),
        encoding="utf-8",
        newline="\n",
    )


def prepare_temp_file(
    source: SourceFile,
) -> Path:
    source.path.parent.mkdir(
        parents=True,
        exist_ok=True,
    )

    fd, temporary_name = tempfile.mkstemp(
        prefix=f".{source.path.name}.wizard-",
        suffix=".tmp",
        dir=source.path.parent,
    )

    temporary_path = Path(temporary_name)

    try:
        with os.fdopen(fd, "wb") as file:
            file.write(
                encode_source(source)
            )

            file.flush()
            os.fsync(
                file.fileno()
            )

        if source.path.exists():
            mode = stat.S_IMODE(
                source.path.stat().st_mode
            )

            os.chmod(
                temporary_path,
                mode,
            )

        return temporary_path

    except Exception:
        try:
            temporary_path.unlink(
                missing_ok=True,
            )
        except OSError:
            pass

        raise


def restore_from_backup(
    originals: dict[Path, SourceFile],
    changed_paths: list[Path],
    backup_dir: Path,
) -> list[str]:
    errors: list[str] = []

    for relative_path in reversed(changed_paths):
        original = originals[relative_path]
        target = original.path

        try:
            if original.existed:
                backup_path = backup_dir / relative_path

                if not backup_path.is_file():
                    raise FileNotFoundError(
                        f"Backup file missing: {backup_path}"
                    )

                target.parent.mkdir(
                    parents=True,
                    exist_ok=True,
                )

                fd, temporary_name = tempfile.mkstemp(
                    prefix=f".{target.name}.wizard-restore-",
                    suffix=".tmp",
                    dir=target.parent,
                )

                os.close(fd)

                temporary_path = Path(
                    temporary_name
                )

                try:
                    shutil.copy2(
                        backup_path,
                        temporary_path,
                    )

                    os.replace(
                        temporary_path,
                        target,
                    )
                finally:
                    temporary_path.unlink(
                        missing_ok=True,
                    )

            else:
                target.unlink(
                    missing_ok=True,
                )

        except Exception as exc:
            errors.append(
                f"{relative_path}: {exc}"
            )

    return errors


def commit_changes(
    originals: dict[Path, SourceFile],
    patched: dict[Path, SourceFile],
    changed_paths: list[Path],
    backup_dir: Path,
) -> None:
    prepared: dict[Path, Path] = {}

    try:
        # Prepare every replacement before touching any source path.
        for relative_path in changed_paths:
            result = patched[relative_path]

            if result.deleted:
                continue

            prepared[relative_path] = prepare_temp_file(
                result
            )

        # Commit only after every temporary file was prepared successfully.
        for relative_path in changed_paths:
            result = patched[relative_path]

            if result.deleted:
                result.path.unlink()
                continue

            temporary_path = prepared.pop(
                relative_path
            )

            os.replace(
                temporary_path,
                result.path,
            )

    except Exception as exc:
        print(
            f"[ERROR] Commit failed: {exc}",
            file=sys.stderr,
        )

        print(
            "[INFO] Attempting rollback...",
            file=sys.stderr,
        )

        rollback_errors = restore_from_backup(
            originals,
            changed_paths,
            backup_dir,
        )

        if rollback_errors:
            print(
                "[ERROR] Rollback was incomplete:",
                file=sys.stderr,
            )

            for error in rollback_errors:
                print(
                    f"  {error}",
                    file=sys.stderr,
                )
        else:
            print(
                "[INFO] Rollback completed.",
                file=sys.stderr,
            )

        raise WizardError(
            "Patch commit failed."
        ) from exc

    finally:
        for temporary_path in prepared.values():
            try:
                temporary_path.unlink(
                    missing_ok=True,
                )
            except OSError:
                pass


def load_patch_file(path: Path) -> str:
    if not path.is_file():
        fail(
            f"Patch file not found: {path}"
        )

    raw = path.read_bytes()

    if raw.startswith(UTF8_BOM):
        raw = raw[len(UTF8_BOM):]

    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError as exc:
        fail(
            f"Patch file is not valid UTF-8: "
            f"{path}: {exc}"
        )


def preflight(
    document: PatchDocument,
) -> tuple[
    dict[Path, SourceFile],
    dict[Path, SourceFile],
    list[Path],
]:
    originals: dict[Path, SourceFile] = {}
    patched: dict[Path, SourceFile] = {}
    changed_paths: list[Path] = []

    for file_patch in document.files:
        relative_path = file_patch.relative_path

        original = read_source(
            relative_path
        )

        result = apply_file_patch(
            file_patch,
            original,
        )

        originals[relative_path] = original
        patched[relative_path] = result

        if source_changed(
            original,
            result,
        ):
            changed_paths.append(
                relative_path
            )

    return (
        originals,
        patched,
        changed_paths,
    )


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Apply a transactional Patch DSL file "
            "relative to wizard.py."
        )
    )

    parser.add_argument(
        "patch",
        type=Path,
        help=(
            "Patch DSL file, for example patch.txt"
        ),
    )

    parser.add_argument(
        "--check",
        action="store_true",
        help=(
            "Parse, validate and print the diff "
            "without modifying files."
        ),
    )

    return parser.parse_args()


def main() -> int:
    args = parse_arguments()

    try:
        patch_text = load_patch_file(
            args.patch
        )

        document = Parser(
            patch_text
        ).parse()

        originals, patched, changed_paths = preflight(
            document
        )

        print(
            f"[INFO] Patch DSL v{document.version}: "
            f"{len(document.files)} file section(s)."
        )

        if not changed_paths:
            print(
                "[SUCCESS] Patch is already satisfied; "
                "no files need to change."
            )

            return 0

        print(
            f"[INFO] {len(changed_paths)} file(s) will change:"
        )

        for relative_path in changed_paths:
            print(
                f"  {relative_path}"
            )

        print()

        for relative_path in changed_paths:
            print_diff(
                relative_path,
                originals[relative_path],
                patched[relative_path],
            )

        if args.check:
            print(
                "[SUCCESS] Check passed. "
                "No files were written."
            )

            return 0

        backup_dir = create_backup_directory()

        backup_existing_files(
            originals,
            changed_paths,
            backup_dir,
        )

        write_backup_manifest(
            originals,
            patched,
            changed_paths,
            backup_dir,
        )

        commit_changes(
            originals,
            patched,
            changed_paths,
            backup_dir,
        )

        print(
            f"[SUCCESS] Patch applied to "
            f"{len(changed_paths)} file(s)."
        )

        print(
            f"[INFO] Backup: {backup_dir}"
        )

        return 0

    except WizardError as exc:
        print(
            f"[ERROR] {exc}",
            file=sys.stderr,
        )

        return 1

    except KeyboardInterrupt:
        print(
            "\n[ERROR] Interrupted.",
            file=sys.stderr,
        )

        return 130


if __name__ == "__main__":
    sys.exit(
        main()
    )
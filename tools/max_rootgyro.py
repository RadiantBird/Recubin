#!/usr/bin/env python3

from pathlib import Path
import argparse
import shutil
import sys

import yaml


MAX_FLOAT = 3.4028234663852886e38


def walk(value):
    if isinstance(value, dict):
        yield value

        for child in value.values():
            if isinstance(child, (dict, list)):
                yield from walk(child)

    elif isinstance(value, list):
        for child in value:
            yield from walk(child)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("path")
    args = parser.parse_args()

    path = Path(args.path).resolve()

    if not path.exists():
        print(f"[ERROR] File not found: {path}")
        sys.exit(1)

    data = yaml.safe_load(
        path.read_text(encoding="utf-8")
    )

    found = 0

    for node in walk(data):
        if (
            node.get("ClassName") != "Gyro" or
            node.get("Name") != "RootGyro"
        ):
            continue

        props = node.setdefault(
            "Properties",
            {}
        )

        props["XMaxTorque"] = MAX_FLOAT
        props["YMaxTorque"] = MAX_FLOAT
        props["ZMaxTorque"] = MAX_FLOAT

        found += 1

    if found == 0:
        print("[ERROR] RootGyro was not found")
        sys.exit(1)

    backup = Path(
        str(path) + ".gyro.bak"
    )

    if not backup.exists():
        shutil.copy2(
            path,
            backup
        )

        print(
            f"[BACKUP] {backup}"
        )

    path.write_text(
        yaml.safe_dump(
            data,
            allow_unicode=True,
            sort_keys=False,
            default_flow_style=False,
            width=120,
        ),
        encoding="utf-8",
    )

    print(
        f"[OK] RootGyro patched: {path}"
    )

    print(
        f"[OK] Gyros changed: {found}"
    )

    print(
        f"[OK] MaxTorque: {MAX_FLOAT}"
    )


if __name__ == "__main__":
    main()
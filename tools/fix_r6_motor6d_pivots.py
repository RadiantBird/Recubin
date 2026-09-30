#!/usr/bin/env python3

from pathlib import Path
import math
import os
import shutil
import sys
import tempfile
import argparse

import yaml


ROOT = Path(__file__).resolve().parent

MIGRATION = ROOT / "migrate_character_rig_v2.py"
CHARACTER_RIG = ROOT / "src/Core/CharacterRig.cpp"


TOPOLOGY = [
    ("RootJoint", "Root", "Torso"),
    ("Neck", "Torso", "Head"),
    ("LeftShoulder", "Torso", "LeftArm"),
    ("RightShoulder", "Torso", "RightArm"),
    ("LeftHip", "Torso", "LeftLeg"),
    ("RightHip", "Torso", "RightLeg"),
]


JOINT_C1_POSITION = {
    "RootJoint": [0.0, 0.0, 0.0],
    "Neck": [0.0, 0.0, 0.0],

    # @RadiantBird 2026/09/13:
    # Restore the R6 rotation pivots which existed before Character Rig v2.
    "LeftShoulder": [0.0, 0.5, 0.0],
    "RightShoulder": [0.0, 0.5, 0.0],

    "LeftHip": [0.0, 1.0, 0.0],
    "RightHip": [0.0, 1.0, 0.0],
}


def fail(message):
    print(f"[ERROR] {message}")
    sys.exit(1)


def backup_once(path: Path, suffix=".pivot.bak"):
    backup = Path(str(path) + suffix)

    if not backup.exists():
        shutil.copy2(path, backup)
        print(f"[BACKUP] {backup}")

    return backup


def write_text_atomic(path: Path, text: str):
    fd, temp_name = tempfile.mkstemp(
        prefix=path.name + ".",
        suffix=".tmp",
        dir=path.parent,
        text=True,
    )

    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="") as file:
            file.write(text)

        os.replace(temp_name, path)

    except Exception:
        try:
            os.unlink(temp_name)
        except OSError:
            pass

        raise


def replace_once(text, old, new, description):
    count = text.count(old)

    if count == 0:
        fail(
            f"{description}: target text was not found.\n"
            "The source probably changed; refusing to guess."
        )

    if count != 1:
        fail(
            f"{description}: expected exactly one target, found {count}.\n"
            "Refusing to apply an ambiguous patch."
        )

    return text.replace(old, new, 1)


# ============================================================
# CFrame math for existing .rcbn repair
# ============================================================

def normalize_quaternion(q):
    if not isinstance(q, list) or len(q) != 4:
        raise ValueError(f"Invalid quaternion: {q!r}")

    q = [float(value) for value in q]

    length = math.sqrt(sum(value * value for value in q))

    if not math.isfinite(length) or length <= 1.0e-8:
        raise ValueError(f"Invalid quaternion length: {q!r}")

    return [value / length for value in q]


def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b

    return [
        aw * bx + ax * bw + ay * bz - az * by,
        aw * by - ax * bz + ay * bw + az * bx,
        aw * bz + ax * by - ay * bx + az * bw,
        aw * bw - ax * bx - ay * by - az * bz,
    ]


def qrot(q, v):
    inverse = [
        -q[0],
        -q[1],
        -q[2],
        q[3],
    ]

    result = qmul(
        qmul(
            q,
            [
                v[0],
                v[1],
                v[2],
                0.0,
            ],
        ),
        inverse,
    )

    return result[:3]


def identity_cframe():
    return {
        "Position": [0.0, 0.0, 0.0],
        "Rotation": [0.0, 0.0, 0.0, 1.0],
    }


def read_frame(node):
    props = node.get("Properties", {})

    position = props.get(
        "Position",
        [0.0, 0.0, 0.0],
    )

    rotation = props.get(
        "Rotation",
        [0.0, 0.0, 0.0, 1.0],
    )

    if not isinstance(position, list) or len(position) != 3:
        raise ValueError(
            f'Invalid Position on "{node.get("Name", "?")}"'
        )

    position = [
        float(value)
        for value in position
    ]

    rotation = normalize_quaternion(
        rotation
    )

    return position, rotation


def relative(a, b):
    ap, aq = a
    bp, bq = b

    inverse = [
        -aq[0],
        -aq[1],
        -aq[2],
        aq[3],
    ]

    relative_position = qrot(
        inverse,
        [
            bp[index] - ap[index]
            for index in range(3)
        ],
    )

    relative_rotation = normalize_quaternion(
        qmul(
            inverse,
            bq,
        )
    )

    return {
        "Position": relative_position,
        "Rotation": relative_rotation,
    }


def multiply_cframe(a, b):
    ap = a["Position"]
    aq = normalize_quaternion(
        a["Rotation"]
    )

    bp = b["Position"]
    bq = normalize_quaternion(
        b["Rotation"]
    )

    rotated_position = qrot(
        aq,
        bp,
    )

    return {
        "Position": [
            ap[index] + rotated_position[index]
            for index in range(3)
        ],
        "Rotation": normalize_quaternion(
            qmul(
                aq,
                bq,
            )
        ),
    }


def joint_c1(joint_name):
    result = identity_cframe()

    result["Position"] = list(
        JOINT_C1_POSITION[joint_name]
    )

    return result


def motor_bind_frames(
    joint_name,
    part0_frame,
    part1_frame,
):
    c1 = joint_c1(
        joint_name
    )

    # Motor6D bind invariant:
    #
    # Part0 * C0 == Part1 * C1
    #
    # therefore:
    #
    # C0 = inverse(Part0) * Part1 * C1
    #
    c0 = multiply_cframe(
        relative(
            part0_frame,
            part1_frame,
        ),
        c1,
    )

    return c0, c1


# ============================================================
# Patch migrate_character_rig_v2.py
# ============================================================

def patch_migration():
    if not MIGRATION.exists():
        fail(
            f"Missing migration script: {MIGRATION}"
        )

    text = MIGRATION.read_text(
        encoding="utf-8"
    )

    marker = "def r6_motor_bind_frames("

    if marker in text:
        print(
            "[SKIP] migrate_character_rig_v2.py "
            "already contains pivot patch"
        )
        return

    old_identity = '''def identity_cframe():
    return {
        "Position": [0.0, 0.0, 0.0],
        "Rotation": [0.0, 0.0, 0.0, 1.0],
    }


'''

    new_identity = '''def identity_cframe():
    return {
        "Position": [0.0, 0.0, 0.0],
        "Rotation": [0.0, 0.0, 0.0, 1.0],
    }


R6_JOINT_C1_POSITION = {
    "RootJoint": [0.0, 0.0, 0.0],
    "Neck": [0.0, 0.0, 0.0],
    "LeftShoulder": [0.0, 0.5, 0.0],
    "RightShoulder": [0.0, 0.5, 0.0],
    "LeftHip": [0.0, 1.0, 0.0],
    "RightHip": [0.0, 1.0, 0.0],
}


def multiply_cframe(a, b):
    ap = a["Position"]
    aq = quaternion_normalize(
        a["Rotation"],
        "left CFrame",
    )

    bp = b["Position"]
    bq = quaternion_normalize(
        b["Rotation"],
        "right CFrame",
    )

    rotated_position = qrot(aq, bp)

    return {
        "Position": [
            ap[index] + rotated_position[index]
            for index in range(3)
        ],
        "Rotation": quaternion_normalize(
            qmul(aq, bq),
            "multiplied CFrame",
        ),
    }


def r6_motor_bind_frames(
    joint_name,
    part0_frame,
    part1_frame,
):
    c1 = identity_cframe()

    c1["Position"] = list(
        R6_JOINT_C1_POSITION[joint_name]
    )

    # @RadiantBird 2026/09/13:
    # Preserve the visible bind pose while restoring the R6 joint pivot.
    #
    # Part0 * C0 == Part1 * C1
    #
    # Therefore C0 is derived after selecting the intended Part1-local
    # pivot instead of forcing C1 to identity.
    c0 = multiply_cframe(
        relative(
            part0_frame,
            part1_frame,
        ),
        c1,
    )

    return c0, c1


'''

    text = replace_once(
        text,
        old_identity,
        new_identity,
        "Insert Motor6D pivot helpers",
    )

    old_motor = '''        set_properties(
            motor,
            {
                "Part0": ref(part0),
                "Part1": ref(part1),
                "C0": relative(frames[part0], frames[part1]),
                "C1": identity_cframe(),
                "Transform": identity_cframe(),
                "Frequency": 10.0,
                "DampingRatio": 1.0,
            },
        )
'''

    new_motor = '''        c0, c1 = r6_motor_bind_frames(
            joint_name,
            frames[part0],
            frames[part1],
        )

        set_properties(
            motor,
            {
                "Part0": ref(part0),
                "Part1": ref(part1),
                "C0": c0,
                "C1": c1,
                "Transform": identity_cframe(),
                "Frequency": 10.0,
                "DampingRatio": 1.0,
            },
        )
'''

    text = replace_once(
        text,
        old_motor,
        new_motor,
        "Replace migration Motor6D bind generation",
    )

    backup_once(
        MIGRATION
    )

    write_text_atomic(
        MIGRATION,
        text,
    )

    print(
        "[OK] migrate_character_rig_v2.py"
    )


# ============================================================
# Patch CharacterRig.cpp fallback/default rig
# ============================================================

def patch_character_rig_cpp():
    if not CHARACTER_RIG.exists():
        fail(
            f"Missing CharacterRig.cpp: {CHARACTER_RIG}"
        )

    text = CHARACTER_RIG.read_text(
        encoding="utf-8"
    )

    marker = "r6MotorC1("

    if marker in text:
        print(
            "[SKIP] CharacterRig.cpp "
            "already contains pivot patch"
        )
        return

    old_helper_point = '''void calculateMotor6DBind(const CFrame& part0, const CFrame& part1,
                          CFrame& c0, CFrame& c1) {
    c0 = part0.inverse() * part1;
    c1 = CFrame();
}

'''

    new_helper_point = '''void calculateMotor6DBind(const CFrame& part0, const CFrame& part1,
                          CFrame& c0, CFrame& c1) {
    c0 = part0.inverse() * part1;
    c1 = CFrame();
}

static CFrame r6MotorC1(const std::string& jointName) {
    // @RadiantBird 2026/09/13:
    // Motor6D must preserve the original R6 rotation pivots.
    // C1 is the joint frame in Part1 local coordinates.
    if (
        jointName == "LeftShoulder" ||
        jointName == "RightShoulder"
    ) {
        return CFrame(0.0f, 0.5f, 0.0f);
    }

    if (
        jointName == "LeftHip" ||
        jointName == "RightHip"
    ) {
        return CFrame(0.0f, 1.0f, 0.0f);
    }

    return CFrame();
}

'''

    text = replace_once(
        text,
        old_helper_point,
        new_helper_point,
        "Insert CharacterRig R6 C1 helper",
    )

    old_bind = '''        CFrame c0;
        CFrame c1;
        calculateMotor6DBind(part0->getWorldCFrame(), part1->getWorldCFrame(), c0, c1);
        motor->setC0(c0);
        motor->setC1(c1);
'''

    new_bind = '''        const CFrame c1 =
            r6MotorC1(
                topology.jointName
            );

        const CFrame c0 =
            part0->getWorldCFrame().inverse() *
            part1->getWorldCFrame() *
            c1;

        motor->setC0(c0);
        motor->setC1(c1);
'''

    text = replace_once(
        text,
        old_bind,
        new_bind,
        "Replace default R6 Motor6D bind generation",
    )

    backup_once(
        CHARACTER_RIG
    )

    write_text_atomic(
        CHARACTER_RIG,
        text,
    )

    print(
        "[OK] src/Core/CharacterRig.cpp"
    )


# ============================================================
# Repair existing .rcbn files
# ============================================================

def walk_instances(value):
    if isinstance(value, dict):
        if isinstance(
            value.get("ClassName"),
            str,
        ):
            yield value

        for child in value.values():
            if isinstance(
                child,
                (dict, list),
            ):
                yield from walk_instances(
                    child
                )

    elif isinstance(value, list):
        for child in value:
            yield from walk_instances(
                child
            )


def repair_starter_character(starter):
    children = starter.get(
        "Children",
        [],
    )

    if not isinstance(children, list):
        return 0

    by_name = {}

    for child in children:
        if not isinstance(child, dict):
            continue

        name = child.get("Name")

        if isinstance(name, str):
            by_name[name] = child

    required_parts = {
        part
        for _, part0, part1 in TOPOLOGY
        for part in (part0, part1)
    }

    if not required_parts.issubset(
        by_name.keys()
    ):
        return 0

    frames = {
        name: read_frame(
            by_name[name]
        )
        for name in required_parts
    }

    changed = 0

    for joint_name, part0, part1 in TOPOLOGY:
        motor = by_name.get(
            joint_name
        )

        if not isinstance(motor, dict):
            continue

        if motor.get("ClassName") != "Motor6D":
            continue

        props = motor.setdefault(
            "Properties",
            {},
        )

        if not isinstance(props, dict):
            raise ValueError(
                f"{joint_name}.Properties is not a mapping"
            )

        c0, c1 = motor_bind_frames(
            joint_name,
            frames[part0],
            frames[part1],
        )

        if (
            props.get("C0") != c0 or
            props.get("C1") != c1
        ):
            props["C0"] = c0
            props["C1"] = c1
            changed += 1

    return changed


def repair_rcbn_file(path: Path):
    try:
        original_text = path.read_text(
            encoding="utf-8"
        )

        data = yaml.safe_load(
            original_text
        )

    except Exception as error:
        print(
            f"[WARN] Could not parse {path}: {error}"
        )
        return 0

    if data is None:
        return 0

    changed = 0

    for instance in walk_instances(
        data
    ):
        if (
            instance.get("ClassName") ==
            "StarterCharacter"
        ):
            changed += repair_starter_character(
                instance
            )

    if changed == 0:
        return 0

    backup_once(
        path
    )

    output = yaml.safe_dump(
        data,
        allow_unicode=True,
        sort_keys=False,
        default_flow_style=False,
        width=120,
    )

    write_text_atomic(
        path,
        output,
    )

    print(
        f"[OK] {path} "
        f"({changed} Motor6D pivots repaired)"
    )

    return changed


def repair_rcbn_target(target: Path):
    ignored_directories = {
        ".git",
        ".vs",
        "build",
        "Build",
        "out",
        "cmake-build-debug",
        "cmake-build-release",
    }

    target = target.resolve()

    if not target.exists():
        fail(
            f"Target does not exist: {target}"
        )

    if target.is_file():
        if target.suffix.lower() != ".rcbn":
            fail(
                f"Target file is not .rcbn: {target}"
            )

        files = [
            target
        ]

    elif target.is_dir():
        files = []

        for path in target.rglob("*.rcbn"):
            relative_parts = path.relative_to(
                target
            ).parts

            if any(
                part in ignored_directories
                for part in relative_parts
            ):
                continue

            if path.name.endswith(
                ".pivot.bak"
            ):
                continue

            files.append(
                path
            )

    else:
        fail(
            f"Unsupported target: {target}"
        )

    files_changed = 0
    motors_changed = 0

    print(
        f"[TARGET] {target}"
    )

    print(
        f"[RCBN] files found: {len(files)}"
    )

    print()

    for path in files:
        changed = repair_rcbn_file(
            path
        )

        if changed:
            files_changed += 1
            motors_changed += changed

    print()
    print(
        f"[RCBN] files changed: {files_changed}"
    )

    print(
        f"[RCBN] Motor6D pivots repaired: "
        f"{motors_changed}"
    )


def main():
    parser = argparse.ArgumentParser(
        description=(
            "Repair Recubin R6 Motor6D pivots "
            "and optionally repair a specific .rcbn file "
            "or directory."
        )
    )

    parser.add_argument(
        "target",
        nargs="?",
        default=None,
        help=(
            "A .rcbn file or directory to repair. "
            "If omitted, scans the repository root."
        ),
    )

    parser.add_argument(
        "--rcbn-only",
        action="store_true",
        help=(
            "Only repair .rcbn files; "
            "do not patch migrate_character_rig_v2.py "
            "or CharacterRig.cpp."
        ),
    )

    args = parser.parse_args()

    print(
        "=== Recubin R6 Motor6D pivot repair ==="
    )

    print()

    if not args.rcbn_only:
        patch_migration()
        patch_character_rig_cpp()
        print()

    target = (
        Path(args.target)
        if args.target is not None
        else ROOT
    )

    print(
        "Repairing .rcbn files..."
    )

    repair_rcbn_target(
        target
    )

    print()
    print(
        "======================================"
    )

    print(
        "R6 Motor6D pivot repair complete."
    )

    print(
        "======================================"
    )

    print()
    print(
        "Backups use the suffix:"
    )

    print(
        "  .pivot.bak"
    )

if __name__ == "__main__":
    main()
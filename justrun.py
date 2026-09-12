from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parent

HEADER = ROOT / "include/Core/Box3DPhysicsBackend.hpp"
SOURCE = ROOT / "src/Core/Box3DPhysicsBackend.cpp"

for path in (HEADER, SOURCE):
    if not path.exists():
        raise FileNotFoundError(path)

    backup = path.with_suffix(path.suffix + ".maintain-velocity.bak")
    shutil.copy2(path, backup)
    print(f"[BACKUP] {backup}")


# ------------------------------------------------------------
# Header
# ------------------------------------------------------------

header = HEADER.read_text(encoding="utf-8")

if "void applyMaintainedVelocities();" not in header:
    old = """    void applyForces();
    void applyGyroForces();"""

    new = """    void applyForces();
    void applyMaintainedVelocities();
    void applyGyroForces();"""

    if old not in header:
        raise RuntimeError(
            "Could not find applyForces/applyGyroForces declarations "
            "in Box3DPhysicsBackend.hpp"
        )

    header = header.replace(old, new, 1)
    HEADER.write_text(header, encoding="utf-8")
    print("[OK] Added applyMaintainedVelocities declaration")
else:
    print("[SKIP] Declaration already exists")


# ------------------------------------------------------------
# Source implementation
# ------------------------------------------------------------

source = SOURCE.read_text(encoding="utf-8")

if "void Box3DPhysicsBackend::applyMaintainedVelocities()" not in source:
    marker = """void Box3DPhysicsBackend::applyGyroForces() {"""

    if marker not in source:
        raise RuntimeError(
            "Could not find applyGyroForces() in Box3DPhysicsBackend.cpp"
        )

    implementation = r"""void Box3DPhysicsBackend::applyMaintainedVelocities() {
    std::set<std::uint64_t> visited;

    for (const BodyEntry& bodyEntry : m_bodies) {
        const b3BodyId id = bodyEntry.bodyId;

        if (
            B3_IS_NULL(id) ||
            !b3Body_IsValid(id) ||
            b3Body_GetType(id) != b3_dynamicBody
        ) {
            continue;
        }

        if (!visited.insert(b3StoreBodyId(id)).second) {
            continue;
        }

        bool maintainLinear = false;
        bool maintainAngular = false;

        Vector3 linearTarget;
        Vector3 angularTarget;

        Vector3 angularAxisMask = {
            1.0f,
            1.0f,
            1.0f
        };

        for (const BodyEntry& entry : m_bodies) {
            if (!idsEqual(entry.bodyId, id)) {
                continue;
            }

            auto member = entry.cube.lock();

            if (!member) {
                continue;
            }

            for (const auto& [name, child] : member->children) {
                (void)name;

                if (
                    !child ||
                    !child->IsA("Force")
                ) {
                    continue;
                }

                const auto* force =
                    static_cast<const Force*>(child.get());

                if (
                    !force->Enabled ||
                    !force->MaintainVelocity
                ) {
                    continue;
                }

                if (force->Torque) {
                    maintainAngular = true;
                    angularTarget = force->Value;
                    angularAxisMask = force->AxisMask;
                }
                else {
                    maintainLinear = true;
                    linearTarget = force->Value;
                }
            }
        }

        // @RadiantBird 2026/09/13:
        // MaintainVelocity is authoritative. Box3D contacts,
        // friction and constraints are allowed to solve normally,
        // then the requested maintained velocity is restored after
        // the solver has finished.
        if (maintainLinear) {
            b3Body_SetLinearVelocity(
                id,
                toB3Length(linearTarget)
            );
        }

        if (maintainAngular) {
            const b3Vec3 currentAngularVelocity =
                b3Body_GetAngularVelocity(id);

            b3Vec3 targetAngularVelocity =
                toB3Vector(angularTarget);

            if (angularAxisMask.x == 0.0f) {
                targetAngularVelocity.x =
                    currentAngularVelocity.x;
            }

            if (angularAxisMask.y == 0.0f) {
                targetAngularVelocity.y =
                    currentAngularVelocity.y;
            }

            if (angularAxisMask.z == 0.0f) {
                targetAngularVelocity.z =
                    currentAngularVelocity.z;
            }

            b3Body_SetAngularVelocity(
                id,
                targetAngularVelocity
            );
        }
    }
}

"""

    source = source.replace(
        marker,
        implementation + marker,
        1
    )

    SOURCE.write_text(source, encoding="utf-8")
    print("[OK] Added applyMaintainedVelocities implementation")
else:
    print("[SKIP] Implementation already exists")


# ------------------------------------------------------------
# Ensure post-solver call exists
# ------------------------------------------------------------

source = SOURCE.read_text(encoding="utf-8")

call = """        applyMaintainedVelocities();"""

if call not in source:
    old = """        b3World_Step(m_worldId, FIXED_STEP, SUB_STEPS);
        ++m_simulationTick;"""

    new = """        b3World_Step(m_worldId, FIXED_STEP, SUB_STEPS);

        applyMaintainedVelocities();

        ++m_simulationTick;"""

    if old not in source:
        raise RuntimeError(
            "Could not find b3World_Step() call site"
        )

    source = source.replace(old, new, 1)
    SOURCE.write_text(source, encoding="utf-8")
    print("[OK] Added post-solver MaintainVelocity call")
else:
    print("[SKIP] Post-solver call already exists")


print("[DONE]")
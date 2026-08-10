#include "include/Core/Physics.hpp"
#include "include/Core/Box3DPhysicsBackend.hpp"
#include "include/Core/PhysXPhysicsBackend.hpp"
#include "include/Instances/BaseCube.hpp"
#include "include/Instances/Attachment.hpp"
#include "include/Instances/BallSocket.hpp"
#include "include/Instances/Motor.hpp"
#include "include/Instances/NoCollision.hpp"
#include "include/Instances/Rod.hpp"
#include "include/Instances/Rope.hpp"
#include "include/Instances/Weld.hpp"
#include "include/Instances/Workspace.hpp"
#include "include/Util/Logger.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

PhysicsBackendType Physics::s_requestedBackend = PhysicsBackendType::Box3D;
std::function<void(BaseCube*, BaseCube*)> Physics::s_contactCallback;

namespace {
bool finiteVector(const Vector3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

bool finiteQuaternion(const Quaternion& value) {
    return std::isfinite(value.w) && std::isfinite(value.x) &&
           std::isfinite(value.y) && std::isfinite(value.z);
}

bool validQuaternion(const Quaternion& value) {
    if (!finiteQuaternion(value)) return false;
    const float lengthSquared = value.w * value.w + value.x * value.x +
        value.y * value.y + value.z * value.z;
    return std::isfinite(lengthSquared) && lengthSquared > 1.0e-12f;
}

bool validCubeDescriptor(const BaseCube& cube) {
    const bool baseValid = finiteVector(cube.Size) && cube.Size.x > 0.0f &&
           cube.Size.y > 0.0f && cube.Size.z > 0.0f &&
           finiteVector(cube.getWorldPosition()) &&
           validQuaternion(cube.getWorldCFrame().Rotation) &&
           std::isfinite(cube.MassDensity) && cube.MassDensity > 0.0f &&
           std::isfinite(cube.material.staticFriction) &&
           std::isfinite(cube.material.dynamicFriction) &&
           std::isfinite(cube.material.restitution) &&
           cube.material.staticFriction >= 0.0f &&
           cube.material.dynamicFriction >= 0.0f &&
           cube.material.restitution >= 0.0f;
    if (!baseValid) return false;
    if (cube.getPhysicsShape() != PhysicsShape::ConvexMesh) return true;
    const auto vertices = cube.getConvexVertices();
    return vertices.size() >= 4 &&
        std::all_of(vertices.begin(), vertices.end(), finiteVector);
}

bool validTerrainDescriptor(const PhysicsTerrainDescriptor& descriptor) {
    if (!finiteVector(descriptor.origin) ||
        !std::isfinite(descriptor.staticFriction) ||
        !std::isfinite(descriptor.dynamicFriction) ||
        !std::isfinite(descriptor.restitution) ||
        descriptor.staticFriction < 0.0f ||
        descriptor.dynamicFriction < 0.0f || descriptor.restitution < 0.0f)
        return false;
    if (!std::all_of(descriptor.vertices.begin(), descriptor.vertices.end(),
                     finiteVector))
        return false;
    if (descriptor.indices.size() % 3 != 0 ||
        !std::all_of(descriptor.indices.begin(), descriptor.indices.end(),
                     [&](std::uint32_t index) {
                         return index < descriptor.vertices.size();
                     }))
        return false;
    for (const auto& hull : descriptor.hulls) {
        if (hull.vertices.size() < 4 ||
            !std::all_of(hull.vertices.begin(), hull.vertices.end(),
                         finiteVector) ||
            !finiteVector(hull.localFrame.Position) ||
            !validQuaternion(hull.localFrame.Rotation))
            return false;
    }
    return true;
}
}

IPhysicsBackend::~IPhysicsBackend() = default;
std::uint64_t IPhysicsBackend::getSimulationTick() const {
    return m_simulationTick;
}
float IPhysicsBackend::getAccumulatorAlpha() const {
    return m_accumulatorAlpha;
}

Physics::Physics()
    : m_backendType(s_requestedBackend) {
    if (m_backendType == PhysicsBackendType::PhysX)
        m_backend = std::make_unique<PhysXPhysicsBackend>(this);
    else if (m_backendType == PhysicsBackendType::Box3D)
        m_backend = std::make_unique<Box3DPhysicsBackend>(this);
}

Physics::~Physics() = default;

bool Physics::configureBackendFromCommandLine(int argc, char* argv[]) {
    bool found = false;
    PhysicsBackendType requested = PhysicsBackendType::Box3D;

    for (int i = 1; i < argc; ++i) {
        const char* argument = argv[i];
        if (std::strncmp(argument, "--physics", 9) != 0) continue;
        if (std::strncmp(argument, "--physics=", 10) != 0 || argument[10] == '\0') {
            RCBN_ERROR("Invalid physics backend option: " << argument);
            return false;
        }

        PhysicsBackendType parsed;
        if (std::strcmp(argument + 10, "physx") == 0)
            parsed = PhysicsBackendType::PhysX;
        else if (std::strcmp(argument + 10, "box3d") == 0)
            parsed = PhysicsBackendType::Box3D;
        else {
            RCBN_ERROR("Invalid physics backend: " << (argument + 10));
            return false;
        }

        if (found && parsed != requested) {
            RCBN_ERROR("Conflicting physics backend options");
            return false;
        }
        requested = parsed;
        found = true;
    }

    s_requestedBackend = requested;
    return true;
}

void Physics::init() {
    m_available = m_backend && m_backend->init();
    if (!m_available) {
        RCBN_ERROR(
            "Requested physics backend failed to initialize: "
            << (m_backendType == PhysicsBackendType::Box3D ? "box3d" : "physx"));
    }
}

bool Physics::isAvailable() const {
    return m_available && m_backend && m_backend->isAvailable();
}

PhysicsBackendType Physics::getBackendType() const {
    return m_backendType;
}

std::uint64_t Physics::getSimulationTick() const {
    return isAvailable() ? m_backend->getSimulationTick() : 0;
}

float Physics::getAccumulatorAlpha() const {
    return isAvailable() ? m_backend->getAccumulatorAlpha() : 0.0f;
}

float Physics::getWaveTime() const {
    return static_cast<float>(getWavePhaseTicks() / 60.0);
}

double Physics::getWavePhaseTicks() const {
    return static_cast<double>(getSimulationTick()) +
        static_cast<double>(getAccumulatorAlpha()) + m_wavePhaseOffsetTicks;
}

void Physics::getSynchronizedSimulationClock(
    std::uint32_t& tick, float& alpha) const {
    constexpr double WRAP = 4294967296.0;
    double phase = std::fmod(getWavePhaseTicks(), WRAP);
    if (phase < 0.0) phase += WRAP;
    const double whole = std::floor(phase);
    tick = static_cast<std::uint32_t>(whole);
    alpha = static_cast<float>(phase - whole);
}

void Physics::synchronizeSimulationClock(
    std::uint32_t authoritativeTick, float authoritativeAlpha) {
    if (!std::isfinite(authoritativeAlpha)) return;
    authoritativeAlpha = std::clamp(authoritativeAlpha, 0.0f, 1.0f);
    const double rawLocalPhase = static_cast<double>(getSimulationTick()) +
        static_cast<double>(getAccumulatorAlpha());
    const double localPhase = rawLocalPhase + m_wavePhaseOffsetTicks;
    constexpr double WRAP = 4294967296.0;
    const double baseCycle = std::floor(localPhase / WRAP);
    double authoritativePhase = baseCycle * WRAP +
        static_cast<double>(authoritativeTick) +
        static_cast<double>(authoritativeAlpha);
    if (authoritativePhase - localPhase > WRAP * 0.5)
        authoritativePhase -= WRAP;
    else if (localPhase - authoritativePhase > WRAP * 0.5)
        authoritativePhase += WRAP;
    const double target = authoritativePhase - rawLocalPhase;
    if (!m_hasWavePhaseTarget ||
        std::abs(target - m_wavePhaseOffsetTicks) > 6.0) {
        m_wavePhaseOffsetTicks = target;
    }
    m_wavePhaseTargetOffsetTicks = target;
    m_hasWavePhaseTarget = true;
}

void Physics::resetSimulationClockSynchronization() {
    m_wavePhaseOffsetTicks = 0.0;
    m_wavePhaseTargetOffsetTicks = 0.0;
    m_hasWavePhaseTarget = false;
}

void Physics::makeSimulationClockAuthoritative() {
    m_hasWavePhaseTarget = false;
    m_wavePhaseTargetOffsetTicks = m_wavePhaseOffsetTicks;
}

void Physics::advanceWavePhaseCorrection(std::uint64_t simulatedSteps) {
    if (!m_hasWavePhaseTarget || simulatedSteps == 0) return;
    const double error = m_wavePhaseTargetOffsetTicks - m_wavePhaseOffsetTicks;
    const double maximum = 0.1 * static_cast<double>(simulatedSteps);
    m_wavePhaseOffsetTicks += std::clamp(error, -maximum, maximum);
}

bool Physics::ownsBody(const BaseCube& cube) const {
    return cube.m_physicsOwner == this;
}

void Physics::reconcileConstraints(Workspace& workspace) {
    std::vector<std::shared_ptr<Instance>> constraints;
    auto collect = [&](auto& self, const std::shared_ptr<Instance>& value) -> void {
        if (!value) return;
        if (value->IsA("Weld") || value->IsA("Rope") || value->IsA("Rod") ||
            value->IsA("BallSocket") || value->IsA("Motor") ||
            value->IsA("NoCollision"))
            constraints.push_back(value);
        for (const auto& [name, child] : value->getChildren()) {
            (void)name;
            self(self, child);
        }
    };
    for (const auto& [name, child] : workspace.getChildren()) {
        (void)name;
        collect(collect, child);
    }

    std::unordered_set<Instance*> live;
    live.reserve(constraints.size());
    for (const auto& value : constraints) live.insert(value.get());
    std::erase_if(m_constraintBindings, [&](const auto& entry) {
        return entry.second.constraint.expired() || !live.contains(entry.first);
    });
    std::erase_if(m_crossWorkspaceWarnings,
        [&](Instance* value) { return !live.contains(value); });
    std::erase_if(m_invalidBindingWarnings,
        [&](Instance* value) { return !live.contains(value); });

    auto sameFrame = [](const CFrame& first, const CFrame& second) {
        return first.Position == second.Position &&
            first.Rotation.w == second.Rotation.w &&
            first.Rotation.x == second.Rotation.x &&
            first.Rotation.y == second.Rotation.y &&
            first.Rotation.z == second.Rotation.z;
    };

    for (const auto& value : constraints) {
        std::shared_ptr<BaseCube> cube0;
        std::shared_ptr<BaseCube> cube1;
        std::shared_ptr<Attachment> attachment0;
        std::shared_ptr<Attachment> attachment1;
        PhysicsConstraintHandle handle;
        Vector3 axis;

        if (value->IsA("Weld")) {
            auto constraint = std::static_pointer_cast<Weld>(value);
            cube0 = constraint->m_cube0.lock();
            cube1 = constraint->m_cube1.lock();
            handle = constraint->m_constraintHandle;
        } else if (value->IsA("Rope")) {
            auto constraint = std::static_pointer_cast<Rope>(value);
            cube0 = constraint->m_cube0.lock();
            cube1 = constraint->m_cube1.lock();
            attachment0 = constraint->m_attachment0.lock();
            attachment1 = constraint->m_attachment1.lock();
            handle = constraint->m_constraintHandle;
        } else if (value->IsA("Rod")) {
            auto constraint = std::static_pointer_cast<Rod>(value);
            cube0 = constraint->m_cube0.lock();
            cube1 = constraint->m_cube1.lock();
            attachment0 = constraint->m_attachment0.lock();
            attachment1 = constraint->m_attachment1.lock();
            handle = constraint->m_constraintHandle;
        } else if (value->IsA("BallSocket")) {
            auto constraint = std::static_pointer_cast<BallSocket>(value);
            cube0 = constraint->m_cube0.lock();
            cube1 = constraint->m_cube1.lock();
            attachment0 = constraint->m_attachment0.lock();
            attachment1 = constraint->m_attachment1.lock();
            handle = constraint->m_constraintHandle;
        } else if (value->IsA("Motor")) {
            auto constraint = std::static_pointer_cast<Motor>(value);
            cube0 = constraint->m_cube0.lock();
            cube1 = constraint->m_cube1.lock();
            attachment0 = constraint->m_attachment0.lock();
            attachment1 = constraint->m_attachment1.lock();
            handle = constraint->m_constraintHandle;
            axis = constraint->Axis;
        } else if (value->IsA("NoCollision")) {
            auto constraint = std::static_pointer_cast<NoCollision>(value);
            cube0 = constraint->m_cube0.lock();
            cube1 = constraint->m_cube1.lock();
            handle = constraint->m_constraintHandle;
        }

        const bool endpointsInWorkspace = cube0 && cube1 &&
            cube0->findFirstAncestorWorkspace() == &workspace &&
            cube1->findFirstAncestorWorkspace() == &workspace;
        const bool bodiesOwned = endpointsInWorkspace && ownsBody(*cube0) &&
            ownsBody(*cube1) && m_backend->hasBody(*cube0) &&
            m_backend->hasBody(*cube1);

        if (!bodiesOwned) {
            if (handle) m_backend->removeConstraint(value);
            m_constraintBindings.erase(value.get());
            if (endpointsInWorkspace) {
                // body が同じ update のpending flushで作成される場合がある。
                // constraint も残し、body 生成後の同じ安全窓で接続する。
                workspace.registerConstraint(value);
                continue;
            }
            workspace.unregisterConstraint(value.get());
            const bool crossWorkspace = cube0 && cube1 &&
                (cube0->findFirstAncestorWorkspace() != &workspace ||
                 cube1->findFirstAncestorWorkspace() != &workspace);
            if (crossWorkspace && m_crossWorkspaceWarnings.insert(value.get()).second) {
                RCBN_WARN("Constraint \"" << value->Name
                          << "\" crosses Workspace boundaries; native binding disabled");
            }
            continue;
        }
        m_crossWorkspaceWarnings.erase(value.get());

        ConstraintBindingSnapshot current;
        current.constraint = value;
        current.cube0 = cube0.get();
        current.cube1 = cube1.get();
        current.body0 = cube0->m_bodyHandle;
        current.body1 = cube1->m_bodyHandle;
        current.attachment0 = attachment0.get();
        current.attachment1 = attachment1.get();
        current.localFrame0 = attachment0
            ? attachment0->relativeToAncestor(cube0.get()) : CFrame();
        current.localFrame1 = attachment1
            ? attachment1->relativeToAncestor(cube1.get()) : CFrame();
        current.axis = axis;

        const bool bindingFinite =
            finiteVector(current.localFrame0.Position) &&
            finiteQuaternion(current.localFrame0.Rotation) &&
            finiteVector(current.localFrame1.Position) &&
            finiteQuaternion(current.localFrame1.Rotation) &&
            (!value->IsA("Motor") ||
             (finiteVector(current.axis) && current.axis.length() > 1.0e-6f));
        if (!bindingFinite) {
            if (m_invalidBindingWarnings.insert(value.get()).second)
                RCBN_ERROR("Constraint binding is non-finite; old native state "
                           "retained: " << value->Name);
            continue;
        }
        m_invalidBindingWarnings.erase(value.get());

        if (!handle) {
            workspace.registerConstraint(value);
            m_constraintBindings[value.get()] = current;
            continue;
        }

        auto previous = m_constraintBindings.find(value.get());
        if (previous == m_constraintBindings.end()) {
            m_constraintBindings[value.get()] = current;
            continue;
        }
        const auto& old = previous->second;
        const bool bodyChanged = old.body0 != current.body0 ||
            old.body1 != current.body1;
        const bool changed = old.cube0 != current.cube0 ||
            old.cube1 != current.cube1 ||
            (!value->IsA("Weld") && bodyChanged) ||
            old.attachment0 != current.attachment0 ||
            old.attachment1 != current.attachment1 ||
            !sameFrame(old.localFrame0, current.localFrame0) ||
            !sameFrame(old.localFrame1, current.localFrame1) ||
            old.axis != current.axis;
        if (changed) {
            // topology/filter/frame 変更は固定step開始前の安全窓で一度だけ
            // 解除し、新しい binding を同じ update で生成する。
            m_backend->removeConstraint(value);
            workspace.registerConstraint(value);
            previous->second = current;
        }
    }
}

#define RCBN_PHYSICS_VOID(method, ...) \
    do { if (isAvailable()) m_backend->method(__VA_ARGS__); } while (false)

void Physics::update(Workspace& workspace, float dt) {
    if (!isAvailable()) return;
    reconcileConstraints(workspace);
    const std::uint64_t before = m_backend->getSimulationTick();
    m_backend->update(workspace, dt);
    advanceWavePhaseCorrection(m_backend->getSimulationTick() - before);
}
void Physics::stepOnce(float dt) {
    if (!isAvailable()) return;
    const std::uint64_t before = m_backend->getSimulationTick();
    m_backend->stepOnce(dt);
    advanceWavePhaseCorrection(m_backend->getSimulationTick() - before);
}
void Physics::syncAllCubes() { RCBN_PHYSICS_VOID(syncAllCubes); }
void Physics::syncWeldKinematics() { RCBN_PHYSICS_VOID(syncWeldKinematics); }
void Physics::moveWeldAssembly(const std::shared_ptr<BaseCube>& member, const CFrame& cframe) {
    RCBN_PHYSICS_VOID(moveWeldAssembly, member, cframe);
}

void Physics::createActor(const std::shared_ptr<BaseCube>& cube) {
    if (!isAvailable() || !cube) return;
    if (!validCubeDescriptor(*cube)) {
        RCBN_ERROR("Refusing invalid physics body descriptor: " << cube->Name);
        return;
    }
    if (cube->m_physicsOwner && cube->m_physicsOwner != this) {
        RCBN_ERROR("Refusing to create a body owned by another Physics world: "
                   << cube->Name);
        return;
    }
    cube->m_physicsOwner = this;
    m_backend->createActor(cube);
    if (!m_backend->hasBody(*cube)) cube->m_physicsOwner = nullptr;
}

void Physics::recreateActor(const std::shared_ptr<BaseCube>& cube) {
    if (!isAvailable() || !cube) return;
    if (!validCubeDescriptor(*cube)) {
        RCBN_ERROR("Refusing invalid replacement body descriptor; old body "
                   "retained: " << cube->Name);
        return;
    }
    if (cube->m_physicsOwner && cube->m_physicsOwner != this) {
        RCBN_ERROR("Refusing to recreate a body owned by another Physics world: "
                   << cube->Name);
        return;
    }
    cube->m_physicsOwner = this;
    m_backend->recreateActor(cube);
    if (!m_backend->hasBody(*cube)) cube->m_physicsOwner = nullptr;
}

void Physics::removeCube(const std::shared_ptr<BaseCube>& cube) {
    if (!isAvailable() || !cube ||
        (cube->m_physicsOwner && cube->m_physicsOwner != this)) return;
    m_backend->removeCube(cube);
}
void Physics::onCubeDestroyed(BaseCube& cube) {
    if (m_backend && ownsBody(cube)) m_backend->onCubeDestroyed(cube);
}
void Physics::clearCubes() { RCBN_PHYSICS_VOID(clearCubes); }

bool Physics::hasBody(const BaseCube& cube) const {
    return isAvailable() && ownsBody(cube) && m_backend->hasBody(cube);
}

bool Physics::sharesBody(const BaseCube& first, const BaseCube& second) const {
    return isAvailable() && ownsBody(first) && ownsBody(second) &&
        m_backend->sharesBody(first, second);
}
PhysicsBodyHandle Physics::getBodyHandle(const BaseCube& cube) const {
    return ownsBody(cube) ? cube.m_bodyHandle : PhysicsBodyHandle{};
}

CFrame Physics::getBodyWorldCFrame(const BaseCube& cube) const {
    return isAvailable() && ownsBody(cube)
        ? m_backend->getBodyWorldCFrame(cube) : CFrame();
}

void Physics::setBodyWorldCFrame(BaseCube& cube, const CFrame& cframe) {
    if (isAvailable() && ownsBody(cube))
        m_backend->setBodyWorldCFrame(cube, cframe);
}

Vector3 Physics::getLinearVelocity(const BaseCube& cube) const {
    return isAvailable() && ownsBody(cube)
        ? m_backend->getLinearVelocity(cube) : Vector3();
}

void Physics::setLinearVelocity(BaseCube& cube, const Vector3& velocity) {
    if (isAvailable() && ownsBody(cube)) m_backend->setLinearVelocity(cube, velocity);
}

void Physics::setAngularVelocity(BaseCube& cube, const Vector3& velocity) {
    if (isAvailable() && ownsBody(cube)) m_backend->setAngularVelocity(cube, velocity);
}

void Physics::setGravityEnabled(BaseCube& cube, bool enabled) {
    if (isAvailable() && ownsBody(cube)) m_backend->setGravityEnabled(cube, enabled);
}

void Physics::applyLockFlags(BaseCube& cube) {
    if (isAvailable() && ownsBody(cube)) m_backend->applyLockFlags(cube);
}
void Physics::refreshCollisionFilter(BaseCube& cube) {
    if (isAvailable() && ownsBody(cube)) m_backend->refreshCollisionFilter(cube);
}
void Physics::syncCube(BaseCube& cube) {
    if (isAvailable() && ownsBody(cube)) m_backend->syncCube(cube);
}
void Physics::enqueueResize(const std::shared_ptr<BaseCube>& cube) {
    if (isAvailable() && cube && ownsBody(*cube)) m_backend->enqueueResize(cube);
}
void Physics::enqueueSetRotation(const std::shared_ptr<BaseCube>& cube, Quaternion rotation) {
    if (isAvailable() && cube && ownsBody(*cube))
        m_backend->enqueueSetRotation(cube, rotation);
}

void Physics::createRope(const std::shared_ptr<Rope>& rope) { RCBN_PHYSICS_VOID(createRope, rope); }
void Physics::createRod(const std::shared_ptr<Rod>& rod) { RCBN_PHYSICS_VOID(createRod, rod); }
void Physics::createWeld(const std::shared_ptr<Weld>& weld, Workspace& workspace) {
    RCBN_PHYSICS_VOID(createWeld, weld, workspace);
}
void Physics::createMotor(const std::shared_ptr<Motor>& motor) { RCBN_PHYSICS_VOID(createMotor, motor); }
void Physics::createBallSocket(const std::shared_ptr<BallSocket>& ballSocket) {
    RCBN_PHYSICS_VOID(createBallSocket, ballSocket);
}
void Physics::createNoCollision(const std::shared_ptr<NoCollision>& noCollision) {
    RCBN_PHYSICS_VOID(createNoCollision, noCollision);
}
void Physics::removeConstraint(const std::shared_ptr<Instance>& constraint) {
    RCBN_PHYSICS_VOID(removeConstraint, constraint);
}
void Physics::updateConstraint(const std::shared_ptr<Instance>& constraint) {
    RCBN_PHYSICS_VOID(updateConstraint, constraint);
}

bool Physics::raycast(const Vector3& origin, const Vector3& direction, float maxDistance,
                      RaycastHit& hitResult, const BaseCube* ignoreCube) {
    if (!isAvailable() || !finiteVector(origin) || !finiteVector(direction) ||
        !std::isfinite(maxDistance) || maxDistance <= 0.0f) {
        hitResult = {};
        return false;
    }
    return m_backend->raycast(origin, direction, maxDistance, hitResult, ignoreCube);
}

BaseCube* Physics::findOverlapping(const BaseCube& cube, const std::string& className, float margin) const {
    return isAvailable() ? m_backend->findOverlapping(cube, className, margin) : nullptr;
}

void Physics::setGravity(const Vector3& gravity) {
    if (!finiteVector(gravity)) {
        RCBN_ERROR("Rejected non-finite Workspace gravity");
        return;
    }
    RCBN_PHYSICS_VOID(setGravity, gravity);
}
Vector3 Physics::getGravity() const {
    return isAvailable() ? m_backend->getGravity() : Vector3();
}

PhysicsTerrainHandle Physics::createTerrain(const PhysicsTerrainDescriptor& descriptor) {
    const bool empty = descriptor.vertices.empty() && descriptor.indices.empty() &&
        descriptor.hulls.empty();
    if (!isAvailable() || empty || !validTerrainDescriptor(descriptor)) {
        RCBN_ERROR("Rejected invalid Terrain physics descriptor");
        return {};
    }
    return m_backend->createTerrain(descriptor);
}

PhysicsTerrainHandle Physics::replaceTerrain(
    PhysicsTerrainHandle oldHandle,
    const PhysicsTerrainDescriptor& descriptor) {
    const bool empty = descriptor.vertices.empty() && descriptor.indices.empty() &&
        descriptor.hulls.empty();
    if (!isAvailable()) return oldHandle;
    if (!empty && !validTerrainDescriptor(descriptor)) {
        RCBN_ERROR("Rejected invalid Terrain replacement; old handle retained");
        return oldHandle;
    }
    return m_backend->replaceTerrain(oldHandle, descriptor);
}

void Physics::destroyTerrain(PhysicsTerrainHandle handle) {
    if (isAvailable()) m_backend->destroyTerrain(handle);
}

#undef RCBN_PHYSICS_VOID

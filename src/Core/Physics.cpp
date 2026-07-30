#include "include/Core/Physics.hpp"
#include "include/Core/Box3DPhysicsBackend.hpp"
#include "include/Core/PhysXPhysicsBackend.hpp"
#include "include/Instances/BaseCube.hpp"
#include "include/Util/Logger.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

PhysicsBackendType Physics::s_requestedBackend = PhysicsBackendType::Box3D;
std::function<void(BaseCube*, BaseCube*)> Physics::s_contactCallback;

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

#define RCBN_PHYSICS_VOID(method, ...) \
    do { if (isAvailable()) m_backend->method(__VA_ARGS__); } while (false)

void Physics::update(Workspace& workspace, float dt) {
    if (!isAvailable()) return;
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
    cube->m_physicsOwner = this;
    m_backend->createActor(cube);
}

void Physics::recreateActor(const std::shared_ptr<BaseCube>& cube) {
    if (!isAvailable() || !cube) return;
    cube->m_physicsOwner = this;
    m_backend->recreateActor(cube);
}

void Physics::removeCube(const std::shared_ptr<BaseCube>& cube) { RCBN_PHYSICS_VOID(removeCube, cube); }
void Physics::onCubeDestroyed(BaseCube& cube) {
    if (m_backend) m_backend->onCubeDestroyed(cube);
}
void Physics::clearCubes() { RCBN_PHYSICS_VOID(clearCubes); }

bool Physics::hasBody(const BaseCube& cube) const {
    return isAvailable() && m_backend->hasBody(cube);
}

bool Physics::sharesBody(const BaseCube& first, const BaseCube& second) const {
    return isAvailable() && m_backend->sharesBody(first, second);
}

CFrame Physics::getBodyWorldCFrame(const BaseCube& cube) const {
    return isAvailable() ? m_backend->getBodyWorldCFrame(cube) : CFrame();
}

void Physics::setBodyWorldCFrame(BaseCube& cube, const CFrame& cframe) {
    RCBN_PHYSICS_VOID(setBodyWorldCFrame, cube, cframe);
}

Vector3 Physics::getLinearVelocity(const BaseCube& cube) const {
    return isAvailable() ? m_backend->getLinearVelocity(cube) : Vector3();
}

void Physics::setLinearVelocity(BaseCube& cube, const Vector3& velocity) {
    RCBN_PHYSICS_VOID(setLinearVelocity, cube, velocity);
}

void Physics::setAngularVelocity(BaseCube& cube, const Vector3& velocity) {
    RCBN_PHYSICS_VOID(setAngularVelocity, cube, velocity);
}

void Physics::setGravityEnabled(BaseCube& cube, bool enabled) {
    RCBN_PHYSICS_VOID(setGravityEnabled, cube, enabled);
}

void Physics::applyLockFlags(BaseCube& cube) { RCBN_PHYSICS_VOID(applyLockFlags, cube); }
void Physics::syncCube(BaseCube& cube) { RCBN_PHYSICS_VOID(syncCube, cube); }
void Physics::enqueueResize(const std::shared_ptr<BaseCube>& cube) { RCBN_PHYSICS_VOID(enqueueResize, cube); }
void Physics::enqueueSetRotation(const std::shared_ptr<BaseCube>& cube, Quaternion rotation) {
    RCBN_PHYSICS_VOID(enqueueSetRotation, cube, rotation);
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
    if (!isAvailable()) {
        hitResult = {};
        return false;
    }
    return m_backend->raycast(origin, direction, maxDistance, hitResult, ignoreCube);
}

BaseCube* Physics::findOverlapping(const BaseCube& cube, const std::string& className, float margin) const {
    return isAvailable() ? m_backend->findOverlapping(cube, className, margin) : nullptr;
}

void Physics::setGravity(const Vector3& gravity) { RCBN_PHYSICS_VOID(setGravity, gravity); }
Vector3 Physics::getGravity() const {
    return isAvailable() ? m_backend->getGravity() : Vector3();
}

PhysicsTerrainHandle Physics::createTerrain(const PhysicsTerrainDescriptor& descriptor) {
    return isAvailable() ? m_backend->createTerrain(descriptor) : PhysicsTerrainHandle{};
}

PhysicsTerrainHandle Physics::replaceTerrain(
    PhysicsTerrainHandle oldHandle,
    const PhysicsTerrainDescriptor& descriptor) {
    return isAvailable() ? m_backend->replaceTerrain(oldHandle, descriptor) : oldHandle;
}

void Physics::destroyTerrain(PhysicsTerrainHandle handle) {
    if (isAvailable()) m_backend->destroyTerrain(handle);
}

#undef RCBN_PHYSICS_VOID

#pragma once

#include <include/Core/IPhysicsBackend.hpp>
#include <functional>
#include <memory>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

class Attachment;

class Physics {
private:
    static PhysicsBackendType s_requestedBackend;

    std::unique_ptr<IPhysicsBackend> m_backend;
    PhysicsBackendType m_backendType = PhysicsBackendType::PhysX;
    bool m_available = false;
    double m_wavePhaseOffsetTicks = 0.0;
    double m_wavePhaseTargetOffsetTicks = 0.0;
    bool m_hasWavePhaseTarget = false;

    struct ConstraintBindingSnapshot {
        std::weak_ptr<Instance> constraint;
        BaseCube* cube0 = nullptr;
        BaseCube* cube1 = nullptr;
        PhysicsBodyHandle body0;
        PhysicsBodyHandle body1;
        Attachment* attachment0 = nullptr;
        Attachment* attachment1 = nullptr;
        CFrame localFrame0;
        CFrame localFrame1;
        Vector3 axis;
    };
    std::unordered_map<Instance*, ConstraintBindingSnapshot> m_constraintBindings;
    std::unordered_set<Instance*> m_crossWorkspaceWarnings;

    void advanceWavePhaseCorrection(std::uint64_t simulatedSteps);
    bool ownsBody(const BaseCube& cube) const;
    void reconcileConstraints(Workspace& workspace);

public:
    static std::function<void(BaseCube*, BaseCube*)> s_contactCallback;

    Physics();
    ~Physics();

    static bool configureBackendFromCommandLine(int argc, char* argv[]);

    void init();
    bool isAvailable() const;
    PhysicsBackendType getBackendType() const;
    std::uint64_t getSimulationTick() const;
    float getAccumulatorAlpha() const;
    float getWaveTime() const;
    double getWavePhaseTicks() const;
    void getSynchronizedSimulationClock(
        std::uint32_t& tick, float& alpha) const;
    void synchronizeSimulationClock(
        std::uint32_t authoritativeTick, float authoritativeAlpha);
    void resetSimulationClockSynchronization();
    void makeSimulationClockAuthoritative();

    void update(Workspace& workspace, float dt);
    void stepOnce(float dt);
    void syncAllCubes();
    void syncWeldKinematics();
    void moveWeldAssembly(const std::shared_ptr<BaseCube>& member, const CFrame& worldCFrame);

    void createActor(const std::shared_ptr<BaseCube>& cube);
    void recreateActor(const std::shared_ptr<BaseCube>& cube);
    void removeCube(const std::shared_ptr<BaseCube>& cube);
    void onCubeDestroyed(BaseCube& cube);
    void clearCubes();

    bool hasBody(const BaseCube& cube) const;
    bool sharesBody(const BaseCube& first, const BaseCube& second) const;
    CFrame getBodyWorldCFrame(const BaseCube& cube) const;
    void setBodyWorldCFrame(BaseCube& cube, const CFrame& worldCFrame);
    Vector3 getLinearVelocity(const BaseCube& cube) const;
    void setLinearVelocity(BaseCube& cube, const Vector3& velocity);
    void setAngularVelocity(BaseCube& cube, const Vector3& velocity);
    void setGravityEnabled(BaseCube& cube, bool enabled);
    void applyLockFlags(BaseCube& cube);
    void syncCube(BaseCube& cube);

    void enqueueResize(const std::shared_ptr<BaseCube>& cube);
    void enqueueSetRotation(const std::shared_ptr<BaseCube>& cube, Quaternion rotation);

    void createRope(const std::shared_ptr<Rope>& rope);
    void createRod(const std::shared_ptr<Rod>& rod);
    void createWeld(const std::shared_ptr<Weld>& weld, Workspace& workspace);
    void createMotor(const std::shared_ptr<Motor>& motor);
    void createBallSocket(const std::shared_ptr<BallSocket>& ballSocket);
    void createNoCollision(const std::shared_ptr<NoCollision>& noCollision);
    void removeConstraint(const std::shared_ptr<Instance>& constraint);
    void updateConstraint(const std::shared_ptr<Instance>& constraint);

    bool raycast(const Vector3& origin, const Vector3& direction, float maxDistance,
                 RaycastHit& hitResult, const BaseCube* ignoreCube = nullptr);
    BaseCube* findOverlapping(const BaseCube& cube, const std::string& className,
                              float margin = 0.0f) const;

    void setGravity(const Vector3& gravity);
    Vector3 getGravity() const;

    PhysicsTerrainHandle createTerrain(const PhysicsTerrainDescriptor& descriptor);
    PhysicsTerrainHandle replaceTerrain(
        PhysicsTerrainHandle oldHandle,
        const PhysicsTerrainDescriptor& descriptor);
    void destroyTerrain(PhysicsTerrainHandle handle);
};

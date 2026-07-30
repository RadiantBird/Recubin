#pragma once

#include <include/Core/IPhysicsBackend.hpp>
#include <include/Instances/BaseCube.hpp>
#include <box3d/box3d.h>
#include <memory>
#include <set>
#include <unordered_map>
#include <vector>

class Physics;

class Box3DPhysicsBackend final : public IPhysicsBackend {
private:
    struct BodyEntry {
        std::weak_ptr<BaseCube> cube;
        BaseCube* cubeRaw = nullptr;
        b3BodyId bodyId = b3_nullBodyId;
    };

    struct ConstraintEntry {
        std::weak_ptr<Instance> constraint;
        PhysicsConstraintHandle handle;
        b3JointId jointId = b3_nullJointId;
    };

    struct NoCollisionEntry {
        std::weak_ptr<Instance> constraint;
        std::weak_ptr<BaseCube> cube0;
        std::weak_ptr<BaseCube> cube1;
        PhysicsConstraintHandle handle;
    };

    struct TerrainEntry {
        PhysicsTerrainHandle handle;
        b3BodyId bodyId = b3_nullBodyId;
        b3ShapeId shapeId = b3_nullShapeId;
        b3CompoundData* compound = nullptr;
        std::vector<b3MeshData*> meshes;
        std::vector<b3HullData*> hulls;
    };

    struct BuoyancyProxy {
        PhysicsShape shape = PhysicsShape::Box;
        Vector3 size;
        std::vector<Vector3> vertices;
        std::vector<std::vector<int>> faces;
        float volumeCorrection = 1.0f;
        float normalizedVolume = 0.0f;
        Vector3 normalizedCentroid;
    };

    using CubePair = std::pair<const BaseCube*, const BaseCube*>;

    Physics* m_facade = nullptr;
    b3WorldId m_worldId = b3_nullWorldId;
    float m_accumulator = 0.0f;
    std::uint64_t m_nextLogicalConstraintHandle = 1;
    std::vector<BodyEntry> m_bodies;
    std::vector<ConstraintEntry> m_constraints;
    std::vector<NoCollisionEntry> m_noCollisionEntries;
    std::shared_ptr<const std::set<CubePair>> m_noCollisionSnapshot;
    std::vector<TerrainEntry> m_terrains;
    std::unordered_map<const BaseCube*, BuoyancyProxy> m_buoyancyProxyCache;

    static bool customFilter(
        b3ShapeId shapeIdA, b3ShapeId shapeIdB, void* context);

    b3BodyId bodyId(const BaseCube& cube) const;
    void assignBody(BaseCube& cube, b3BodyId bodyId, const CFrame& localOffset);
    b3ShapeId createCubeShape(
        b3BodyId bodyId, const std::shared_ptr<BaseCube>& cube, const CFrame& localFrame);
    void destroyUniqueBodies();
    void rebuildNoCollisionSnapshot();
    void processContactEvents();
    void applyBuoyancy();
    void applyForces();
    const BuoyancyProxy* getBuoyancyProxy(const BaseCube& cube);
    void removeExpiredEntries();
    void rebuildAssembly(const std::vector<std::shared_ptr<BaseCube>>& assembly);
    void recreateConstraintsFor(const std::set<BaseCube*>& cubes);
    PhysicsConstraintHandle allocateLogicalConstraintHandle();
    ConstraintEntry* findConstraint(const std::shared_ptr<Instance>& constraint);
    void clearConstraintHandle(Instance& constraint);
    void createPendingConstraints(Workspace& workspace);
    static CubePair normalizePair(const BaseCube* first, const BaseCube* second);

public:
    explicit Box3DPhysicsBackend(Physics* facade);
    ~Box3DPhysicsBackend() override;

    bool init() override;
    bool isAvailable() const override;
    PhysicsBackendType getType() const override;

    void update(Workspace& workspace, float dt) override;
    void stepOnce(float dt) override;
    void syncAllCubes() override;
    void syncWeldKinematics() override;
    void moveWeldAssembly(
        const std::shared_ptr<BaseCube>& member, const CFrame& worldCFrame) override;

    void createActor(const std::shared_ptr<BaseCube>& cube) override;
    void recreateActor(const std::shared_ptr<BaseCube>& cube) override;
    void removeCube(const std::shared_ptr<BaseCube>& cube) override;
    void onCubeDestroyed(BaseCube& cube) override;
    void clearCubes() override;

    bool hasBody(const BaseCube& cube) const override;
    bool sharesBody(const BaseCube& first, const BaseCube& second) const override;
    CFrame getBodyWorldCFrame(const BaseCube& cube) const override;
    void setBodyWorldCFrame(BaseCube& cube, const CFrame& worldCFrame) override;
    Vector3 getLinearVelocity(const BaseCube& cube) const override;
    void setLinearVelocity(BaseCube& cube, const Vector3& velocity) override;
    void setAngularVelocity(BaseCube& cube, const Vector3& velocity) override;
    void setGravityEnabled(BaseCube& cube, bool enabled) override;
    void applyLockFlags(BaseCube& cube) override;
    void syncCube(BaseCube& cube) override;

    void enqueueResize(const std::shared_ptr<BaseCube>& cube) override;
    void enqueueSetRotation(
        const std::shared_ptr<BaseCube>& cube, Quaternion rotation) override;

    void createRope(const std::shared_ptr<Rope>& rope) override;
    void createRod(const std::shared_ptr<Rod>& rod) override;
    void createWeld(
        const std::shared_ptr<Weld>& weld, Workspace& workspace) override;
    void createMotor(const std::shared_ptr<Motor>& motor) override;
    void createBallSocket(const std::shared_ptr<BallSocket>& ballSocket) override;
    void createNoCollision(const std::shared_ptr<NoCollision>& noCollision) override;
    void removeConstraint(const std::shared_ptr<Instance>& constraint) override;
    void updateConstraint(const std::shared_ptr<Instance>& constraint) override;

    bool raycast(
        const Vector3& origin, const Vector3& direction, float maxDistance,
        RaycastHit& hitResult, const BaseCube* ignoreCube = nullptr) override;
    BaseCube* findOverlapping(
        const BaseCube& cube, const std::string& className,
        float margin = 0.0f) const override;

    void setGravity(const Vector3& gravity) override;
    Vector3 getGravity() const override;

    PhysicsTerrainHandle createTerrain(
        const PhysicsTerrainDescriptor& descriptor) override;
    PhysicsTerrainHandle replaceTerrain(
        PhysicsTerrainHandle oldHandle,
        const PhysicsTerrainDescriptor& descriptor) override;
    void destroyTerrain(PhysicsTerrainHandle handle) override;
};

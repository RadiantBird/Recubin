#pragma once

#include <include/Core/PhysicsTypes.hpp>
#include <include/Math/Vector3.hpp>
#include <memory>
#include <optional>
#include <vector>
#include <string>
#include <cstdint>

class BaseCube;
class Workspace;
class Instance;
class Rope;
class Rod;
class Weld;
class Motor;
class Motor6D;
class Gyro;
class BallSocket;
class NoCollision;
struct CFrame;
struct Quaternion;

struct RaycastHit {
    bool hit = false;
    float distance = 0.0f;
    Vector3 position;
    Vector3 normal;
    Instance* instance = nullptr;
};

// A convex box swept through the physics world. travelDistance is the amount
// the query shape moved; callers that need a semantic distance (for example,
// Root-center to floor) must derive it from position.
struct ShapeCastHit {
    bool hit = false;
    float travelDistance = 0.0f;
    Vector3 position;
    Vector3 normal;
    Instance* instance = nullptr;
};

class IPhysicsBackend {
protected:
    std::uint64_t m_simulationTick = 0;
    float m_accumulatorAlpha = 0.0f;

public:
    virtual ~IPhysicsBackend();

    virtual bool init() = 0;
    virtual bool isAvailable() const = 0;
    virtual PhysicsBackendType getType() const = 0;
    std::uint64_t getSimulationTick() const;
    float getAccumulatorAlpha() const;

    virtual void update(Workspace& workspace, float dt) = 0;
    virtual void stepOnce(float dt) = 0;
    virtual void syncAllCubes() = 0;
    virtual void moveWeldAssembly(const std::shared_ptr<BaseCube>& member, const CFrame& worldCFrame) = 0;

    virtual void createActor(const std::shared_ptr<BaseCube>& cube) = 0;
    virtual void recreateActor(const std::shared_ptr<BaseCube>& cube) = 0;
    virtual void removeCube(const std::shared_ptr<BaseCube>& cube) = 0;
    virtual void onCubeDestroyed(BaseCube& cube) = 0;
    virtual void clearCubes() = 0;

    virtual bool hasBody(const BaseCube& cube) const = 0;
    virtual bool sharesBody(const BaseCube& first, const BaseCube& second) const = 0;
    virtual CFrame getBodyWorldCFrame(const BaseCube& cube) const = 0;
    virtual void setBodyWorldCFrame(BaseCube& cube, const CFrame& worldCFrame) = 0;
    virtual Vector3 getLinearVelocity(const BaseCube& cube) const = 0;
    virtual Vector3 getAngularVelocity(const BaseCube&) const {
        return Vector3();
    }
    // Returns and clears the maximum contact impact accumulated for this body
    // since the previous physics update.
    virtual float consumeContactImpact(const BaseCube&) {
        return 0.0f;
    }
    virtual std::optional<float> getBodyMass(const BaseCube&) const {
        return std::nullopt;
    }
    virtual void setLinearVelocity(BaseCube& cube, const Vector3& velocity) = 0;
    virtual void setAngularVelocity(BaseCube& cube, const Vector3& velocity) = 0;
    virtual void setGravityEnabled(BaseCube& cube, bool enabled) = 0;
    virtual void applyLockFlags(BaseCube& cube) = 0;
    virtual void refreshCollisionFilter(BaseCube& cube) = 0;
    virtual std::size_t getTouchSensorShapeCount() const { return 0; }
    virtual void syncCube(BaseCube& cube) = 0;

    virtual void enqueueResize(const std::shared_ptr<BaseCube>& cube) = 0;
    virtual void enqueueSetRotation(const std::shared_ptr<BaseCube>& cube, Quaternion rotation) = 0;

    virtual void createRope(const std::shared_ptr<Rope>& rope) = 0;
    virtual void createRod(const std::shared_ptr<Rod>& rod) = 0;
    virtual void createWeld(const std::shared_ptr<Weld>& weld, Workspace& workspace) = 0;
    virtual void createMotor(const std::shared_ptr<Motor>& motor) = 0;
    virtual void createMotor6D(const std::shared_ptr<Motor6D>& motor) = 0;
    virtual void createGyro(const std::shared_ptr<Gyro>& gyro) = 0;
    virtual void createBallSocket(const std::shared_ptr<BallSocket>& ballSocket) = 0;
    virtual void createNoCollision(const std::shared_ptr<NoCollision>& noCollision) = 0;
    virtual void removeConstraint(const std::shared_ptr<Instance>& constraint) = 0;
    virtual void updateConstraint(const std::shared_ptr<Instance>& constraint) = 0;

    virtual bool raycast(
        const Vector3& origin,
        const Vector3& direction,
        float maxDistance,
        RaycastHit& hitResult,
        const Instance* excludeRoot
    ) = 0;

    // Current Box3D queries can filter multiple roots during traversal.
    // The default keeps the retired backend's existing single-root contract.
    virtual bool raycastExcluding(
        const Vector3& origin,
        const Vector3& direction,
        float maxDistance,
        RaycastHit& hitResult,
        const std::vector<const Instance*>& excludeRoots
    ) {
        const Instance* root = excludeRoots.empty() ? nullptr : excludeRoots.front();
        return raycast(origin, direction, maxDistance, hitResult, root);
    }

    virtual bool shapeCastBox(
        const CFrame& startFrame,
        const Vector3& size,
        const Vector3& direction,
        float maxDistance,
        ShapeCastHit& hitResult,
        const Instance* excludeRoot,
        float minimumNormalY = 0.0f
    );
    
    virtual BaseCube* findOverlapping(const BaseCube& cube, const std::string& className,
                                      float margin = 0.0f) const = 0;

    virtual void setGravity(const Vector3& gravity) = 0;
    virtual Vector3 getGravity() const = 0;

    virtual PhysicsTerrainHandle createTerrain(const PhysicsTerrainDescriptor& descriptor) = 0;
    virtual PhysicsTerrainHandle replaceTerrain(
        PhysicsTerrainHandle oldHandle,
        const PhysicsTerrainDescriptor& descriptor) = 0;
    virtual void destroyTerrain(PhysicsTerrainHandle handle) = 0;
};

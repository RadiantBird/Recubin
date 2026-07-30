#include <include/Core/Box3DPhysicsBackend.hpp>
#include <include/Core/Physics.hpp>
#include <include/Instances/Attachment.hpp>
#include <include/Instances/BallSocket.hpp>
#include <include/Instances/BaseCube.hpp>
#include <include/Instances/Force.hpp>
#include <include/Instances/LiquidCube.hpp>
#include <include/Instances/Motor.hpp>
#include <include/Instances/NoCollision.hpp>
#include <include/Instances/Rod.hpp>
#include <include/Instances/Rope.hpp>
#include <include/Instances/Weld.hpp>
#include <include/Instances/Workspace.hpp>
#include <include/Util/Logger.hpp>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <unordered_set>

namespace {
constexpr float STUDS_PER_METER = 20.0f;
constexpr float METERS_PER_STUD = 1.0f / STUDS_PER_METER;
constexpr float DENSITY_TO_MKS = 8000.0f;
constexpr float TORQUE_TO_MKS = 1.0f / 400.0f;
constexpr float FIXED_STEP = 1.0f / 60.0f;
constexpr int SUB_STEPS = 4;
constexpr int MAX_STEPS = 10;
constexpr float CLIP_EPSILON = 1.0e-5f;

struct FacePolyhedron {
    std::vector<std::vector<Vector3>> faces;
};

struct Plane {
    Vector3 normal;
    float offset = 0.0f;
};

bool finiteVector(const Vector3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

void orientFaces(
    const std::vector<Vector3>& vertices, std::vector<std::vector<int>>& faces) {
    Vector3 center;
    for (const Vector3& vertex : vertices) center = center + vertex;
    if (!vertices.empty()) center = center / static_cast<float>(vertices.size());
    for (auto& face : faces) {
        if (face.size() < 3) continue;
        const Vector3 normal = Vector3::Cross(
            vertices[face[1]] - vertices[face[0]],
            vertices[face[2]] - vertices[face[0]]);
        if (Vector3::Dot(normal, center - vertices[face[0]]) > 0.0f)
            std::reverse(face.begin(), face.end());
    }
}

FacePolyhedron makeFacePolyhedron(
    const std::vector<Vector3>& vertices,
    const std::vector<std::vector<int>>& indices) {
    FacePolyhedron result;
    for (const auto& face : indices) {
        std::vector<Vector3> polygon;
        polygon.reserve(face.size());
        for (int index : face) {
            if (index >= 0 && static_cast<size_t>(index) < vertices.size())
                polygon.push_back(vertices[static_cast<size_t>(index)]);
        }
        if (polygon.size() >= 3) result.faces.push_back(std::move(polygon));
    }
    return result;
}

bool samePoint(const Vector3& first, const Vector3& second) {
    const Vector3 delta = first - second;
    return Vector3::Dot(delta, delta) <= CLIP_EPSILON * CLIP_EPSILON;
}

FacePolyhedron clipPolyhedron(const FacePolyhedron& input, const Plane& plane) {
    FacePolyhedron result;
    std::vector<Vector3> cap;
    for (const auto& face : input.faces) {
        if (face.size() < 3) continue;
        std::vector<Vector3> clipped;
        Vector3 previous = face.back();
        float previousDistance =
            Vector3::Dot(plane.normal, previous) - plane.offset;
        bool previousInside = previousDistance <= CLIP_EPSILON;
        for (const Vector3& current : face) {
            const float currentDistance =
                Vector3::Dot(plane.normal, current) - plane.offset;
            const bool currentInside = currentDistance <= CLIP_EPSILON;
            if (currentInside != previousInside) {
                const float denominator = previousDistance - currentDistance;
                if (std::abs(denominator) > CLIP_EPSILON) {
                    const float amount = std::clamp(
                        previousDistance / denominator, 0.0f, 1.0f);
                    const Vector3 intersection =
                        previous + (current - previous) * amount;
                    if (finiteVector(intersection)) {
                        clipped.push_back(intersection);
                        bool duplicate = false;
                        for (const Vector3& existing : cap)
                            if (samePoint(existing, intersection)) {
                                duplicate = true;
                                break;
                            }
                        if (!duplicate) cap.push_back(intersection);
                    }
                }
            }
            if (currentInside) clipped.push_back(current);
            previous = current;
            previousDistance = currentDistance;
            previousInside = currentInside;
        }
        if (clipped.size() >= 3) result.faces.push_back(std::move(clipped));
    }

    if (cap.size() >= 3) {
        Vector3 center;
        for (const Vector3& point : cap) center = center + point;
        center = center / static_cast<float>(cap.size());
        Vector3 axis = std::abs(plane.normal.x) < 0.8f
            ? Vector3(1.0f, 0.0f, 0.0f) : Vector3(0.0f, 1.0f, 0.0f);
        Vector3 tangent = Vector3::Cross(axis, plane.normal).normalize();
        Vector3 bitangent = Vector3::Cross(plane.normal, tangent);
        std::sort(cap.begin(), cap.end(), [&](const Vector3& first, const Vector3& second) {
            const Vector3 a = first - center;
            const Vector3 b = second - center;
            return std::atan2(Vector3::Dot(a, bitangent), Vector3::Dot(a, tangent)) <
                   std::atan2(Vector3::Dot(b, bitangent), Vector3::Dot(b, tangent));
        });
        if (Vector3::Dot(
                Vector3::Cross(cap[1] - cap[0], cap[2] - cap[0]),
                plane.normal) < 0.0f)
            std::reverse(cap.begin(), cap.end());
        result.faces.push_back(std::move(cap));
    }
    return result;
}

bool volumeAndCentroid(
    const FacePolyhedron& polyhedron, float& volume, Vector3& centroid) {
    double signedVolume = 0.0;
    Vector3 weighted;
    for (const auto& face : polyhedron.faces) {
        if (face.size() < 3) continue;
        const Vector3& first = face[0];
        for (size_t index = 1; index + 1 < face.size(); ++index) {
            const Vector3& second = face[index];
            const Vector3& third = face[index + 1];
            const double tetrahedron = static_cast<double>(
                Vector3::Dot(first, Vector3::Cross(second, third))) / 6.0;
            if (!std::isfinite(tetrahedron)) return false;
            signedVolume += tetrahedron;
            weighted = weighted +
                (first + second + third) * static_cast<float>(tetrahedron * 0.25);
        }
    }
    if (!std::isfinite(signedVolume) ||
        std::abs(signedVolume) <= static_cast<double>(CLIP_EPSILON)) {
        volume = 0.0f;
        centroid = Vector3();
        return false;
    }
    centroid = weighted / static_cast<float>(signedVolume);
    volume = static_cast<float>(std::abs(signedVolume));
    return finiteVector(centroid) && std::isfinite(volume);
}

std::vector<Plane> planesFromPolyhedron(const FacePolyhedron& polyhedron) {
    std::vector<Plane> result;
    for (const auto& face : polyhedron.faces) {
        if (face.size() < 3) continue;
        Vector3 normal =
            Vector3::Cross(face[1] - face[0], face[2] - face[0]).normalize();
        if (normal.length() <= CLIP_EPSILON || !finiteVector(normal)) continue;
        result.push_back({normal, Vector3::Dot(normal, face[0])});
    }
    return result;
}

FacePolyhedron makeLiquidPrism(float time, bool secondTriangle) {
    const Vector3 top[4] = {
        {-0.5f, 0.5f + LiquidCube::waveHeight(-0.5f, 0.5f, time), 0.5f},
        {-0.5f, 0.5f + LiquidCube::waveHeight(-0.5f, -0.5f, time), -0.5f},
        {0.5f, 0.5f + LiquidCube::waveHeight(0.5f, -0.5f, time), -0.5f},
        {0.5f, 0.5f + LiquidCube::waveHeight(0.5f, 0.5f, time), 0.5f},
    };
    const int triangle[2][3] = {{0, 1, 2}, {0, 2, 3}};
    const int* selected = triangle[secondTriangle ? 1 : 0];
    std::vector<Vector3> vertices;
    vertices.reserve(6);
    for (int index = 0; index < 3; ++index)
        vertices.push_back(top[selected[index]]);
    for (int index = 0; index < 3; ++index) {
        Vector3 bottom = top[selected[index]];
        bottom.y = -0.5f;
        vertices.push_back(bottom);
    }
    std::vector<std::vector<int>> faces = {
        {0, 1, 2}, {3, 5, 4},
        {0, 3, 4, 1}, {1, 4, 5, 2}, {2, 5, 3, 0},
    };
    orientFaces(vertices, faces);
    return makeFacePolyhedron(vertices, faces);
}

b3Vec3 toB3Vector(const Vector3& value) {
    return {value.x, value.y, value.z};
}

b3Vec3 toB3Length(const Vector3& value) {
    return {
        value.x * METERS_PER_STUD,
        value.y * METERS_PER_STUD,
        value.z * METERS_PER_STUD,
    };
}

b3Pos toB3Position(const Vector3& value) {
    return {
        value.x * METERS_PER_STUD,
        value.y * METERS_PER_STUD,
        value.z * METERS_PER_STUD,
    };
}

Vector3 fromB3Vector(b3Vec3 value) {
    return {value.x, value.y, value.z};
}

Vector3 fromB3Length(b3Vec3 value) {
    return {
        value.x * STUDS_PER_METER,
        value.y * STUDS_PER_METER,
        value.z * STUDS_PER_METER,
    };
}

Vector3 fromB3Position(b3Pos value) {
    return {
        static_cast<float>(value.x * STUDS_PER_METER),
        static_cast<float>(value.y * STUDS_PER_METER),
        static_cast<float>(value.z * STUDS_PER_METER),
    };
}

b3Quat toB3Quaternion(const Quaternion& value) {
    return {{value.x, value.y, value.z}, value.w};
}

Quaternion fromB3Quaternion(b3Quat value) {
    return {value.s, value.v.x, value.v.y, value.v.z};
}

b3Transform toB3Transform(const CFrame& value) {
    return {toB3Length(value.Position), toB3Quaternion(value.Rotation)};
}

CFrame fromB3Transform(b3Transform value) {
    return CFrame(fromB3Length(value.p), fromB3Quaternion(value.q));
}

CFrame bodyWorldFrame(b3BodyId bodyId) {
    return CFrame(
        fromB3Position(b3Body_GetPosition(bodyId)),
        fromB3Quaternion(b3Body_GetRotation(bodyId)));
}

b3MotionLocks toB3Locks(PhysicsLockFlags flags) {
    b3MotionLocks locks = {};
    locks.linearX = hasPhysicsLockFlag(flags, PhysicsLockFlags::LinearX);
    locks.linearY = hasPhysicsLockFlag(flags, PhysicsLockFlags::LinearY);
    locks.linearZ = hasPhysicsLockFlag(flags, PhysicsLockFlags::LinearZ);
    locks.angularX = hasPhysicsLockFlag(flags, PhysicsLockFlags::AngularX);
    locks.angularY = hasPhysicsLockFlag(flags, PhysicsLockFlags::AngularY);
    locks.angularZ = hasPhysicsLockFlag(flags, PhysicsLockFlags::AngularZ);
    return locks;
}

b3SurfaceMaterial toB3Material(const Material& material) {
    b3SurfaceMaterial result = b3DefaultSurfaceMaterial();
    result.friction = material.dynamicFriction;
    result.restitution = material.restitution;
    return result;
}

Quaternion rotationFromZ(const Vector3& directionValue) {
    const Vector3 from(0.0f, 0.0f, 1.0f);
    Vector3 direction = directionValue.normalize();
    if (direction.length() < 1.0e-5f) direction = from;
    const float dot = std::clamp(Vector3::Dot(from, direction), -1.0f, 1.0f);
    if (dot > 0.999999f) return Quaternion();
    if (dot < -0.999999f) return Quaternion(0.0f, 0.0f, 1.0f, 0.0f);
    Vector3 cross = Vector3::Cross(from, direction);
    const float scale = std::sqrt((1.0f + dot) * 2.0f);
    const float inverse = 1.0f / scale;
    return Quaternion(
        scale * 0.5f,
        cross.x * inverse,
        cross.y * inverse,
        cross.z * inverse);
}

bool idsEqual(b3BodyId first, b3BodyId second) {
    return B3_IS_NON_NULL(first) && B3_ID_EQUALS(first, second);
}

CFrame attachmentFrame(
    const CFrame& cubeFrame, const std::weak_ptr<Attachment>& attachment,
    const BaseCube* cube) {
    auto value = attachment.lock();
    return value ? cubeFrame * value->relativeToAncestor(cube) : cubeFrame;
}
}

Box3DPhysicsBackend::Box3DPhysicsBackend(Physics* facade)
    : m_facade(facade),
      m_noCollisionSnapshot(std::make_shared<const std::set<CubePair>>()) {}

Box3DPhysicsBackend::~Box3DPhysicsBackend() {
    clearCubes();
    while (!m_terrains.empty()) destroyTerrain(m_terrains.back().handle);
    if (B3_IS_NON_NULL(m_worldId)) {
        b3DestroyWorld(m_worldId);
        m_worldId = b3_nullWorldId;
    }
}

bool Box3DPhysicsBackend::init() {
    if (B3_IS_NON_NULL(m_worldId)) return b3World_IsValid(m_worldId);
    b3WorldDef definition = b3DefaultWorldDef();
    definition.enableContinuous = true;
    definition.userData = this;
    m_worldId = b3CreateWorld(&definition);
    if (B3_IS_NULL(m_worldId) || !b3World_IsValid(m_worldId)) {
        RCBN_ERROR("Box3D world initialization failed");
        m_worldId = b3_nullWorldId;
        return false;
    }
    b3World_SetCustomFilterCallback(m_worldId, &Box3DPhysicsBackend::customFilter, this);
    return true;
}

bool Box3DPhysicsBackend::isAvailable() const {
    return B3_IS_NON_NULL(m_worldId) && b3World_IsValid(m_worldId);
}

PhysicsBackendType Box3DPhysicsBackend::getType() const {
    return PhysicsBackendType::Box3D;
}

b3BodyId Box3DPhysicsBackend::bodyId(const BaseCube& cube) const {
    if (!cube.m_bodyHandle) return b3_nullBodyId;
    const b3BodyId result = b3LoadBodyId(cube.m_bodyHandle.value);
    return b3Body_IsValid(result) ? result : b3_nullBodyId;
}

void Box3DPhysicsBackend::assignBody(
    BaseCube& cube, b3BodyId value, const CFrame& localOffset) {
    cube.m_bodyHandle = {B3_IS_NON_NULL(value) ? b3StoreBodyId(value) : 0};
    cube.m_compoundLocalOffset = localOffset;
    cube.m_physicsOwner = B3_IS_NON_NULL(value) ? m_facade : nullptr;
}

bool Box3DPhysicsBackend::hasBody(const BaseCube& cube) const {
    return B3_IS_NON_NULL(bodyId(cube));
}

bool Box3DPhysicsBackend::sharesBody(
    const BaseCube& first, const BaseCube& second) const {
    return idsEqual(bodyId(first), bodyId(second));
}

CFrame Box3DPhysicsBackend::getBodyWorldCFrame(const BaseCube& cube) const {
    const b3BodyId id = bodyId(cube);
    return B3_IS_NON_NULL(id) ? bodyWorldFrame(id) : CFrame();
}

void Box3DPhysicsBackend::setBodyWorldCFrame(
    BaseCube& cube, const CFrame& worldCFrame) {
    const b3BodyId id = bodyId(cube);
    if (B3_IS_NULL(id)) return;
    b3Body_SetTransform(id, toB3Position(worldCFrame.Position),
                       toB3Quaternion(worldCFrame.Rotation));
}

Vector3 Box3DPhysicsBackend::getLinearVelocity(const BaseCube& cube) const {
    const b3BodyId id = bodyId(cube);
    return B3_IS_NON_NULL(id)
        ? fromB3Length(b3Body_GetLinearVelocity(id)) : Vector3();
}

void Box3DPhysicsBackend::setLinearVelocity(
    BaseCube& cube, const Vector3& velocity) {
    const b3BodyId id = bodyId(cube);
    if (B3_IS_NON_NULL(id)) b3Body_SetLinearVelocity(id, toB3Length(velocity));
}

void Box3DPhysicsBackend::setAngularVelocity(
    BaseCube& cube, const Vector3& velocity) {
    const b3BodyId id = bodyId(cube);
    if (B3_IS_NON_NULL(id)) b3Body_SetAngularVelocity(id, toB3Vector(velocity));
}

void Box3DPhysicsBackend::setGravityEnabled(BaseCube& cube, bool enabled) {
    const b3BodyId id = bodyId(cube);
    if (B3_IS_NON_NULL(id)) b3Body_SetGravityScale(id, enabled ? 1.0f : 0.0f);
}

void Box3DPhysicsBackend::applyLockFlags(BaseCube& cube) {
    const b3BodyId id = bodyId(cube);
    if (B3_IS_NON_NULL(id)) b3Body_SetMotionLocks(id, toB3Locks(cube.LockFlags));
}

b3ShapeId Box3DPhysicsBackend::createCubeShape(
    b3BodyId id, const std::shared_ptr<BaseCube>& cube, const CFrame& localFrame) {
    if (!cube || !cube->CanCollide || B3_IS_NULL(id)) return b3_nullShapeId;

    b3ShapeDef definition = b3DefaultShapeDef();
    definition.userData = cube.get();
    definition.baseMaterial = toB3Material(cube->material);
    definition.density = std::max(cube->MassDensity, 0.01f) * DENSITY_TO_MKS;
    definition.enableCustomFiltering = true;
    definition.enableContactEvents = true;
    definition.updateBodyMass = true;

    if (cube->getPhysicsShape() == PhysicsShape::Sphere) {
        b3Sphere sphere = {
            toB3Length(localFrame.Position),
            std::max(cube->Size.x * 0.5f * METERS_PER_STUD, B3_LINEAR_SLOP),
        };
        return b3CreateSphereShape(id, &definition, &sphere);
    }

    if (cube->getPhysicsShape() == PhysicsShape::ConvexMesh) {
        const auto source = cube->getConvexVertices();
        if (source.empty()) return b3_nullShapeId;
        std::vector<b3Vec3> points;
        points.reserve(source.size());
        for (const Vector3& point : source) {
            points.push_back(toB3Length(point * cube->Size));
        }
        b3HullData* hull = b3CreateHull(
            points.data(), static_cast<int>(points.size()), 64);
        if (!hull) return b3_nullShapeId;
        const b3ShapeId shapeId = b3CreateTransformedHullShape(
            id, &definition, hull, toB3Transform(localFrame), b3Vec3_one);
        b3DestroyHull(hull);
        return shapeId;
    }

    const b3BoxHull box = b3MakeBoxHull(
        std::max(cube->Size.x * 0.5f * METERS_PER_STUD, B3_LINEAR_SLOP),
        std::max(cube->Size.y * 0.5f * METERS_PER_STUD, B3_LINEAR_SLOP),
        std::max(cube->Size.z * 0.5f * METERS_PER_STUD, B3_LINEAR_SLOP));
    return b3CreateTransformedHullShape(
        id, &definition, &box.base, toB3Transform(localFrame), b3Vec3_one);
}

const Box3DPhysicsBackend::BuoyancyProxy*
Box3DPhysicsBackend::getBuoyancyProxy(const BaseCube& cube) {
    const PhysicsShape shape = cube.getPhysicsShape();
    auto cached = m_buoyancyProxyCache.find(&cube);
    if (cached != m_buoyancyProxyCache.end() &&
        cached->second.shape == shape && cached->second.size == cube.Size)
        return &cached->second;

    BuoyancyProxy proxy;
    proxy.shape = shape;
    proxy.size = cube.Size;
    if (shape == PhysicsShape::Box) {
        proxy.vertices = {
            {-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f},
            {0.5f, 0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f},
            {-0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, 0.5f},
            {0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f},
        };
        proxy.faces = {
            {0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4},
            {3, 7, 6, 2}, {0, 4, 7, 3}, {1, 2, 6, 5},
        };
    } else if (shape == PhysicsShape::ConvexMesh) {
        const auto source = cube.getConvexVertices();
        std::vector<b3Vec3> points;
        points.reserve(source.size());
        for (const Vector3& point : source)
            if (finiteVector(point)) points.push_back(toB3Vector(point));
        b3HullData* hull = points.size() >= 4
            ? b3CreateHull(points.data(), static_cast<int>(points.size()), 64)
            : nullptr;
        if (hull) {
            const b3Vec3* hullPoints = b3GetHullPoints(hull);
            const b3HullHalfEdge* edges = b3GetHullEdges(hull);
            const b3HullFace* faces = b3GetHullFaces(hull);
            for (int index = 0; index < hull->vertexCount; ++index)
                proxy.vertices.push_back(fromB3Vector(hullPoints[index]));
            for (int faceIndex = 0; faceIndex < hull->faceCount; ++faceIndex) {
                std::vector<int> face;
                const uint8_t first = faces[faceIndex].edge;
                uint8_t edge = first;
                do {
                    face.push_back(edges[edge].origin);
                    edge = edges[edge].next;
                } while (edge != first && face.size() <=
                         static_cast<size_t>(hull->edgeCount));
                if (face.size() >= 3) proxy.faces.push_back(std::move(face));
            }
            b3DestroyHull(hull);
        }
    } else if (shape == PhysicsShape::Sphere) {
        const float golden = (1.0f + std::sqrt(5.0f)) * 0.5f;
        proxy.vertices = {
            {-1, golden, 0}, {1, golden, 0}, {-1, -golden, 0}, {1, -golden, 0},
            {0, -1, golden}, {0, 1, golden}, {0, -1, -golden}, {0, 1, -golden},
            {golden, 0, -1}, {golden, 0, 1}, {-golden, 0, -1}, {-golden, 0, 1},
        };
        for (Vector3& vertex : proxy.vertices)
            vertex = vertex.normalize() * 0.5f;
        proxy.faces = {
            {0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},
            {1,5,9},{5,11,4},{11,10,2},{10,7,6},{7,1,8},
            {3,9,4},{3,4,2},{3,2,6},{3,6,8},{3,8,9},
            {4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,1},
        };
        for (int subdivision = 0; subdivision < 2; ++subdivision) {
            std::unordered_map<std::uint64_t, int> midpointCache;
            std::vector<std::vector<int>> subdivided;
            auto midpoint = [&](int first, int second) {
                const std::uint32_t low =
                    static_cast<std::uint32_t>(std::min(first, second));
                const std::uint32_t high =
                    static_cast<std::uint32_t>(std::max(first, second));
                const std::uint64_t key =
                    (static_cast<std::uint64_t>(low) << 32) | high;
                auto found = midpointCache.find(key);
                if (found != midpointCache.end()) return found->second;
                const int index = static_cast<int>(proxy.vertices.size());
                proxy.vertices.push_back(
                    ((proxy.vertices[first] + proxy.vertices[second]) * 0.5f)
                        .normalize() * 0.5f);
                midpointCache[key] = index;
                return index;
            };
            for (const auto& face : proxy.faces) {
                const int a = midpoint(face[0], face[1]);
                const int b = midpoint(face[1], face[2]);
                const int c = midpoint(face[2], face[0]);
                subdivided.push_back({face[0], a, c});
                subdivided.push_back({face[1], b, a});
                subdivided.push_back({face[2], c, b});
                subdivided.push_back({a, b, c});
            }
            proxy.faces = std::move(subdivided);
        }
    }

    orientFaces(proxy.vertices, proxy.faces);
    volumeAndCentroid(
        makeFacePolyhedron(proxy.vertices, proxy.faces),
        proxy.normalizedVolume, proxy.normalizedCentroid);
    if (shape == PhysicsShape::Sphere && proxy.normalizedVolume > CLIP_EPSILON) {
        const float analyticalSphereVolume =
            4.0f / 3.0f * pi * 0.5f * 0.5f * 0.5f;
        proxy.volumeCorrection =
            analyticalSphereVolume / proxy.normalizedVolume;
    }
    if (proxy.vertices.empty() || proxy.faces.empty() ||
        !(proxy.normalizedVolume > CLIP_EPSILON))
        return nullptr;
    auto inserted =
        m_buoyancyProxyCache.insert_or_assign(&cube, std::move(proxy)).first;
    return &inserted->second;
}

void Box3DPhysicsBackend::createActor(const std::shared_ptr<BaseCube>& cube) {
    if (!cube || hasBody(*cube) || !isAvailable()) return;
    const CFrame world = cube->getWorldCFrame();
    b3BodyDef definition = b3DefaultBodyDef();
    definition.type = cube->Anchored ? b3_kinematicBody : b3_dynamicBody;
    definition.position = toB3Position(world.Position);
    definition.rotation = toB3Quaternion(world.Rotation);
    definition.userData = cube.get();
    definition.motionLocks = toB3Locks(cube->LockFlags);
    definition.isBullet = cube->CollisionDetection == CCDMode::Bullet;
    const b3BodyId id = b3CreateBody(m_worldId, &definition);
    if (B3_IS_NULL(id)) return;
    createCubeShape(id, cube, CFrame());
    assignBody(*cube, id, CFrame());
    cube->m_weldKinematic = false;
    m_bodies.push_back({cube, cube.get(), id});
}

void Box3DPhysicsBackend::recreateActor(const std::shared_ptr<BaseCube>& cube) {
    if (!cube) return;
    m_buoyancyProxyCache.erase(cube.get());
    const b3BodyId oldId = bodyId(*cube);
    if (B3_IS_NULL(oldId)) {
        createActor(cube);
        return;
    }

    std::vector<std::shared_ptr<BaseCube>> shared;
    for (const BodyEntry& entry : m_bodies) {
        if (!idsEqual(entry.bodyId, oldId)) continue;
        if (auto value = entry.cube.lock()) shared.push_back(value);
    }
    if (shared.size() > 1) {
        rebuildAssembly(shared);
        return;
    }

    const Vector3 linear = getLinearVelocity(*cube);
    const Vector3 angular = fromB3Vector(b3Body_GetAngularVelocity(oldId));
    removeCube(cube);
    createActor(cube);
    setLinearVelocity(*cube, linear);
    setAngularVelocity(*cube, angular);
}

void Box3DPhysicsBackend::removeCube(const std::shared_ptr<BaseCube>& cube) {
    if (!cube) return;
    m_buoyancyProxyCache.erase(cube.get());
    std::vector<std::shared_ptr<Instance>> attachedConstraints;
    for (const ConstraintEntry& entry : m_constraints) {
        auto value = entry.constraint.lock();
        if (!value) continue;
        std::shared_ptr<BaseCube> first;
        std::shared_ptr<BaseCube> second;
        if (value->IsA("Weld")) {
            auto c = std::static_pointer_cast<Weld>(value);
            first = c->m_cube0.lock(); second = c->m_cube1.lock();
        } else if (value->IsA("Rope")) {
            auto c = std::static_pointer_cast<Rope>(value);
            first = c->m_cube0.lock(); second = c->m_cube1.lock();
        } else if (value->IsA("Rod")) {
            auto c = std::static_pointer_cast<Rod>(value);
            first = c->m_cube0.lock(); second = c->m_cube1.lock();
        } else if (value->IsA("BallSocket")) {
            auto c = std::static_pointer_cast<BallSocket>(value);
            first = c->m_cube0.lock(); second = c->m_cube1.lock();
        } else if (value->IsA("Motor")) {
            auto c = std::static_pointer_cast<Motor>(value);
            first = c->m_cube0.lock(); second = c->m_cube1.lock();
        }
        if (first == cube || second == cube) attachedConstraints.push_back(value);
    }
    for (const NoCollisionEntry& entry : m_noCollisionEntries) {
        auto value = entry.constraint.lock();
        if (value && (entry.cube0.lock() == cube || entry.cube1.lock() == cube))
            attachedConstraints.push_back(value);
    }
    for (const auto& value : attachedConstraints) removeConstraint(value);

    const b3BodyId oldId = bodyId(*cube);
    assignBody(*cube, b3_nullBodyId, CFrame());
    cube->m_weldKinematic = false;

    m_bodies.erase(
        std::remove_if(m_bodies.begin(), m_bodies.end(),
            [&](const BodyEntry& entry) {
                return entry.cubeRaw == cube.get() || entry.cube.lock() == cube;
            }),
        m_bodies.end());
    if (B3_IS_NULL(oldId)) return;

    std::vector<std::shared_ptr<BaseCube>> survivors;
    for (BodyEntry& entry : m_bodies) {
        if (!idsEqual(entry.bodyId, oldId)) continue;
        if (auto value = entry.cube.lock()) survivors.push_back(value);
    }
    b3DestroyBody(oldId);
    for (const auto& value : survivors) assignBody(*value, b3_nullBodyId, CFrame());
    for (BodyEntry& entry : m_bodies) {
        if (idsEqual(entry.bodyId, oldId)) entry.bodyId = b3_nullBodyId;
    }
    for (const auto& value : survivors) createActor(value);
}

void Box3DPhysicsBackend::onCubeDestroyed(BaseCube& cube) {
    m_buoyancyProxyCache.erase(&cube);
    const b3BodyId oldId = bodyId(cube);
    if (B3_IS_NON_NULL(oldId)) {
        const int shapeCount = b3Body_GetShapeCount(oldId);
        std::vector<b3ShapeId> shapes(shapeCount);
        b3Body_GetShapes(oldId, shapes.data(), shapeCount);
        for (b3ShapeId shapeId : shapes) {
            if (b3Shape_GetUserData(shapeId) == &cube)
                b3Shape_SetUserData(shapeId, nullptr);
        }
        if (b3Body_GetUserData(oldId) == &cube)
            b3Body_SetUserData(oldId, nullptr);
    }
    assignBody(cube, b3_nullBodyId, CFrame());
    cube.m_weldKinematic = false;
    m_bodies.erase(
        std::remove_if(m_bodies.begin(), m_bodies.end(),
            [&](const BodyEntry& entry) { return entry.cubeRaw == &cube; }),
        m_bodies.end());
    if (B3_IS_NULL(oldId)) return;
    std::vector<std::shared_ptr<BaseCube>> survivors;
    for (const BodyEntry& entry : m_bodies) {
        if (!idsEqual(entry.bodyId, oldId)) continue;
        if (auto value = entry.cube.lock()) survivors.push_back(value);
    }
    if (survivors.empty()) {
        if (b3Body_IsValid(oldId)) b3DestroyBody(oldId);
    } else {
        rebuildAssembly(survivors);
    }
}

void Box3DPhysicsBackend::destroyUniqueBodies() {
    std::set<std::uint64_t> destroyed;
    for (BodyEntry& entry : m_bodies) {
        if (B3_IS_NULL(entry.bodyId)) continue;
        const std::uint64_t stored = b3StoreBodyId(entry.bodyId);
        if (destroyed.insert(stored).second && b3Body_IsValid(entry.bodyId))
            b3DestroyBody(entry.bodyId);
        if (auto cube = entry.cube.lock()) {
            assignBody(*cube, b3_nullBodyId, CFrame());
            cube->m_weldKinematic = false;
        }
        entry.bodyId = b3_nullBodyId;
    }
    m_bodies.clear();
}

void Box3DPhysicsBackend::clearCubes() {
    for (ConstraintEntry& entry : m_constraints) {
        if (auto value = entry.constraint.lock()) clearConstraintHandle(*value);
        if (B3_IS_NON_NULL(entry.jointId) && b3Joint_IsValid(entry.jointId))
            b3DestroyJoint(entry.jointId, false);
    }
    m_constraints.clear();
    for (NoCollisionEntry& entry : m_noCollisionEntries) {
        if (auto value = entry.constraint.lock()) clearConstraintHandle(*value);
    }
    m_noCollisionEntries.clear();
    rebuildNoCollisionSnapshot();
    destroyUniqueBodies();
    m_buoyancyProxyCache.clear();
}

void Box3DPhysicsBackend::syncCube(BaseCube& cube) {
    const b3BodyId id = bodyId(cube);
    if (B3_IS_NULL(id)) return;
    if (cube.Anchored) {
        const CFrame cubeWorld = cube.getWorldCFrame();
        const CFrame bodyTarget = cubeWorld * cube.m_compoundLocalOffset.inverse();
        b3WorldTransform target = {
            toB3Position(bodyTarget.Position),
            toB3Quaternion(bodyTarget.Rotation),
        };
        if (cube.m_weldKinematic)
            b3Body_SetTransform(id, target.p, target.q);
        else
            b3Body_SetTargetTransform(id, target, FIXED_STEP, true);
        return;
    }
    cube.setWorldCFrame(bodyWorldFrame(id) * cube.m_compoundLocalOffset);
}

void Box3DPhysicsBackend::syncAllCubes() {
    for (BodyEntry& entry : m_bodies) {
        if (auto cube = entry.cube.lock()) syncCube(*cube);
    }
}

void Box3DPhysicsBackend::syncWeldKinematics() {
    for (BodyEntry& entry : m_bodies) {
        auto cube = entry.cube.lock();
        if (cube && cube->m_weldKinematic && cube->Anchored) syncCube(*cube);
    }
    for (BodyEntry& entry : m_bodies) {
        auto cube = entry.cube.lock();
        if (cube && cube->m_weldKinematic && !cube->Anchored) syncCube(*cube);
    }
}

void Box3DPhysicsBackend::moveWeldAssembly(
    const std::shared_ptr<BaseCube>& member, const CFrame& worldCFrame) {
    if (!member) return;

    std::vector<std::shared_ptr<BaseCube>> assembly{member};
    if (auto* workspace = member->findFirstAncestorWorkspace())
        assembly = Weld::collectAssembly(member, *workspace);

    const CFrame before = member->getWorldCFrame();
    const CFrame delta = worldCFrame * before.inverse();
    for (const auto& cube : assembly)
        if (cube) cube->setWorldCFrame(delta * cube->getWorldCFrame());

    const b3BodyId id = bodyId(*member);
    if (B3_IS_NULL(id)) return;
    const CFrame target = worldCFrame * member->m_compoundLocalOffset.inverse();
    b3Body_SetTransform(id, toB3Position(target.Position), toB3Quaternion(target.Rotation));
    b3Body_SetLinearVelocity(id, b3Vec3_zero);
    b3Body_SetAngularVelocity(id, b3Vec3_zero);
}

void Box3DPhysicsBackend::enqueueResize(const std::shared_ptr<BaseCube>& cube) {
    recreateActor(cube);
}

void Box3DPhysicsBackend::enqueueSetRotation(
    const std::shared_ptr<BaseCube>& cube, Quaternion) {
    if (!cube) return;
    const b3BodyId id = bodyId(*cube);
    if (B3_IS_NULL(id)) return;
    const CFrame cubeWorld = cube->getWorldCFrame();
    const CFrame bodyWorld = cubeWorld * cube->m_compoundLocalOffset.inverse();
    b3Body_SetTransform(
        id, toB3Position(bodyWorld.Position), toB3Quaternion(bodyWorld.Rotation));
}

void Box3DPhysicsBackend::applyForces() {
    for (BodyEntry& entry : m_bodies) {
        auto cube = entry.cube.lock();
        if (!cube || B3_IS_NULL(entry.bodyId) ||
            b3Body_GetType(entry.bodyId) != b3_dynamicBody) continue;
        for (const auto& childEntry : cube->children) {
            const auto& child = childEntry.second;
            if (!child || !child->IsA("Force")) continue;
            auto* force = static_cast<Force*>(child.get());
            if (!force->Enabled) continue;
            if (force->MaintainVelocity) {
                if (force->Torque)
                    b3Body_SetAngularVelocity(entry.bodyId, toB3Vector(force->Value));
                else
                    b3Body_SetLinearVelocity(entry.bodyId, toB3Length(force->Value));
            } else if (force->Torque) {
                b3Body_ApplyTorque(
                    entry.bodyId,
                    {force->Value.x * TORQUE_TO_MKS,
                     force->Value.y * TORQUE_TO_MKS,
                     force->Value.z * TORQUE_TO_MKS},
                    true);
            } else {
                b3Body_ApplyForceToCenter(
                    entry.bodyId, toB3Length(force->Value), true);
            }
        }
    }
}

void Box3DPhysicsBackend::applyBuoyancy() {
    if (!m_facade) return;
    std::vector<std::shared_ptr<LiquidCube>> liquids;
    for (const BodyEntry& entry : m_bodies) {
        auto cube = entry.cube.lock();
        if (cube && cube->IsA("LiquidCube"))
            liquids.push_back(std::static_pointer_cast<LiquidCube>(cube));
    }

    std::set<std::uint64_t> visitedBodies;
    const Vector3 gravity = getGravity();
    const float waveTime = m_facade->getWaveTime();
    for (const BodyEntry& bodyEntry : m_bodies) {
        const b3BodyId id = bodyEntry.bodyId;
        if (B3_IS_NULL(id) || !b3Body_IsValid(id) ||
            b3Body_GetType(id) != b3_dynamicBody)
            continue;
        const std::uint64_t stored = b3StoreBodyId(id);
        if (!visitedBodies.insert(stored).second) continue;

        std::vector<std::shared_ptr<BaseCube>> members;
        float totalBodyVolume = 0.0f;
        for (const BodyEntry& entry : m_bodies) {
            if (!idsEqual(entry.bodyId, id)) continue;
            auto member = entry.cube.lock();
            if (!member || member->IsA("LiquidCube")) continue;
            if (!finiteVector(member->Size)) continue;
            const BuoyancyProxy* proxy = getBuoyancyProxy(*member);
            if (!proxy) continue;
            members.push_back(member);
            const float sizeVolume = proxy->shape == PhysicsShape::Sphere
                ? std::abs(member->Size.x * member->Size.x * member->Size.x)
                : std::abs(member->Size.x * member->Size.y * member->Size.z);
            totalBodyVolume += proxy->normalizedVolume * sizeVolume *
                               proxy->volumeCorrection;
        }

        float totalSubmergedVolume = 0.0f;
        for (const auto& liquid : liquids) {
            if (!liquid || !std::isfinite(liquid->Density) ||
                !finiteVector(liquid->Size) ||
                std::abs(liquid->Size.x) <= CLIP_EPSILON ||
                std::abs(liquid->Size.y) <= CLIP_EPSILON ||
                std::abs(liquid->Size.z) <= CLIP_EPSILON)
                continue;
            const CFrame liquidWorld = liquid->getWorldCFrame();
            const CFrame liquidInverse = liquidWorld.inverse();
            const FacePolyhedron prisms[2] = {
                makeLiquidPrism(waveTime, false),
                makeLiquidPrism(waveTime, true),
            };
            std::vector<Plane> prismPlanes[2] = {
                planesFromPolyhedron(prisms[0]),
                planesFromPolyhedron(prisms[1]),
            };
            const float minimumSurfaceHeight = std::min({
                0.5f + LiquidCube::waveHeight(-0.5f, 0.5f, waveTime),
                0.5f + LiquidCube::waveHeight(-0.5f, -0.5f, waveTime),
                0.5f + LiquidCube::waveHeight(0.5f, -0.5f, waveTime),
                0.5f + LiquidCube::waveHeight(0.5f, 0.5f, waveTime),
            });
            float liquidVolume = 0.0f;
            Vector3 liquidWeightedCenter;

            for (const auto& member : members) {
                const BuoyancyProxy* proxy = getBuoyancyProxy(*member);
                if (!proxy) continue;
                const CFrame memberWorld =
                    bodyWorldFrame(id) * member->m_compoundLocalOffset;
                std::vector<Vector3> normalizedVertices;
                normalizedVertices.reserve(proxy->vertices.size());
                Vector3 minimum(
                    std::numeric_limits<float>::max(),
                    std::numeric_limits<float>::max(),
                    std::numeric_limits<float>::max());
                Vector3 maximum(
                    -std::numeric_limits<float>::max(),
                    -std::numeric_limits<float>::max(),
                    -std::numeric_limits<float>::max());
                for (const Vector3& source : proxy->vertices) {
                    const Vector3 memberScale =
                        proxy->shape == PhysicsShape::Sphere
                        ? Vector3(member->Size.x, member->Size.x, member->Size.x)
                        : member->Size;
                    const Vector3 world =
                        memberWorld.pointToWorld(source * memberScale);
                    const Vector3 local = liquidInverse.pointToWorld(world);
                    const Vector3 normalized = local / liquid->Size;
                    if (!finiteVector(normalized)) {
                        normalizedVertices.clear();
                        break;
                    }
                    normalizedVertices.push_back(normalized);
                    minimum.x = std::min(minimum.x, normalized.x);
                    minimum.y = std::min(minimum.y, normalized.y);
                    minimum.z = std::min(minimum.z, normalized.z);
                    maximum.x = std::max(maximum.x, normalized.x);
                    maximum.y = std::max(maximum.y, normalized.y);
                    maximum.z = std::max(maximum.z, normalized.z);
                }
                if (normalizedVertices.empty() ||
                    maximum.x < -0.5f || minimum.x > 0.5f ||
                    maximum.z < -0.5f || minimum.z > 0.5f ||
                    maximum.y < -0.5f ||
                    minimum.y > 0.5f + LiquidCube::WAVE_AMPLITUDE)
                    continue;

                const bool fullyContained =
                    minimum.x >= -0.5f + CLIP_EPSILON &&
                    maximum.x <= 0.5f - CLIP_EPSILON &&
                    minimum.z >= -0.5f + CLIP_EPSILON &&
                    maximum.z <= 0.5f - CLIP_EPSILON &&
                    minimum.y >= -0.5f + CLIP_EPSILON &&
                    maximum.y <= minimumSurfaceHeight - CLIP_EPSILON;
                if (fullyContained) {
                    const float sizeVolume =
                        proxy->shape == PhysicsShape::Sphere
                        ? std::abs(member->Size.x * member->Size.x *
                                   member->Size.x)
                        : std::abs(member->Size.x * member->Size.y *
                                   member->Size.z);
                    const float volume = proxy->normalizedVolume * sizeVolume *
                                         proxy->volumeCorrection;
                    const Vector3 memberScale =
                        proxy->shape == PhysicsShape::Sphere
                        ? Vector3(member->Size.x, member->Size.x, member->Size.x)
                        : member->Size;
                    const Vector3 center = memberWorld.pointToWorld(
                        proxy->normalizedCentroid * memberScale);
                    if (volume > CLIP_EPSILON && std::isfinite(volume) &&
                        finiteVector(center)) {
                        liquidVolume += volume;
                        liquidWeightedCenter =
                            liquidWeightedCenter + center * volume;
                    }
                    continue;
                }

                const FacePolyhedron memberPoly =
                    makeFacePolyhedron(normalizedVertices, proxy->faces);
                for (int prismIndex = 0; prismIndex < 2; ++prismIndex) {
                    FacePolyhedron clipped = memberPoly;
                    for (const Plane& plane : prismPlanes[prismIndex]) {
                        clipped = clipPolyhedron(clipped, plane);
                        if (clipped.faces.empty()) break;
                    }
                    float normalizedVolume = 0.0f;
                    Vector3 normalizedCentroid;
                    if (!volumeAndCentroid(
                            clipped, normalizedVolume, normalizedCentroid))
                        continue;
                    const float volume = normalizedVolume *
                        std::abs(liquid->Size.x * liquid->Size.y * liquid->Size.z) *
                        proxy->volumeCorrection;
                    if (!(volume > CLIP_EPSILON) || !std::isfinite(volume))
                        continue;
                    const Vector3 center = liquidWorld.pointToWorld(
                        normalizedCentroid * liquid->Size);
                    if (!finiteVector(center)) continue;
                    liquidVolume += volume;
                    liquidWeightedCenter =
                        liquidWeightedCenter + center * volume;
                }
            }

            if (!(liquidVolume > CLIP_EPSILON) ||
                !std::isfinite(liquidVolume))
                continue;
            const Vector3 centerOfBuoyancy =
                liquidWeightedCenter / liquidVolume;
            const float density = std::max(liquid->Density, 0.0f);
            const Vector3 force =
                (-gravity) * (liquidVolume * density / STUDS_PER_METER);
            if (finiteVector(force) && finiteVector(centerOfBuoyancy))
                b3Body_ApplyForce(
                    id, toB3Vector(force), toB3Position(centerOfBuoyancy), true);
            totalSubmergedVolume += liquidVolume;
        }

        const float fraction = totalBodyVolume > CLIP_EPSILON
            ? std::clamp(totalSubmergedVolume / totalBodyVolume, 0.0f, 1.0f)
            : 0.0f;
        b3Body_SetLinearDamping(id, 3.0f * fraction);
        b3Body_SetAngularDamping(id, 3.0f * fraction);
    }
}

void Box3DPhysicsBackend::processContactEvents() {
    if (!Physics::s_contactCallback) return;
    const b3ContactEvents events = b3World_GetContactEvents(m_worldId);
    for (int index = 0; index < events.beginCount; ++index) {
        const b3ContactBeginTouchEvent& event = events.beginEvents[index];
        if (!b3Shape_IsValid(event.shapeIdA) || !b3Shape_IsValid(event.shapeIdB))
            continue;
        auto* first = static_cast<Instance*>(b3Shape_GetUserData(event.shapeIdA));
        auto* second = static_cast<Instance*>(b3Shape_GetUserData(event.shapeIdB));
        if (!first || !second || !first->IsA("BaseCube") || !second->IsA("BaseCube"))
            continue;
        Physics::s_contactCallback(
            static_cast<BaseCube*>(first), static_cast<BaseCube*>(second));
    }
}

void Box3DPhysicsBackend::stepOnce(float dt) {
    if (!isAvailable()) return;
    m_accumulator += std::clamp(dt, 0.0f, 0.25f);
    int stepCount = 0;
    while (m_accumulator >= FIXED_STEP && stepCount < MAX_STEPS) {
        applyBuoyancy();
        applyForces();
        b3World_Step(m_worldId, FIXED_STEP, SUB_STEPS);
        ++m_simulationTick;
        processContactEvents();
        m_accumulator -= FIXED_STEP;
        ++stepCount;
    }
    if (stepCount == MAX_STEPS && m_accumulator >= FIXED_STEP) {
        m_accumulator = 0.0f;
        RCBN_WARN("Box3D physics safety break engaged");
    }
    m_accumulatorAlpha = std::clamp(m_accumulator / FIXED_STEP, 0.0f, 1.0f);
}

Box3DPhysicsBackend::CubePair Box3DPhysicsBackend::normalizePair(
    const BaseCube* first, const BaseCube* second) {
    return std::less<const BaseCube*>()(second, first)
        ? CubePair{second, first} : CubePair{first, second};
}

bool Box3DPhysicsBackend::customFilter(
    b3ShapeId shapeIdA, b3ShapeId shapeIdB, void* context) {
    auto* backend = static_cast<Box3DPhysicsBackend*>(context);
    if (!backend) return true;
    auto* first = static_cast<const BaseCube*>(b3Shape_GetUserData(shapeIdA));
    auto* second = static_cast<const BaseCube*>(b3Shape_GetUserData(shapeIdB));
    if (!first || !second) return true;
    const auto snapshot = backend->m_noCollisionSnapshot;
    return !snapshot ||
           !snapshot->contains(normalizePair(first, second));
}

void Box3DPhysicsBackend::rebuildNoCollisionSnapshot() {
    auto snapshot = std::make_shared<std::set<CubePair>>();
    for (const NoCollisionEntry& entry : m_noCollisionEntries) {
        auto first = entry.cube0.lock();
        auto second = entry.cube1.lock();
        if (first && second)
            snapshot->insert(normalizePair(first.get(), second.get()));
    }
    m_noCollisionSnapshot = std::move(snapshot);
}

PhysicsConstraintHandle Box3DPhysicsBackend::allocateLogicalConstraintHandle() {
    if (m_nextLogicalConstraintHandle == 0) ++m_nextLogicalConstraintHandle;
    return {m_nextLogicalConstraintHandle++};
}

Box3DPhysicsBackend::ConstraintEntry* Box3DPhysicsBackend::findConstraint(
    const std::shared_ptr<Instance>& constraint) {
    auto iterator = std::find_if(
        m_constraints.begin(), m_constraints.end(),
        [&](const ConstraintEntry& entry) {
            return entry.constraint.lock() == constraint;
        });
    return iterator == m_constraints.end() ? nullptr : &*iterator;
}

void Box3DPhysicsBackend::clearConstraintHandle(Instance& constraint) {
    if (constraint.IsA("Weld"))
        static_cast<Weld&>(constraint).m_constraintHandle = {};
    else if (constraint.IsA("Rope"))
        static_cast<Rope&>(constraint).m_constraintHandle = {};
    else if (constraint.IsA("Rod"))
        static_cast<Rod&>(constraint).m_constraintHandle = {};
    else if (constraint.IsA("BallSocket"))
        static_cast<BallSocket&>(constraint).m_constraintHandle = {};
    else if (constraint.IsA("Motor"))
        static_cast<Motor&>(constraint).m_constraintHandle = {};
    else if (constraint.IsA("NoCollision"))
        static_cast<NoCollision&>(constraint).m_constraintHandle = {};
}

void Box3DPhysicsBackend::rebuildAssembly(
    const std::vector<std::shared_ptr<BaseCube>>& assembly) {
    if (assembly.empty()) return;
    std::set<BaseCube*> members;
    std::unordered_map<BaseCube*, CFrame> worldFrames;
    std::set<std::uint64_t> oldBodies;
    Vector3 linearVelocity;
    Vector3 angularVelocity;
    bool velocityCaptured = false;
    bool anchored = false;
    PhysicsLockFlags combinedLocks = PhysicsLockFlags::None;

    for (const auto& cube : assembly) {
        if (!cube) continue;
        members.insert(cube.get());
        const b3BodyId id = bodyId(*cube);
        worldFrames[cube.get()] = B3_IS_NON_NULL(id)
            ? bodyWorldFrame(id) * cube->m_compoundLocalOffset
            : cube->getWorldCFrame();
        if (B3_IS_NON_NULL(id)) {
            oldBodies.insert(b3StoreBodyId(id));
            if (!velocityCaptured && b3Body_GetType(id) == b3_dynamicBody) {
                linearVelocity = fromB3Length(b3Body_GetLinearVelocity(id));
                angularVelocity = fromB3Vector(b3Body_GetAngularVelocity(id));
                velocityCaptured = true;
            }
        }
        anchored = anchored || cube->Anchored;
        combinedLocks |= cube->LockFlags;
    }

    std::vector<std::shared_ptr<Instance>> recreate;
    auto touchesMembers = [&](const std::shared_ptr<Instance>& value) {
        std::shared_ptr<BaseCube> first;
        std::shared_ptr<BaseCube> second;
        if (value->IsA("Rope")) {
            auto c = std::static_pointer_cast<Rope>(value);
            first = c->m_cube0.lock(); second = c->m_cube1.lock();
        } else if (value->IsA("Rod")) {
            auto c = std::static_pointer_cast<Rod>(value);
            first = c->m_cube0.lock(); second = c->m_cube1.lock();
        } else if (value->IsA("BallSocket")) {
            auto c = std::static_pointer_cast<BallSocket>(value);
            first = c->m_cube0.lock(); second = c->m_cube1.lock();
        } else if (value->IsA("Motor")) {
            auto c = std::static_pointer_cast<Motor>(value);
            first = c->m_cube0.lock(); second = c->m_cube1.lock();
        }
        return (first && members.contains(first.get())) ||
               (second && members.contains(second.get()));
    };
    for (ConstraintEntry& entry : m_constraints) {
        auto value = entry.constraint.lock();
        if (!value || value->IsA("Weld") || !touchesMembers(value)) continue;
        if (B3_IS_NON_NULL(entry.jointId) && b3Joint_IsValid(entry.jointId))
            b3DestroyJoint(entry.jointId, false);
        clearConstraintHandle(*value);
        recreate.push_back(value);
    }
    m_constraints.erase(
        std::remove_if(m_constraints.begin(), m_constraints.end(),
            [&](const ConstraintEntry& entry) {
                auto value = entry.constraint.lock();
                return value && !value->IsA("Weld") && touchesMembers(value);
            }),
        m_constraints.end());

    for (std::uint64_t stored : oldBodies) {
        const b3BodyId id = b3LoadBodyId(stored);
        if (b3Body_IsValid(id)) b3DestroyBody(id);
    }
    for (BodyEntry& entry : m_bodies) {
        if (auto cube = entry.cube.lock(); cube && members.contains(cube.get())) {
            entry.bodyId = b3_nullBodyId;
            assignBody(*cube, b3_nullBodyId, CFrame());
        }
    }

    const auto originCube = *std::find_if(
        assembly.begin(), assembly.end(),
        [](const std::shared_ptr<BaseCube>& cube) { return static_cast<bool>(cube); });
    const CFrame origin = worldFrames[originCube.get()];
    b3BodyDef bodyDefinition = b3DefaultBodyDef();
    bodyDefinition.type = anchored ? b3_kinematicBody : b3_dynamicBody;
    bodyDefinition.position = toB3Position(origin.Position);
    bodyDefinition.rotation = toB3Quaternion(origin.Rotation);
    bodyDefinition.userData = originCube.get();
    bodyDefinition.motionLocks = toB3Locks(combinedLocks);
    bodyDefinition.isBullet = std::any_of(
        assembly.begin(), assembly.end(),
        [](const auto& cube) {
            return cube && cube->CollisionDetection == CCDMode::Bullet;
        });
    const b3BodyId newBody = b3CreateBody(m_worldId, &bodyDefinition);
    if (B3_IS_NULL(newBody)) return;

    for (const auto& cube : assembly) {
        if (!cube) continue;
        const CFrame local = origin.inverse() * worldFrames[cube.get()];
        createCubeShape(newBody, cube, local);
        assignBody(*cube, newBody, local);
        cube->m_weldKinematic = anchored;
        auto found = std::find_if(
            m_bodies.begin(), m_bodies.end(),
            [&](const BodyEntry& entry) { return entry.cubeRaw == cube.get(); });
        if (found == m_bodies.end())
            m_bodies.push_back({cube, cube.get(), newBody});
        else
            found->bodyId = newBody;
    }
    b3Body_ApplyMassFromShapes(newBody);
    if (!anchored && velocityCaptured) {
        b3Body_SetLinearVelocity(newBody, toB3Length(linearVelocity));
        b3Body_SetAngularVelocity(newBody, toB3Vector(angularVelocity));
    }

    for (const auto& value : recreate) {
        if (value->IsA("Rope"))
            createRope(std::static_pointer_cast<Rope>(value));
        else if (value->IsA("Rod"))
            createRod(std::static_pointer_cast<Rod>(value));
        else if (value->IsA("BallSocket"))
            createBallSocket(std::static_pointer_cast<BallSocket>(value));
        else if (value->IsA("Motor"))
            createMotor(std::static_pointer_cast<Motor>(value));
    }
}

void Box3DPhysicsBackend::recreateConstraintsFor(const std::set<BaseCube*>&) {
    // rebuildAssembly performs this atomically around body replacement.
}

void Box3DPhysicsBackend::createWeld(
    const std::shared_ptr<Weld>& weld, Workspace& workspace) {
    if (!weld) return;
    auto first = weld->m_cube0.lock();
    auto second = weld->m_cube1.lock();
    if (!first || !second || first == second) return;
    if (!findConstraint(weld)) {
        const PhysicsConstraintHandle handle = allocateLogicalConstraintHandle();
        weld->m_constraintHandle = handle;
        m_constraints.push_back({weld, handle, b3_nullJointId});
    }
    rebuildAssembly(Weld::collectAssembly(first, workspace));
}

void Box3DPhysicsBackend::createRope(const std::shared_ptr<Rope>& rope) {
    if (!rope || findConstraint(rope)) return;
    auto first = rope->m_cube0.lock();
    auto second = rope->m_cube1.lock();
    if (!first || !second) return;
    const b3BodyId bodyA = bodyId(*first);
    const b3BodyId bodyB = bodyId(*second);
    if (B3_IS_NULL(bodyA) || B3_IS_NULL(bodyB)) return;
    if (idsEqual(bodyA, bodyB)) {
        const auto handle = allocateLogicalConstraintHandle();
        rope->m_constraintHandle = handle;
        m_constraints.push_back({rope, handle, b3_nullJointId});
        return;
    }

    const CFrame frameA = attachmentFrame(
        first->m_compoundLocalOffset, rope->m_attachment0, first.get());
    const CFrame frameB = attachmentFrame(
        second->m_compoundLocalOffset, rope->m_attachment1, second.get());
    const Vector3 worldA = (bodyWorldFrame(bodyA) * frameA).Position;
    const Vector3 worldB = (bodyWorldFrame(bodyB) * frameB).Position;
    const float maxLength = std::max(
        (rope->MaxDistance > 0.0f
             ? rope->MaxDistance : (worldB - worldA).length()) * METERS_PER_STUD,
        B3_LINEAR_SLOP);

    b3DistanceJointDef definition = b3DefaultDistanceJointDef();
    definition.base.bodyIdA = bodyA;
    definition.base.bodyIdB = bodyB;
    definition.base.localFrameA = toB3Transform(frameA);
    definition.base.localFrameB = toB3Transform(frameB);
    definition.base.userData = rope.get();
    definition.enableSpring = true;
    definition.enableLimit = true;
    definition.length = maxLength;
    definition.minLength = B3_LINEAR_SLOP;
    definition.maxLength = maxLength;
    definition.lowerSpringForce = -FLT_MAX;
    definition.upperSpringForce = 0.0f;

    const float massA = b3Body_GetMass(bodyA);
    const float massB = b3Body_GetMass(bodyB);
    const float effectiveMass = massA > 0.0f && massB > 0.0f
        ? massA * massB / (massA + massB) : std::max(massA, massB);
    if (rope->Stiffness > 0.0f && effectiveMass > 0.0f) {
        definition.hertz =
            std::sqrt(rope->Stiffness / effectiveMass) / (2.0f * pi);
        definition.dampingRatio = rope->Damping /
            (2.0f * std::sqrt(rope->Stiffness * effectiveMass));
    } else {
        definition.hertz = 0.0f;
        definition.dampingRatio = 0.0f;
    }
    const b3JointId joint = b3CreateDistanceJoint(m_worldId, &definition);
    if (B3_IS_NULL(joint)) return;
    const PhysicsConstraintHandle handle{b3StoreJointId(joint)};
    rope->m_constraintHandle = handle;
    m_constraints.push_back({rope, handle, joint});
}

void Box3DPhysicsBackend::createRod(const std::shared_ptr<Rod>& rod) {
    if (!rod || findConstraint(rod)) return;
    auto first = rod->m_cube0.lock();
    auto second = rod->m_cube1.lock();
    if (!first || !second) return;
    const b3BodyId bodyA = bodyId(*first);
    const b3BodyId bodyB = bodyId(*second);
    if (B3_IS_NULL(bodyA) || B3_IS_NULL(bodyB)) return;
    if (idsEqual(bodyA, bodyB)) {
        const auto handle = allocateLogicalConstraintHandle();
        rod->m_constraintHandle = handle;
        m_constraints.push_back({rod, handle, b3_nullJointId});
        return;
    }
    const CFrame frameA = attachmentFrame(
        first->m_compoundLocalOffset, rod->m_attachment0, first.get());
    const CFrame frameB = attachmentFrame(
        second->m_compoundLocalOffset, rod->m_attachment1, second.get());
    const float distance = std::max(
        ((bodyWorldFrame(bodyB) * frameB).Position -
         (bodyWorldFrame(bodyA) * frameA).Position).length() * METERS_PER_STUD,
        B3_LINEAR_SLOP);
    b3DistanceJointDef definition = b3DefaultDistanceJointDef();
    definition.base.bodyIdA = bodyA;
    definition.base.bodyIdB = bodyB;
    definition.base.localFrameA = toB3Transform(frameA);
    definition.base.localFrameB = toB3Transform(frameB);
    definition.base.userData = rod.get();
    definition.length = distance;
    definition.enableSpring = false;
    const b3JointId joint = b3CreateDistanceJoint(m_worldId, &definition);
    if (B3_IS_NULL(joint)) return;
    const PhysicsConstraintHandle handle{b3StoreJointId(joint)};
    rod->m_constraintHandle = handle;
    m_constraints.push_back({rod, handle, joint});
}

void Box3DPhysicsBackend::createBallSocket(
    const std::shared_ptr<BallSocket>& ballSocket) {
    if (!ballSocket || findConstraint(ballSocket)) return;
    auto first = ballSocket->m_cube0.lock();
    auto second = ballSocket->m_cube1.lock();
    if (!first || !second) return;
    const b3BodyId bodyA = bodyId(*first);
    const b3BodyId bodyB = bodyId(*second);
    if (B3_IS_NULL(bodyA) || B3_IS_NULL(bodyB)) return;
    if (idsEqual(bodyA, bodyB)) {
        const auto handle = allocateLogicalConstraintHandle();
        ballSocket->m_constraintHandle = handle;
        m_constraints.push_back({ballSocket, handle, b3_nullJointId});
        return;
    }
    b3SphericalJointDef definition = b3DefaultSphericalJointDef();
    definition.base.bodyIdA = bodyA;
    definition.base.bodyIdB = bodyB;
    definition.base.localFrameA = toB3Transform(attachmentFrame(
        first->m_compoundLocalOffset, ballSocket->m_attachment0, first.get()));
    definition.base.localFrameB = toB3Transform(attachmentFrame(
        second->m_compoundLocalOffset, ballSocket->m_attachment1, second.get()));
    definition.base.userData = ballSocket.get();
    const b3JointId joint = b3CreateSphericalJoint(m_worldId, &definition);
    if (B3_IS_NULL(joint)) return;
    const PhysicsConstraintHandle handle{b3StoreJointId(joint)};
    ballSocket->m_constraintHandle = handle;
    m_constraints.push_back({ballSocket, handle, joint});
}

void Box3DPhysicsBackend::createMotor(const std::shared_ptr<Motor>& motor) {
    if (!motor || findConstraint(motor)) return;
    auto first = motor->m_cube0.lock();
    auto second = motor->m_cube1.lock();
    if (!first || !second) return;
    const b3BodyId bodyA = bodyId(*first);
    const b3BodyId bodyB = bodyId(*second);
    if (B3_IS_NULL(bodyA) || B3_IS_NULL(bodyB)) return;
    if (idsEqual(bodyA, bodyB)) {
        const auto handle = allocateLogicalConstraintHandle();
        motor->m_constraintHandle = handle;
        m_constraints.push_back({motor, handle, b3_nullJointId});
        return;
    }

    const CFrame cubeWorldA = bodyWorldFrame(bodyA) * first->m_compoundLocalOffset;
    const CFrame cubeWorldB = bodyWorldFrame(bodyB) * second->m_compoundLocalOffset;
    Vector3 pivotA = (cubeWorldA.Position + cubeWorldB.Position) * 0.5f;
    Vector3 pivotB = pivotA;
    auto attachmentA = motor->m_attachment0.lock();
    auto attachmentB = motor->m_attachment1.lock();
    if (attachmentA && attachmentB) {
        pivotA = attachmentA->getWorldCFrame().Position;
        pivotB = attachmentB->getWorldCFrame().Position;
    } else if (attachmentA || attachmentB) {
        pivotA = pivotB = (attachmentA ? attachmentA : attachmentB)
            ->getWorldCFrame().Position;
    }
    const Quaternion jointRotation =
        cubeWorldA.Rotation * rotationFromZ(motor->Axis);
    const CFrame jointWorldA(pivotA, jointRotation);
    const CFrame jointWorldB(pivotB, jointRotation);

    b3RevoluteJointDef definition = b3DefaultRevoluteJointDef();
    definition.base.bodyIdA = bodyA;
    definition.base.bodyIdB = bodyB;
    definition.base.localFrameA =
        toB3Transform(bodyWorldFrame(bodyA).inverse() * jointWorldA);
    definition.base.localFrameB =
        toB3Transform(bodyWorldFrame(bodyB).inverse() * jointWorldB);
    definition.base.userData = motor.get();
    definition.enableMotor = true;
    definition.motorSpeed = motor->DriveVelocity;
    definition.maxMotorTorque = std::max(0.0f, motor->MaxForce) * TORQUE_TO_MKS;
    const b3JointId joint = b3CreateRevoluteJoint(m_worldId, &definition);
    if (B3_IS_NULL(joint)) return;
    const PhysicsConstraintHandle handle{b3StoreJointId(joint)};
    motor->m_constraintHandle = handle;
    m_constraints.push_back({motor, handle, joint});
}

void Box3DPhysicsBackend::createNoCollision(
    const std::shared_ptr<NoCollision>& noCollision) {
    if (!noCollision) return;
    auto first = noCollision->m_cube0.lock();
    auto second = noCollision->m_cube1.lock();
    if (!first || !second) return;
    auto existing = std::find_if(
        m_noCollisionEntries.begin(), m_noCollisionEntries.end(),
        [&](const NoCollisionEntry& entry) {
            return entry.constraint.lock() == noCollision;
        });
    if (existing == m_noCollisionEntries.end()) {
        const auto handle = allocateLogicalConstraintHandle();
        noCollision->m_constraintHandle = handle;
        m_noCollisionEntries.push_back(
            {noCollision, first, second, handle});
    } else {
        noCollision->m_constraintHandle = existing->handle;
    }
    rebuildNoCollisionSnapshot();
    for (BodyEntry& entry : m_bodies) {
        if (B3_IS_NULL(entry.bodyId) || !b3Body_IsValid(entry.bodyId)) continue;
        const int count = b3Body_GetShapeCount(entry.bodyId);
        std::vector<b3ShapeId> shapes(count);
        b3Body_GetShapes(entry.bodyId, shapes.data(), count);
        for (b3ShapeId shape : shapes) {
            const b3Filter filter = b3Shape_GetFilter(shape);
            b3Shape_SetFilter(shape, filter, true);
        }
    }
}

void Box3DPhysicsBackend::removeConstraint(
    const std::shared_ptr<Instance>& constraint) {
    if (!constraint) return;
    if (constraint->IsA("NoCollision")) {
        m_noCollisionEntries.erase(
            std::remove_if(
                m_noCollisionEntries.begin(), m_noCollisionEntries.end(),
                [&](const NoCollisionEntry& entry) {
                    return entry.constraint.lock() == constraint;
                }),
            m_noCollisionEntries.end());
        clearConstraintHandle(*constraint);
        rebuildNoCollisionSnapshot();
        for (BodyEntry& entry : m_bodies) {
            if (B3_IS_NULL(entry.bodyId) || !b3Body_IsValid(entry.bodyId)) continue;
            const int count = b3Body_GetShapeCount(entry.bodyId);
            std::vector<b3ShapeId> shapes(count);
            b3Body_GetShapes(entry.bodyId, shapes.data(), count);
            for (b3ShapeId shape : shapes) {
                const b3Filter filter = b3Shape_GetFilter(shape);
                b3Shape_SetFilter(shape, filter, true);
            }
        }
        return;
    }

    auto iterator = std::find_if(
        m_constraints.begin(), m_constraints.end(),
        [&](const ConstraintEntry& entry) {
            return entry.constraint.lock() == constraint;
        });
    if (iterator == m_constraints.end()) {
        clearConstraintHandle(*constraint);
        return;
    }

    if (!constraint->IsA("Weld")) {
        if (B3_IS_NON_NULL(iterator->jointId) &&
            b3Joint_IsValid(iterator->jointId))
            b3DestroyJoint(iterator->jointId, true);
        clearConstraintHandle(*constraint);
        m_constraints.erase(iterator);
        return;
    }

    auto weld = std::static_pointer_cast<Weld>(constraint);
    auto endpoint = weld->m_cube0.lock();
    if (!endpoint || !hasBody(*endpoint)) endpoint = weld->m_cube1.lock();
    const b3BodyId oldBody =
        endpoint ? bodyId(*endpoint) : b3_nullBodyId;
    clearConstraintHandle(*constraint);
    m_constraints.erase(iterator);
    if (B3_IS_NULL(oldBody)) return;

    std::vector<std::shared_ptr<BaseCube>> oldGroup;
    std::set<BaseCube*> groupSet;
    for (const BodyEntry& entry : m_bodies) {
        if (!idsEqual(entry.bodyId, oldBody)) continue;
        if (auto cube = entry.cube.lock()) {
            oldGroup.push_back(cube);
            groupSet.insert(cube.get());
        }
    }

    std::vector<std::shared_ptr<Instance>> recreate;
    auto touches = [&](const std::shared_ptr<Instance>& value) {
        std::shared_ptr<BaseCube> first;
        std::shared_ptr<BaseCube> second;
        if (value->IsA("Rope")) {
            auto c = std::static_pointer_cast<Rope>(value);
            first = c->m_cube0.lock(); second = c->m_cube1.lock();
        } else if (value->IsA("Rod")) {
            auto c = std::static_pointer_cast<Rod>(value);
            first = c->m_cube0.lock(); second = c->m_cube1.lock();
        } else if (value->IsA("BallSocket")) {
            auto c = std::static_pointer_cast<BallSocket>(value);
            first = c->m_cube0.lock(); second = c->m_cube1.lock();
        } else if (value->IsA("Motor")) {
            auto c = std::static_pointer_cast<Motor>(value);
            first = c->m_cube0.lock(); second = c->m_cube1.lock();
        }
        return (first && groupSet.contains(first.get())) ||
               (second && groupSet.contains(second.get()));
    };
    for (ConstraintEntry& entry : m_constraints) {
        auto value = entry.constraint.lock();
        if (!value || value->IsA("Weld") || !touches(value)) continue;
        if (B3_IS_NON_NULL(entry.jointId) && b3Joint_IsValid(entry.jointId))
            b3DestroyJoint(entry.jointId, false);
        clearConstraintHandle(*value);
        recreate.push_back(value);
    }
    m_constraints.erase(
        std::remove_if(m_constraints.begin(), m_constraints.end(),
            [&](const ConstraintEntry& entry) {
                auto value = entry.constraint.lock();
                return value && !value->IsA("Weld") && touches(value);
            }),
        m_constraints.end());

    if (b3Body_IsValid(oldBody)) b3DestroyBody(oldBody);
    m_bodies.erase(
        std::remove_if(m_bodies.begin(), m_bodies.end(),
            [&](const BodyEntry& entry) {
                return entry.cubeRaw && groupSet.contains(entry.cubeRaw);
            }),
        m_bodies.end());
    for (const auto& cube : oldGroup)
        assignBody(*cube, b3_nullBodyId, CFrame());

    std::set<BaseCube*> processed;
    for (const auto& start : oldGroup) {
        if (!start || processed.contains(start.get())) continue;
        std::vector<std::shared_ptr<BaseCube>> component;
        std::queue<std::shared_ptr<BaseCube>> queue;
        queue.push(start);
        processed.insert(start.get());
        while (!queue.empty()) {
            auto current = queue.front();
            queue.pop();
            component.push_back(current);
            for (const ConstraintEntry& entry : m_constraints) {
                auto value = entry.constraint.lock();
                if (!value || !value->IsA("Weld")) continue;
                auto remaining = std::static_pointer_cast<Weld>(value);
                auto first = remaining->m_cube0.lock();
                auto second = remaining->m_cube1.lock();
                std::shared_ptr<BaseCube> neighbor;
                if (first == current && second && groupSet.contains(second.get()))
                    neighbor = second;
                else if (second == current && first && groupSet.contains(first.get()))
                    neighbor = first;
                if (neighbor && processed.insert(neighbor.get()).second)
                    queue.push(neighbor);
            }
        }
        if (component.size() == 1)
            createActor(component.front());
        else
            rebuildAssembly(component);
    }

    for (const auto& value : recreate) {
        if (value->IsA("Rope"))
            createRope(std::static_pointer_cast<Rope>(value));
        else if (value->IsA("Rod"))
            createRod(std::static_pointer_cast<Rod>(value));
        else if (value->IsA("BallSocket"))
            createBallSocket(std::static_pointer_cast<BallSocket>(value));
        else if (value->IsA("Motor"))
            createMotor(std::static_pointer_cast<Motor>(value));
    }
}

void Box3DPhysicsBackend::updateConstraint(
    const std::shared_ptr<Instance>& constraint) {
    if (!constraint) return;
    ConstraintEntry* entry = findConstraint(constraint);
    if (!entry) return;
    if (constraint->IsA("Rope") && B3_IS_NON_NULL(entry->jointId) &&
        b3Joint_IsValid(entry->jointId)) {
        auto rope = std::static_pointer_cast<Rope>(constraint);
        const float maximum =
            std::max(rope->MaxDistance * METERS_PER_STUD, B3_LINEAR_SLOP);
        b3DistanceJoint_SetLength(entry->jointId, maximum);
        b3DistanceJoint_SetLengthRange(
            entry->jointId, B3_LINEAR_SLOP, maximum);
        const b3BodyId first = b3Joint_GetBodyA(entry->jointId);
        const b3BodyId second = b3Joint_GetBodyB(entry->jointId);
        const float massA = b3Body_GetMass(first);
        const float massB = b3Body_GetMass(second);
        const float effectiveMass = massA > 0.0f && massB > 0.0f
            ? massA * massB / (massA + massB) : std::max(massA, massB);
        const float hertz = rope->Stiffness > 0.0f && effectiveMass > 0.0f
            ? std::sqrt(rope->Stiffness / effectiveMass) / (2.0f * pi) : 0.0f;
        const float damping = rope->Stiffness > 0.0f && effectiveMass > 0.0f
            ? rope->Damping /
                (2.0f * std::sqrt(rope->Stiffness * effectiveMass)) : 0.0f;
        b3DistanceJoint_SetSpringHertz(entry->jointId, hertz);
        b3DistanceJoint_SetSpringDampingRatio(entry->jointId, damping);
        return;
    }
    if (constraint->IsA("Motor")) {
        auto motor = std::static_pointer_cast<Motor>(constraint);
        removeConstraint(motor);
        createMotor(motor);
    }
}

void Box3DPhysicsBackend::removeExpiredEntries() {
    for (auto iterator = m_constraints.begin(); iterator != m_constraints.end();) {
        if (!iterator->constraint.expired()) {
            ++iterator;
            continue;
        }
        if (B3_IS_NON_NULL(iterator->jointId) &&
            b3Joint_IsValid(iterator->jointId))
            b3DestroyJoint(iterator->jointId, false);
        iterator = m_constraints.erase(iterator);
    }
    m_noCollisionEntries.erase(
        std::remove_if(
            m_noCollisionEntries.begin(), m_noCollisionEntries.end(),
            [&](const NoCollisionEntry& entry) {
                if (!entry.constraint.expired() && !entry.cube0.expired() &&
                    !entry.cube1.expired()) return false;
                if (auto value = entry.constraint.lock())
                    clearConstraintHandle(*value);
                return true;
            }),
        m_noCollisionEntries.end());
    rebuildNoCollisionSnapshot();
}

void Box3DPhysicsBackend::createPendingConstraints(Workspace& workspace) {
    for (const auto& value : workspace.pendingConstraints) {
        if (value && value->IsA("Weld"))
            createWeld(std::static_pointer_cast<Weld>(value), workspace);
    }
    for (const auto& value : workspace.pendingConstraints) {
        if (!value || value->IsA("Weld")) continue;
        if (value->IsA("Rope"))
            createRope(std::static_pointer_cast<Rope>(value));
        else if (value->IsA("Rod"))
            createRod(std::static_pointer_cast<Rod>(value));
        else if (value->IsA("BallSocket"))
            createBallSocket(std::static_pointer_cast<BallSocket>(value));
        else if (value->IsA("Motor"))
            createMotor(std::static_pointer_cast<Motor>(value));
        else if (value->IsA("NoCollision"))
            createNoCollision(std::static_pointer_cast<NoCollision>(value));
    }
    workspace.pendingConstraints.clear();
}

void Box3DPhysicsBackend::update(Workspace& workspace, float dt) {
    if (!workspace.PhysicsEnabled || !isAvailable()) return;
    setGravity(workspace.Gravity);
    removeExpiredEntries();

    auto isOutside = [&](const std::weak_ptr<BaseCube>& reference) {
        auto cube = reference.lock();
        return !cube || cube->findFirstAncestorWorkspace() != &workspace;
    };
    std::vector<std::shared_ptr<Instance>> invalidConstraints;
    for (const ConstraintEntry& entry : m_constraints) {
        auto value = entry.constraint.lock();
        if (!value) continue;
        bool invalid = false;
        if (value->IsA("Weld")) {
            auto c = std::static_pointer_cast<Weld>(value);
            invalid = isOutside(c->m_cube0) || isOutside(c->m_cube1);
        } else if (value->IsA("Rope")) {
            auto c = std::static_pointer_cast<Rope>(value);
            invalid = isOutside(c->m_cube0) || isOutside(c->m_cube1);
        } else if (value->IsA("Rod")) {
            auto c = std::static_pointer_cast<Rod>(value);
            invalid = isOutside(c->m_cube0) || isOutside(c->m_cube1);
        } else if (value->IsA("BallSocket")) {
            auto c = std::static_pointer_cast<BallSocket>(value);
            invalid = isOutside(c->m_cube0) || isOutside(c->m_cube1);
        } else if (value->IsA("Motor")) {
            auto c = std::static_pointer_cast<Motor>(value);
            invalid = isOutside(c->m_cube0) || isOutside(c->m_cube1);
        }
        if (invalid) invalidConstraints.push_back(value);
    }
    for (const NoCollisionEntry& entry : m_noCollisionEntries) {
        auto value = entry.constraint.lock();
        if (value && (isOutside(entry.cube0) || isOutside(entry.cube1)))
            invalidConstraints.push_back(value);
    }
    std::sort(invalidConstraints.begin(), invalidConstraints.end());
    invalidConstraints.erase(
        std::unique(invalidConstraints.begin(), invalidConstraints.end()),
        invalidConstraints.end());
    for (const auto& value : invalidConstraints) removeConstraint(value);

    std::vector<std::shared_ptr<BaseCube>> removed;
    for (const BodyEntry& entry : m_bodies) {
        auto cube = entry.cube.lock();
        if (cube && !cube->findFirstAncestorWorkspace()) removed.push_back(cube);
    }
    for (const auto& cube : removed) removeCube(cube);

    for (const auto& value : workspace.pendingInstances) {
        if (!value || !value->IsA("BaseCube")) continue;
        auto cube = std::static_pointer_cast<BaseCube>(value);
        if (cube->findFirstAncestorWorkspace() == &workspace) createActor(cube);
    }
    workspace.pendingInstances.clear();
    createPendingConstraints(workspace);
    stepOnce(dt);
    syncAllCubes();
}

namespace {
struct RayContext {
    const BaseCube* ignore = nullptr;
    float maximumDistance = 0.0f;
    RaycastHit* result = nullptr;
};

float box3dRayCallback(
    b3ShapeId shapeId, b3Pos point, b3Vec3 normal, float fraction,
    uint64_t, int, int, void* rawContext) {
    auto* context = static_cast<RayContext*>(rawContext);
    auto* instance = static_cast<Instance*>(b3Shape_GetUserData(shapeId));
    if (!instance) return -1.0f;
    if (context->ignore && instance->IsA("BaseCube") &&
        static_cast<BaseCube*>(instance) == context->ignore)
        return -1.0f;
    if (!context->result->hit || fraction * context->maximumDistance <
        context->result->distance) {
        context->result->hit = true;
        context->result->distance = fraction * context->maximumDistance;
        context->result->position = fromB3Position(point);
        context->result->normal = fromB3Vector(normal);
        context->result->instance = instance;
    }
    return fraction;
}

struct OverlapContext {
    const BaseCube* ignore = nullptr;
    const std::string* className = nullptr;
    BaseCube* result = nullptr;
};

bool box3dOverlapCallback(b3ShapeId shapeId, void* rawContext) {
    auto* context = static_cast<OverlapContext*>(rawContext);
    auto* instance = static_cast<Instance*>(b3Shape_GetUserData(shapeId));
    if (!instance || !instance->IsA("BaseCube")) return true;
    auto* cube = static_cast<BaseCube*>(instance);
    if (cube == context->ignore || !cube->IsA(*context->className)) return true;
    context->result = cube;
    return false;
}
}

bool Box3DPhysicsBackend::raycast(
    const Vector3& origin, const Vector3& direction, float maxDistance,
    RaycastHit& hitResult, const BaseCube* ignoreCube) {
    hitResult = {};
    if (!isAvailable() || maxDistance <= 0.0f) return false;
    const Vector3 normalized = direction.normalize();
    if (normalized.length() <= 0.0f) return false;
    RayContext context{ignoreCube, maxDistance, &hitResult};
    b3World_CastRay(
        m_worldId, toB3Position(origin),
        toB3Length(normalized * maxDistance),
        b3DefaultQueryFilter(), box3dRayCallback, &context);
    return hitResult.hit;
}

BaseCube* Box3DPhysicsBackend::findOverlapping(
    const BaseCube& cube, const std::string& className, float margin) const {
    if (!isAvailable()) return nullptr;
    const Vector3 center = cube.getWorldPosition();
    const Vector3 half = cube.Size * 0.5f +
        Vector3(margin, margin, margin);
    const b3AABB aabb = {
        toB3Length(center - half),
        toB3Length(center + half),
    };
    OverlapContext context{&cube, &className, nullptr};
    b3World_OverlapAABB(
        m_worldId, aabb, b3DefaultQueryFilter(),
        box3dOverlapCallback, &context);
    return context.result;
}

void Box3DPhysicsBackend::setGravity(const Vector3& gravity) {
    if (isAvailable()) b3World_SetGravity(m_worldId, toB3Length(gravity));
}

Vector3 Box3DPhysicsBackend::getGravity() const {
    return isAvailable()
        ? fromB3Length(b3World_GetGravity(m_worldId)) : Vector3();
}

PhysicsTerrainHandle Box3DPhysicsBackend::createTerrain(
    const PhysicsTerrainDescriptor& descriptor) {
    if (!isAvailable()) return {};

    TerrainEntry terrain;
    std::vector<b3CompoundMeshDef> compoundMeshes;
    std::vector<b3CompoundHullDef> compoundHulls;
    std::vector<b3SurfaceMaterial> meshMaterials;

    if (descriptor.vertices.size() >= 3 && descriptor.indices.size() >= 3) {
        std::vector<b3Vec3> vertices;
        vertices.reserve(descriptor.vertices.size());
        for (const Vector3& vertex : descriptor.vertices)
            vertices.push_back(toB3Length(vertex));
        std::vector<int32_t> indices;
        indices.reserve(descriptor.indices.size());
        for (std::uint32_t index : descriptor.indices)
            indices.push_back(static_cast<int32_t>(index));
        b3MeshDef meshDefinition = {};
        meshDefinition.vertices = vertices.data();
        meshDefinition.indices = indices.data();
        meshDefinition.vertexCount = static_cast<int>(vertices.size());
        meshDefinition.triangleCount = static_cast<int>(indices.size() / 3);
        meshDefinition.weldVertices = true;
        meshDefinition.identifyEdges = true;
        meshDefinition.weldTolerance = B3_LINEAR_SLOP;
        b3MeshData* mesh = b3CreateMesh(&meshDefinition, nullptr, 0);
        if (!mesh) return {};
        terrain.meshes.push_back(mesh);
        meshMaterials.push_back(b3DefaultSurfaceMaterial());
        meshMaterials.back().friction = descriptor.dynamicFriction;
        meshMaterials.back().restitution = descriptor.restitution;
        b3CompoundMeshDef child = {};
        child.meshData = mesh;
        child.transform = b3Transform_identity;
        child.scale = b3Vec3_one;
        child.materials = &meshMaterials.back();
        child.materialCount = 1;
        compoundMeshes.push_back(child);
    }

    for (const PhysicsTerrainHullDescriptor& source : descriptor.hulls) {
        if (source.vertices.size() < 4) continue;
        std::vector<b3Vec3> points;
        points.reserve(source.vertices.size());
        for (const Vector3& point : source.vertices)
            points.push_back(toB3Length(point));
        b3HullData* hull = b3CreateHull(
            points.data(), static_cast<int>(points.size()), 64);
        if (!hull) {
            for (b3MeshData* mesh : terrain.meshes) b3DestroyMesh(mesh);
            for (b3HullData* oldHull : terrain.hulls) b3DestroyHull(oldHull);
            return {};
        }
        terrain.hulls.push_back(hull);
        b3CompoundHullDef child = {};
        child.hull = hull;
        child.transform = toB3Transform(source.localFrame);
        child.material = b3DefaultSurfaceMaterial();
        child.material.friction = descriptor.dynamicFriction;
        child.material.restitution = descriptor.restitution;
        compoundHulls.push_back(child);
    }

    if (compoundMeshes.empty() && compoundHulls.empty()) return {};
    // vector growth may move materials, so repair pointers immediately before cloning.
    for (std::size_t index = 0; index < compoundMeshes.size(); ++index)
        compoundMeshes[index].materials = &meshMaterials[index];
    b3CompoundDef compoundDefinition = {};
    compoundDefinition.meshes = compoundMeshes.data();
    compoundDefinition.meshCount = static_cast<int>(compoundMeshes.size());
    compoundDefinition.hulls = compoundHulls.data();
    compoundDefinition.hullCount = static_cast<int>(compoundHulls.size());
    terrain.compound = b3CreateCompound(&compoundDefinition);
    if (!terrain.compound) {
        for (b3MeshData* mesh : terrain.meshes) b3DestroyMesh(mesh);
        for (b3HullData* hull : terrain.hulls) b3DestroyHull(hull);
        return {};
    }

    b3BodyDef bodyDefinition = b3DefaultBodyDef();
    bodyDefinition.type = b3_staticBody;
    bodyDefinition.position = toB3Position(descriptor.origin);
    bodyDefinition.userData = descriptor.userData;
    terrain.bodyId = b3CreateBody(m_worldId, &bodyDefinition);
    if (B3_IS_NULL(terrain.bodyId)) {
        b3DestroyCompound(terrain.compound);
        for (b3MeshData* mesh : terrain.meshes) b3DestroyMesh(mesh);
        for (b3HullData* hull : terrain.hulls) b3DestroyHull(hull);
        return {};
    }
    b3ShapeDef shapeDefinition = b3DefaultShapeDef();
    shapeDefinition.userData = descriptor.userData;
    shapeDefinition.enableCustomFiltering = true;
    terrain.shapeId = b3CreateCompoundShape(
        terrain.bodyId, &shapeDefinition, terrain.compound);
    if (B3_IS_NULL(terrain.shapeId)) {
        b3DestroyBody(terrain.bodyId);
        b3DestroyCompound(terrain.compound);
        for (b3MeshData* mesh : terrain.meshes) b3DestroyMesh(mesh);
        for (b3HullData* hull : terrain.hulls) b3DestroyHull(hull);
        return {};
    }
    terrain.handle = {b3StoreBodyId(terrain.bodyId)};
    const PhysicsTerrainHandle handle = terrain.handle;
    m_terrains.push_back(std::move(terrain));
    return handle;
}

PhysicsTerrainHandle Box3DPhysicsBackend::replaceTerrain(
    PhysicsTerrainHandle oldHandle,
    const PhysicsTerrainDescriptor& descriptor) {
    const PhysicsTerrainHandle newHandle = createTerrain(descriptor);
    if (!newHandle) return oldHandle;
    destroyTerrain(oldHandle);
    return newHandle;
}

void Box3DPhysicsBackend::destroyTerrain(PhysicsTerrainHandle handle) {
    auto iterator = std::find_if(
        m_terrains.begin(), m_terrains.end(),
        [&](const TerrainEntry& entry) { return entry.handle == handle; });
    if (iterator == m_terrains.end()) return;
    if (B3_IS_NON_NULL(iterator->bodyId) && b3Body_IsValid(iterator->bodyId))
        b3DestroyBody(iterator->bodyId);
    if (iterator->compound) b3DestroyCompound(iterator->compound);
    for (b3MeshData* mesh : iterator->meshes) b3DestroyMesh(mesh);
    for (b3HullData* hull : iterator->hulls) b3DestroyHull(hull);
    m_terrains.erase(iterator);
}

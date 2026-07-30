#pragma once

#include <include/Math/CFrame.hpp>
#include <include/Math/Vector3.hpp>
#include <cstdint>
#include <vector>

class Instance;

enum class PhysicsConstraintType {
    Distance,
    Spherical,
    Revolute,
};

enum class PhysicsBackendType {
    PhysX,
    Box3D,
};

enum class CCDMode {
    Default,
    Bullet,
};

struct PhysicsBodyHandle {
    std::uint64_t value = 0;

    explicit constexpr operator bool() const {
        return value != 0;
    }

    friend constexpr bool operator==(PhysicsBodyHandle lhs, PhysicsBodyHandle rhs) {
        return lhs.value == rhs.value;
    }

    friend constexpr bool operator!=(PhysicsBodyHandle lhs, PhysicsBodyHandle rhs) {
        return !(lhs == rhs);
    }
};

struct PhysicsConstraintHandle {
    std::uint64_t value = 0;

    explicit constexpr operator bool() const {
        return value != 0;
    }

    friend constexpr bool operator==(PhysicsConstraintHandle lhs, PhysicsConstraintHandle rhs) {
        return lhs.value == rhs.value;
    }

    friend constexpr bool operator!=(PhysicsConstraintHandle lhs, PhysicsConstraintHandle rhs) {
        return !(lhs == rhs);
    }
};

struct PhysicsConstraintDescriptor {
    PhysicsConstraintType type = PhysicsConstraintType::Distance;
    PhysicsBodyHandle bodyA;
    PhysicsBodyHandle bodyB;
    CFrame localFrameA;
    CFrame localFrameB;
    Instance* userData = nullptr;
    bool collideConnected = false;

    float restLength = 0.0f;
    float minLength = 0.0f;
    float maxLength = 0.0f;
    bool enableLimit = false;
    bool enableSpring = false;
    bool tensionOnly = false;
    float stiffness = 0.0f;
    float damping = 0.0f;

    bool enableMotor = false;
    float driveVelocity = 0.0f;
    float maxTorque = 0.0f;
};

struct PhysicsTerrainHandle {
    std::uint64_t value = 0;

    explicit constexpr operator bool() const {
        return value != 0;
    }

    friend constexpr bool operator==(PhysicsTerrainHandle lhs, PhysicsTerrainHandle rhs) {
        return lhs.value == rhs.value;
    }

    friend constexpr bool operator!=(PhysicsTerrainHandle lhs, PhysicsTerrainHandle rhs) {
        return !(lhs == rhs);
    }
};

struct PhysicsTerrainHullDescriptor {
    std::vector<Vector3> vertices;
    CFrame localFrame;
};

struct PhysicsTerrainDescriptor {
    Vector3 origin;
    std::vector<Vector3> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<PhysicsTerrainHullDescriptor> hulls;
    Instance* userData = nullptr;
    float staticFriction = 0.5f;
    float dynamicFriction = 0.5f;
    float restitution = 0.2f;
};

enum class PhysicsLockFlags : std::uint8_t {
    None     = 0,
    LinearX  = 1u << 0,
    LinearY  = 1u << 1,
    LinearZ  = 1u << 2,
    AngularX = 1u << 3,
    AngularY = 1u << 4,
    AngularZ = 1u << 5,
};

constexpr PhysicsLockFlags operator|(PhysicsLockFlags lhs, PhysicsLockFlags rhs) {
    return static_cast<PhysicsLockFlags>(
        static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs));
}

constexpr PhysicsLockFlags& operator|=(PhysicsLockFlags& lhs, PhysicsLockFlags rhs) {
    lhs = lhs | rhs;
    return lhs;
}

constexpr bool hasPhysicsLockFlag(PhysicsLockFlags flags, PhysicsLockFlags flag) {
    return (static_cast<std::uint8_t>(flags) & static_cast<std::uint8_t>(flag)) != 0;
}

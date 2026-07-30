#pragma once

#include <cstdint>

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

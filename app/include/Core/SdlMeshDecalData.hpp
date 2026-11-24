#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

inline constexpr std::size_t SDL_MESH_DECAL_LIMIT = 8;

struct SdlMeshDecal {
    // Legacy UV center, radius, and projection mode (-1 = UV, 0..5 = Face).
    std::array<float, 4> projection{};
    std::array<float, 4> tint{};
};

struct SdlMeshDecalData {
    std::uint32_t texture = 0;
    std::uint32_t count = 0;
    std::array<float, 3> minimum{};
    std::array<float, 3> maximum{};
    std::array<SdlMeshDecal, SDL_MESH_DECAL_LIMIT> decals{};
};

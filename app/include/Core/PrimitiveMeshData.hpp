#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <vector>

struct PrimitiveFaceRange {
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
};

using PrimitiveFaceRanges = std::array<PrimitiveFaceRange, 6>;

struct PrimitiveMeshData {
    static constexpr std::size_t FLOATS_PER_VERTEX = 8;

    // Position (3), normal (3), UV (2), matching the legacy vertex layout.
    std::vector<float> vertices;
    std::vector<std::uint32_t> indices;
    PrimitiveFaceRanges faces{};
};

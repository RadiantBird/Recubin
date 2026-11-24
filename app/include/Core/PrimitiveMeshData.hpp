#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

struct PrimitiveMeshData {
    static constexpr std::size_t FLOATS_PER_VERTEX = 8;

    // Position (3), normal (3), UV (2), matching the legacy vertex layout.
    std::vector<float> vertices;
    std::vector<std::uint32_t> indices;
};

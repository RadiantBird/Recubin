#pragma once

#include <span>

#include <Core/RgbaImage.hpp>

// Build the fixed-size MeshCube decal atlas from top-down RGBA8 images.
// The first eight images occupy 256x256 tiles in four columns and two rows.
RgbaImage buildMeshDecalAtlas(std::span<const RgbaImage> layers);

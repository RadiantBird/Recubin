#include <Core/CubeFaceVisuals.hpp>

#include <array>
#include <string_view>

#include <Instances/Cube.hpp>
#include <Instances/Decal.hpp>
#include <Instances/MaterialInstance.hpp>
#include <Instances/Texture.hpp>
#include <Util/Logger.hpp>

namespace {

void collectChildren(Instance& parent,
                     const std::array<float, 6>& faceSizeU,
                     const std::array<float, 6>& faceSizeV,
                     std::array<CubeFaceVisual, 6>& output,
                     std::string_view sourceName) {
    std::array<bool, 6> hasTexture{};
    std::array<bool, 6> hasDecal{};
    std::array<std::string_view, 6> selectedNames{};

    for (const auto& [name, child] : parent.getChildren()) {
        if (!child) {
            RCBN_WARN("Cube face visual child is null: parent=" << parent.getFullPath()
                << ", name=" << name);
            continue;
        }

        const bool isDecal = child->IsA("Decal");
        const bool isTexture = child->IsA("Texture");
        if (!isDecal && !isTexture) {
            continue;
        }

        const int faceIndex = isDecal
            ? static_cast<int>(static_cast<Decal*>(child.get())->face)
            : static_cast<int>(static_cast<Texture*>(child.get())->face);
        if (faceIndex < 0 || faceIndex >= 6) {
            RCBN_WARN("Cube face visual has invalid face index: source=" << sourceName
                << ", child=" << child->getFullPath()
                << ", face=" << faceIndex);
            continue;
        }

        const std::string& path = isDecal
            ? static_cast<Decal*>(child.get())->texturePath
            : static_cast<Texture*>(child.get())->texturePath;
        if (path.empty()) {
            continue;
        }

        // A Decal has priority over every Texture on the same source. Among
        // duplicates of one type, the lexicographically smallest child name
        // wins, independent of unordered_map traversal order.
        if (isDecal) {
            if (hasDecal[faceIndex] && name >= selectedNames[faceIndex]) {
                continue;
            }
            const auto& decal = *static_cast<Decal*>(child.get());
            output[faceIndex].path = decal.texturePath;
            output[faceIndex].tint = decal.Color;
            output[faceIndex].uvScale = {1.0f, 1.0f};
            output[faceIndex].decal = true;
            hasDecal[faceIndex] = true;
            selectedNames[faceIndex] = name;
            continue;
        }

        if (hasDecal[faceIndex]
            || (hasTexture[faceIndex] && name >= selectedNames[faceIndex])) {
            continue;
        }
        const auto& texture = *static_cast<Texture*>(child.get());
        const float scaleU = texture.StudsPerTileU > 0.0f
            ? faceSizeU[faceIndex] / texture.StudsPerTileU
            : 1.0f;
        const float scaleV = texture.StudsPerTileV > 0.0f
            ? faceSizeV[faceIndex] / texture.StudsPerTileV
            : 1.0f;
        output[faceIndex].path = texture.texturePath;
        output[faceIndex].tint = texture.Color;
        output[faceIndex].uvScale = {scaleU, scaleV};
        output[faceIndex].decal = false;
        hasTexture[faceIndex] = true;
        selectedNames[faceIndex] = name;
    }
}

} // namespace

CubeFaceVisuals collectCubeFaceVisuals(Cube& cube) {
    CubeFaceVisuals visuals;

    const std::array<float, 6> faceSizeU = {
        cube.Size.x, cube.Size.x, cube.Size.x,
        cube.Size.x, cube.Size.z, cube.Size.z
    };
    const std::array<float, 6> faceSizeV = {
        cube.Size.y, cube.Size.y, cube.Size.z,
        cube.Size.z, cube.Size.y, cube.Size.y
    };

    collectChildren(cube, faceSizeU, faceSizeV, visuals.direct, "direct");
    if (const auto material = cube.getMaterialInstance()) {
        collectChildren(*material, faceSizeU, faceSizeV, visuals.material, "material");
    }

    return visuals;
}

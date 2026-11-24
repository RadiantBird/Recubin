#include <Core/MeshDecalAtlas.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace {

constexpr unsigned ATLAS_WIDTH = 1024;
constexpr unsigned ATLAS_HEIGHT = 512;
constexpr unsigned TILE_SIZE = 256;
constexpr std::size_t MAX_LAYERS = 8;

bool hasCompleteRgbaPixels(const RgbaImage& image) {
    if (image.width == 0 || image.height == 0) {
        return false;
    }

    constexpr std::size_t CHANNEL_COUNT = 4;
    if (static_cast<std::size_t>(image.width) >
        std::numeric_limits<std::size_t>::max() / image.height) {
        return false;
    }
    const std::size_t pixelCount = static_cast<std::size_t>(image.width) * image.height;
    if (pixelCount > std::numeric_limits<std::size_t>::max() / CHANNEL_COUNT) {
        return false;
    }
    return image.pixels.size() >= pixelCount * CHANNEL_COUNT;
}

void resampleLetterboxed(const RgbaImage& source,
                         std::vector<std::uint8_t>& atlas,
                         unsigned tileX,
                         unsigned tileY) {
    if (!hasCompleteRgbaPixels(source)) {
        return;
    }

    const float scale = std::min(static_cast<float>(TILE_SIZE) / source.width,
                                 static_cast<float>(TILE_SIZE) / source.height);
    const unsigned destinationWidth = std::max(
        1u, static_cast<unsigned>(std::round(source.width * scale)));
    const unsigned destinationHeight = std::max(
        1u, static_cast<unsigned>(std::round(source.height * scale)));
    const unsigned offsetX = (TILE_SIZE - destinationWidth) / 2;
    const unsigned offsetY = (TILE_SIZE - destinationHeight) / 2;

    for (unsigned y = 0; y < destinationHeight; ++y) {
        const float sourceY = (static_cast<float>(y) + 0.5f) / scale - 0.5f;
        const int y0 = std::clamp(static_cast<int>(std::floor(sourceY)),
                                  0, static_cast<int>(source.height) - 1);
        const int y1 = std::clamp(y0 + 1, 0, static_cast<int>(source.height) - 1);
        const float fy = std::clamp(sourceY - y0, 0.0f, 1.0f);

        for (unsigned x = 0; x < destinationWidth; ++x) {
            const float sourceX = (static_cast<float>(x) + 0.5f) / scale - 0.5f;
            const int x0 = std::clamp(static_cast<int>(std::floor(sourceX)),
                                      0, static_cast<int>(source.width) - 1);
            const int x1 = std::clamp(x0 + 1, 0, static_cast<int>(source.width) - 1);
            const float fx = std::clamp(sourceX - x0, 0.0f, 1.0f);

            const std::size_t p00 =
                (static_cast<std::size_t>(y0) * source.width + x0) * 4;
            const std::size_t p10 =
                (static_cast<std::size_t>(y0) * source.width + x1) * 4;
            const std::size_t p01 =
                (static_cast<std::size_t>(y1) * source.width + x0) * 4;
            const std::size_t p11 =
                (static_cast<std::size_t>(y1) * source.width + x1) * 4;
            const std::size_t destination =
                (static_cast<std::size_t>(tileY * TILE_SIZE + offsetY + y) * ATLAS_WIDTH +
                 tileX * TILE_SIZE + offsetX + x) * 4;

            for (unsigned channel = 0; channel < 4; ++channel) {
                const float top = source.pixels[p00 + channel] +
                                  (source.pixels[p10 + channel] - source.pixels[p00 + channel]) * fx;
                const float bottom = source.pixels[p01 + channel] +
                                     (source.pixels[p11 + channel] - source.pixels[p01 + channel]) * fx;
                const float value = top + (bottom - top) * fy;
                atlas[destination + channel] = static_cast<std::uint8_t>(
                    std::clamp(value, 0.0f, 255.0f));
            }
        }
    }
}

} // namespace

RgbaImage buildMeshDecalAtlas(std::span<const RgbaImage> layers) {
    RgbaImage atlas;
    atlas.width = ATLAS_WIDTH;
    atlas.height = ATLAS_HEIGHT;
    atlas.pixels.assign(static_cast<std::size_t>(ATLAS_WIDTH) * ATLAS_HEIGHT * 4, 0);

    const std::size_t layerCount = std::min(layers.size(), MAX_LAYERS);
    for (std::size_t layer = 0; layer < layerCount; ++layer) {
        resampleLetterboxed(layers[layer], atlas.pixels,
                            static_cast<unsigned>(layer % 4),
                            static_cast<unsigned>(layer / 4));
    }
    return atlas;
}

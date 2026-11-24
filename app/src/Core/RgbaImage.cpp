#include <Core/RgbaImage.hpp>

#include <Core/SvgRaster.hpp>
#include <include/stb_image.h>

#include <algorithm>
#include <fstream>
#include <limits>

namespace {

void flipRows(std::vector<std::uint8_t>& pixels, unsigned width, unsigned height) {
    const std::size_t rowSize = static_cast<std::size_t>(width) * 4;
    for (unsigned y = 0; y < height / 2; ++y) {
        auto first = pixels.begin() + static_cast<std::size_t>(y) * rowSize;
        auto last = pixels.begin() + static_cast<std::size_t>(height - 1 - y) * rowSize;
        std::swap_ranges(first, first + rowSize, last);
    }
}

bool hasExpectedSize(const std::vector<std::uint8_t>& pixels,
                     unsigned width,
                     unsigned height) {
    if (width == 0 || height == 0) return false;
    constexpr std::size_t channelCount = 4;
    if (static_cast<std::size_t>(width) >
        std::numeric_limits<std::size_t>::max() /
            (static_cast<std::size_t>(height) * channelCount)) {
        return false;
    }
    const std::size_t expected = static_cast<std::size_t>(width) * height * channelCount;
    return pixels.size() == expected;
}

} // namespace

bool decodeRgbaImage(const std::filesystem::path& path,
                     RgbaImage& image,
                     std::string& error) {
    image = {};
    error.clear();
    if (path.empty()) {
        error = "image path is empty";
        return false;
    }

    if (SvgRaster::isSvgPath(path)) {
        int width = 0;
        int height = 0;
        std::vector<unsigned char> pixels;
        if (!SvgRaster::rasterizeFile(path, pixels, width, height)) {
            error = "SVG rasterization failed";
            return false;
        }
        if (width <= 0 || height <= 0) {
            error = "SVG rasterizer returned invalid dimensions";
            return false;
        }
        image.width = static_cast<unsigned>(width);
        image.height = static_cast<unsigned>(height);
        image.pixels.assign(pixels.begin(), pixels.end());
        if (!hasExpectedSize(image.pixels, image.width, image.height)) {
            image = {};
            error = "SVG rasterizer returned an invalid RGBA8 buffer";
            return false;
        }

        // SvgRaster exposes its OpenGL-style bottom-up buffer. The common
        // image contract is top-down, matching stb_image's memory layout.
        flipRows(image.pixels, image.width, image.height);
        return true;
    }

    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        error = "could not open image file";
        return false;
    }
    const std::streamsize size = input.tellg();
    if (size <= 0 || size > std::numeric_limits<int>::max()) {
        error = "image file has an invalid byte count";
        return false;
    }

    std::vector<std::uint8_t> encoded(static_cast<std::size_t>(size));
    input.seekg(0, std::ios::beg);
    if (!input.read(reinterpret_cast<char*>(encoded.data()), size)) {
        error = "could not read image file";
        return false;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_set_flip_vertically_on_load_thread(0);
    stbi_uc* source = stbi_load_from_memory(encoded.data(), static_cast<int>(encoded.size()),
                                             &width, &height, &channels, 4);
    if (!source) {
        const char* reason = stbi_failure_reason();
        error = reason && *reason ? reason : "stb_image failed to decode image";
        return false;
    }
    if (width <= 0 || height <= 0) {
        stbi_image_free(source);
        error = "image decoder returned invalid dimensions";
        return false;
    }

    image.width = static_cast<unsigned>(width);
    image.height = static_cast<unsigned>(height);
    image.pixels.assign(source, source + static_cast<std::size_t>(width) * height * 4);
    stbi_image_free(source);
    if (!hasExpectedSize(image.pixels, image.width, image.height)) {
        image = {};
        error = "image decoder returned an invalid RGBA8 buffer";
        return false;
    }
    return true;
}

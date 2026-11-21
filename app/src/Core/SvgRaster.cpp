#include <Core/SvgRaster.hpp>
#include <Util/Logger.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#include "include/nanosvg.h"
#include "include/nanosvgrast.h"

namespace SvgRaster {

namespace {

constexpr float MIN_LONG_EDGE = 512.0f;
constexpr float MAX_LONG_EDGE = 2048.0f;
constexpr std::uintmax_t MAX_FILE_BYTES = 8u * 1024u * 1024u;

bool readFile(const std::filesystem::path& path, std::string& out) {
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec) {
        RCBN_WARN("SvgRaster: cannot stat " << path.string());
        return false;
    }
    if (size == 0 || size > MAX_FILE_BYTES) {
        RCBN_WARN("SvgRaster: file size out of range (" << size << " bytes): " << path.string());
        return false;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        RCBN_WARN("SvgRaster: cannot open " << path.string());
        return false;
    }
    out.resize(static_cast<size_t>(size));
    file.read(out.data(), static_cast<std::streamsize>(size));
    if (!file) {
        RCBN_WARN("SvgRaster: read failed " << path.string());
        return false;
    }
    return true;
}

void flipRows(std::vector<unsigned char>& rgba, int width, int height) {
    const size_t stride = static_cast<size_t>(width) * 4;
    for (int y = 0; y < height / 2; ++y) {
        std::swap_ranges(rgba.begin() + y * stride,
                         rgba.begin() + (y + 1) * stride,
                         rgba.begin() + (height - 1 - y) * stride);
    }
}

} // namespace

bool isSvgPath(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".svg";
}

bool rasterizeFile(const std::filesystem::path& path,
                   std::vector<unsigned char>& rgba, int& width, int& height) {
    std::string text;
    if (!readFile(path, text)) return false;

    // nsvgParse は入力バッファを書き換える。text は終端NUL付きで保持される。
    NSVGimage* image = nsvgParse(text.data(), "px", 96.0f);
    if (!image) {
        RCBN_WARN("SvgRaster: failed to parse " << path.string());
        return false;
    }

    const float svgW = image->width;
    const float svgH = image->height;
    const float longEdge = std::max(svgW, svgH);
    if (!(svgW > 0.0f) || !(svgH > 0.0f) || !std::isfinite(longEdge)) {
        RCBN_WARN("SvgRaster: invalid SVG size in " << path.string());
        nsvgDelete(image);
        return false;
    }

    const float targetLong = std::clamp(longEdge, MIN_LONG_EDGE, MAX_LONG_EDGE);
    const float scale = targetLong / longEdge;
    const int w = std::max(1, static_cast<int>(std::lround(svgW * scale)));
    const int h = std::max(1, static_cast<int>(std::lround(svgH * scale)));

    NSVGrasterizer* rasterizer = nsvgCreateRasterizer();
    if (!rasterizer) {
        RCBN_WARN("SvgRaster: failed to create rasterizer");
        nsvgDelete(image);
        return false;
    }

    rgba.assign(static_cast<size_t>(w) * h * 4, 0);
    nsvgRasterize(rasterizer, image, 0.0f, 0.0f, scale, rgba.data(), w, h, w * 4);
    nsvgDeleteRasterizer(rasterizer);
    nsvgDelete(image);

    flipRows(rgba, w, h);
    width = w;
    height = h;
    return true;
}

} // namespace SvgRaster

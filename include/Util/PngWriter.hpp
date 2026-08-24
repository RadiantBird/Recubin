#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace PngWriter {
// rgba is tightly packed RGBA8 in OpenGL bottom-up row order.
bool writeRgba8(const std::string& path, int width, int height,
                const std::vector<std::uint8_t>& rgba, std::string* error = nullptr);
}

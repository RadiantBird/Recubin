#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace PngWriter {
// rgba is tightly packed RGBA8 in OpenGL bottom-up row order.
bool writeRgba8(const std::filesystem::path& path, int width, int height,
                const std::vector<std::uint8_t>& rgba, std::string* error = nullptr);
}

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct RgbaImage {
    unsigned width = 0;
    unsigned height = 0;
    std::vector<std::uint8_t> pixels;
};

// Decode an image into top-down, tightly packed RGBA8 pixels.
// The decoder accepts the image formats supported by stb_image and SVG.
bool decodeRgbaImage(const std::filesystem::path& path,
                     RgbaImage& image,
                     std::string& error);

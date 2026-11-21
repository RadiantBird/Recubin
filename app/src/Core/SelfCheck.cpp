#include "include/Core/SelfCheck.hpp"
#include "include/Core/AssetDependencies.hpp"
#include "include/Core/SceneLoader.hpp"
#include "include/Util/AssetPath.hpp"
#include "include/Util/Sha256.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace SelfCheck {

namespace {

bool contains(const std::vector<std::string_view>& keys, const std::string& key) {
    return std::find(keys.begin(), keys.end(), key) != keys.end();
}

bool isScriptPath(const std::string& path) {
    std::string ext = std::filesystem::path(AssetPath::fromStored(path)).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".luau" || ext == ".lua" || ext == ".luar" || ext == ".luauc";
}

// 1つのファイル参照を、内容で比べられる値に置き換える
std::string describeFile(const std::string& stored) {
    if (stored.empty()) return stored;
    const std::filesystem::path path = AssetPath::fromStored(stored);
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) return "missing:" + stored;
    if (isScriptPath(stored)) return "script:present";
    std::ifstream file(path, std::ios::binary);
    if (!file) return "missing:" + stored;
    const std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return "sha256:" + Sha256::hex(bytes.data(), bytes.size());
}

std::string describeDirectory(const std::string& stored) {
    if (stored.empty()) return stored;
    std::error_code error;
    return std::filesystem::is_directory(AssetPath::fromStored(stored), error) ? "dir:present"
                                                                                : "missing:" + stored;
}

void normalizeNode(YAML::Node node) {
    if (node.IsMap()) {
        for (auto entry : node) {
            const std::string key = entry.first.as<std::string>();
            YAML::Node value = entry.second;
            if (value.IsScalar() && contains(AssetDependencies::fileKeys(), key)) {
                node[key] = describeFile(value.as<std::string>());
            } else if (value.IsScalar() && contains(AssetDependencies::directoryKeys(), key)) {
                node[key] = describeDirectory(value.as<std::string>());
            } else if (value.IsSequence() && contains(AssetDependencies::sequenceKeys(), key)) {
                for (std::size_t i = 0; i < value.size(); ++i) {
                    if (value[i].IsScalar()) value[i] = describeFile(value[i].as<std::string>());
                }
            } else {
                normalizeNode(value);
            }
        }
    } else if (node.IsSequence()) {
        for (auto child : node) normalizeNode(child);
    }
}

} // namespace

std::string describeTree(Instance& root) {
    YAML::Emitter out;
    SceneLoader::emitNode(out, &root);
    YAML::Node document = YAML::Load(out.c_str());
    normalizeNode(document);
    YAML::Emitter normalized;
    normalized << document;
    return std::string(normalized.c_str()) + "\n";
}

bool writeRawImage(const std::string& path, const Image& image) {
    std::ofstream file(std::filesystem::u8path(path), std::ios::binary);
    if (!file) return false;
    const std::int32_t size[2] = {image.width, image.height};
    file.write(reinterpret_cast<const char*>(size), sizeof(size));
    file.write(reinterpret_cast<const char*>(image.rgba.data()), static_cast<std::streamsize>(image.rgba.size()));
    return static_cast<bool>(file);
}

bool readRawImage(const std::string& path, Image& image) {
    std::ifstream file(std::filesystem::u8path(path), std::ios::binary);
    if (!file) return false;
    std::int32_t size[2] = {0, 0};
    file.read(reinterpret_cast<char*>(size), sizeof(size));
    if (!file || size[0] <= 0 || size[1] <= 0 || size[0] > 16384 || size[1] > 16384) return false;
    image.width = size[0];
    image.height = size[1];
    image.rgba.assign(static_cast<std::size_t>(size[0]) * static_cast<std::size_t>(size[1]) * 4, 0);
    file.read(reinterpret_cast<char*>(image.rgba.data()), static_cast<std::streamsize>(image.rgba.size()));
    return static_cast<bool>(file);
}

ImageDiff compare(const Image& a, const Image& b, int threshold) {
    ImageDiff diff;
    diff.sizeMatches = a.width == b.width && a.height == b.height && a.rgba.size() == b.rgba.size();
    if (!diff.sizeMatches || a.rgba.empty()) return diff;
    std::uint64_t total = 0;
    std::size_t differing = 0;
    for (std::size_t i = 0; i < a.rgba.size(); i += 4) {
        bool pixelDiffers = false;
        for (std::size_t c = 0; c < 3; ++c) {
            const int delta = std::abs(static_cast<int>(a.rgba[i + c]) - static_cast<int>(b.rgba[i + c]));
            total += static_cast<std::uint64_t>(delta);
            pixelDiffers = pixelDiffers || delta > threshold;
        }
        if (pixelDiffers) ++differing;
    }
    const std::size_t pixels = a.rgba.size() / 4;
    diff.meanAbsolute = static_cast<double>(total) / static_cast<double>(pixels * 3);
    diff.differingRatio = static_cast<double>(differing) / static_cast<double>(pixels);
    return diff;
}

double luminanceStdDev(const Image& image) {
    const std::size_t pixels = image.rgba.size() / 4;
    if (pixels == 0) return 0.0;
    double sum = 0.0, sumSquares = 0.0;
    for (std::size_t i = 0; i < image.rgba.size(); i += 4) {
        const double luminance = 0.2126 * image.rgba[i] + 0.7152 * image.rgba[i + 1] + 0.0722 * image.rgba[i + 2];
        sum += luminance;
        sumSquares += luminance * luminance;
    }
    const double mean = sum / static_cast<double>(pixels);
    return std::sqrt((std::max)(0.0, sumSquares / static_cast<double>(pixels) - mean * mean));
}

} // namespace SelfCheck

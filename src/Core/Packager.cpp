#include <Core/Packager.hpp>
#include <Util/AssetPath.hpp>
#include <Util/UUID.hpp>
#include "include/luau/luacode.h"
#include <yaml-cpp/yaml.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

#ifdef __APPLE__
    #include "include/stb_image.h"
#endif

namespace fs = std::filesystem;

// ---- helpers ----

static bool endsWith(const std::string& s, const std::string& suffix) {
    if (s.size() < suffix.size()) return false;
    return s.rfind(suffix) == s.size() - suffix.size();
}

static bool isScript(const std::string& path) {
    return endsWith(path, ".luau") || endsWith(path, ".lua");
}

static std::string assetSubdir(const std::string& path) {
    std::string ext = AssetPath::toStored(AssetPath::fromStored(path).extension());
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".mp3" || ext == ".wav" || ext == ".ogg" || ext == ".flac")
        return "assets/sound";
    if (ext == ".luau" || ext == ".lua" || ext == ".luauc")
        return "assets/scripts";
    if (ext == ".glb" || ext == ".gltf")
        return "assets/models";
    if (ext == ".rcanim")
        return "assets/anims";
    return "assets/image";
}

static bool isAnimationClip(const std::string& path) {
    std::string ext = AssetPath::toStored(AssetPath::fromStored(path).extension());
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == ".rcanim";
}

// Copy a file, creating parent dirs as needed. Returns false on error.
static bool copyFile(const fs::path& src, const fs::path& dst,
                     std::function<void(const std::string&)>& log) {
    std::error_code ec;
    fs::create_directories(dst.parent_path(), ec);
    if (ec) { log("[WARN] mkdir failed: " + AssetPath::toStored(dst.parent_path())); }
    fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        log("[WARN] Copy failed: " + AssetPath::toStored(src) + " -> " + AssetPath::toStored(dst) + " : " + ec.message());
        return false;
    }
    return true;
}

// Compile a .luau source file to .luauc bytecode in-process. Returns output path, or "" on failure.
static std::string compileLuauInProc(const fs::path& src, const fs::path& dstDir,
                                      std::function<void(const std::string&)>& log) {
    std::error_code ec;
    fs::create_directories(dstDir, ec);

    std::ifstream f(src, std::ios::binary);
    if (!f) {
        log("[WARN] Cannot read script: " + AssetPath::toStored(src));
        return "";
    }
    std::string source((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    size_t bytecodeSize = 0;
    char* bytecode = luau_compile(source.c_str(), source.size(), nullptr, &bytecodeSize);

    // First byte == 0 means compile error; rest of buffer is the error message
    if (!bytecode || bytecodeSize == 0 || (unsigned char)bytecode[0] == 0) {
        std::string errMsg = (bytecode && bytecodeSize > 1)
            ? std::string(bytecode + 1, bytecodeSize - 1)
            : "unknown compile error";
        if (bytecode) free(bytecode);
        log("[WARN] Compile failed (" + AssetPath::toStored(src.filename()) + "): " + errMsg);
        return "";
    }

    fs::path outPath = dstDir / AssetPath::fromStored(AssetPath::toStored(src.stem()) + ".luauc");
    std::ofstream out(outPath, std::ios::binary);
    out.write(bytecode, (std::streamsize)bytecodeSize);
    free(bytecode);
    return AssetPath::toStored(outPath);
}

// Walk YAML tree and collect file-referencing values for given keys.
// out: vector of (yamlNodeRef-like info isn't trackable, so we collect string paths)
// DataPath はディレクトリ参照（Terrainのチャンク保存先）のため、ファイルパスとは別に dirPaths に集める。
static void collectPaths(const YAML::Node& node, std::vector<std::string>& paths, std::vector<std::string>& dirPaths) {
    if (node.IsMap()) {
        for (auto it = node.begin(); it != node.end(); ++it) {
            std::string key = it->first.as<std::string>();
            if ((key == "ContentPath" || key == "Texture" || key == "FacePath" || key == "MeshFile" || key == "IconPath") && it->second.IsScalar()) {
                std::string v = it->second.as<std::string>();
                if (!v.empty()) paths.push_back(v);
            } else if (key == "SkyboxPaths" && it->second.IsSequence()) {
                const YAML::Node skyboxPaths = it->second;
                for (const auto& elem : skyboxPaths) {
                    if (elem.IsScalar()) {
                        std::string v = elem.as<std::string>();
                        if (!v.empty()) paths.push_back(v);
                    }
                }
            } else if (key == "DataPath" && it->second.IsScalar()) {
                std::string v = it->second.as<std::string>();
                if (!v.empty()) dirPaths.push_back(v);
            } else {
                collectPaths(it->second, paths, dirPaths);
            }
        }
    } else if (node.IsSequence()) {
        for (const auto& child : node) {
            collectPaths(child, paths, dirPaths);
        }
    }
}

// Rewrite file-referencing paths inside a YAML node tree.
// pathMap: old absolute/relative path -> new relative path inside the package
static void rewritePaths(YAML::Node node,
                         const std::unordered_map<std::string, std::string>& pathMap) {
    if (node.IsMap()) {
        for (auto it = node.begin(); it != node.end(); ++it) {
            std::string key = it->first.as<std::string>();
            if ((key == "ContentPath" || key == "Texture" || key == "FacePath" || key == "MeshFile" || key == "IconPath" || key == "DataPath") && it->second.IsScalar()) {
                std::string v = it->second.as<std::string>();
                auto found = pathMap.find(v);
                it->second = found != pathMap.end()
                    ? found->second : AssetPath::normalize(v);
            } else if (key == "SkyboxPaths" && it->second.IsSequence()) {
                YAML::Node seq = it->second;
                for (std::size_t i = 0; i < seq.size(); ++i) {
                    if (seq[i].IsScalar()) {
                        std::string v = seq[i].as<std::string>();
                        auto found = pathMap.find(v);
                        seq[i] = found != pathMap.end()
                            ? found->second : AssetPath::normalize(v);
                    }
                }
            } else {
                rewritePaths(it->second, pathMap);
            }
        }
    } else if (node.IsSequence()) {
        for (auto child : node) {
            rewritePaths(child, pathMap);
        }
    }
}

// The first AppImage directly under Root is also the application icon.  Keep
// this separate from the recursive asset walk: nested AppImages are runtime
// images, not the bundle icon.
static YAML::Node findYamlMapValue(const YAML::Node& map, const char* key) {
    if (!map.IsMap()) return {};
    for (auto it = map.begin(); it != map.end(); ++it) {
        if (it->first.IsScalar() && it->first.as<std::string>() == key) {
            return it->second;
        }
    }
    return {};
}

static YAML::Node findSystemNode(const YAML::Node& node) {
    if (node.IsMap()) {
        const YAML::Node className = findYamlMapValue(node, "ClassName");
        if (className.IsScalar() && className.as<std::string>() == "System") return node;
        for (auto it = node.begin(); it != node.end(); ++it) {
            YAML::Node found = findSystemNode(it->second);
            if (found) return found;
        }
    } else if (node.IsSequence()) {
        for (const auto& child : node) {
            YAML::Node found = findSystemNode(child);
            if (found) return found;
        }
    }
    return {};
}

static std::string findRootAppIconPath(const YAML::Node& sceneNode) {
    const YAML::Node root = findYamlMapValue(sceneNode, "Root");
    if (!root.IsMap()) return {};
    const YAML::Node children = findYamlMapValue(root, "Children");
    if (!children.IsSequence()) return {};

    for (const auto& child : children) {
        const YAML::Node className = findYamlMapValue(child, "ClassName");
        if (!className.IsScalar() || className.as<std::string>() != "AppImage") {
            continue;
        }
        const YAML::Node iconPath = findYamlMapValue(findYamlMapValue(child, "Properties"), "IconPath");
        if (iconPath.IsScalar()) return iconPath.as<std::string>();
        return {};
    }
    return {};
}

static std::string findLegacyWalkContentPath(const YAML::Node& sceneNode) {
    const YAML::Node recubin = findYamlMapValue(sceneNode, "recubin");
    const YAML::Node animations = findYamlMapValue(recubin, "animations");
    const YAML::Node walk = findYamlMapValue(animations, "r6_walk");
    const YAML::Node contentPath = findYamlMapValue(walk, "ContentPath");
    return contentPath.IsScalar() ? contentPath.as<std::string>() : std::string();
}

#ifdef __APPLE__
struct IconPng {
    std::vector<unsigned char> bytes;
};

static void appendBigEndian32(std::vector<unsigned char>& output, std::uint32_t value) {
    output.push_back(static_cast<unsigned char>((value >> 24) & 0xff));
    output.push_back(static_cast<unsigned char>((value >> 16) & 0xff));
    output.push_back(static_cast<unsigned char>((value >> 8) & 0xff));
    output.push_back(static_cast<unsigned char>(value & 0xff));
}

static std::uint32_t crc32(const unsigned char* data, std::size_t size) {
    std::uint32_t crc = 0xffffffffu;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

static std::uint32_t adler32(const std::vector<unsigned char>& data) {
    constexpr std::uint32_t kAdlerMod = 65521;
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (unsigned char value : data) {
        a = (a + value) % kAdlerMod;
        b = (b + a) % kAdlerMod;
    }
    return (b << 16) | a;
}

static void appendPngChunk(std::vector<unsigned char>& png,
                           const char type[4],
                           const std::vector<unsigned char>& data) {
    appendBigEndian32(png, static_cast<std::uint32_t>(data.size()));
    const std::size_t typeOffset = png.size();
    png.insert(png.end(), type, type + 4);
    png.insert(png.end(), data.begin(), data.end());
    appendBigEndian32(png, crc32(png.data() + typeOffset, 4 + data.size()));
}

// Encode RGBA pixels as a PNG without depending on zlib or external tools.
// Stored DEFLATE blocks are intentionally used here: icons are small and this
// keeps the packager portable across every supported macOS installation.
static bool encodeRgbaPng(const std::vector<unsigned char>& rgba,
                          int size,
                          IconPng& output,
                          std::string& error) {
    if (size <= 0 || rgba.size() != static_cast<std::size_t>(size) * size * 4) {
        error = "invalid RGBA buffer size";
        return false;
    }

    std::vector<unsigned char> scanlines;
    scanlines.reserve(static_cast<std::size_t>(size) * (1 + size * 4));
    for (int y = 0; y < size; ++y) {
        scanlines.push_back(0); // PNG filter type: none
        const auto begin = rgba.begin() + static_cast<std::size_t>(y) * size * 4;
        scanlines.insert(scanlines.end(), begin, begin + size * 4);
    }

    std::vector<unsigned char> compressed{0x78, 0x01}; // zlib, no compression
    std::size_t offset = 0;
    while (offset < scanlines.size()) {
        const std::size_t blockSize = std::min<std::size_t>(65535, scanlines.size() - offset);
        const bool finalBlock = offset + blockSize == scanlines.size();
        compressed.push_back(finalBlock ? 0x01 : 0x00);
        const auto length = static_cast<std::uint16_t>(blockSize);
        compressed.push_back(static_cast<unsigned char>(length & 0xff));
        compressed.push_back(static_cast<unsigned char>((length >> 8) & 0xff));
        const auto inverse = static_cast<std::uint16_t>(~length);
        compressed.push_back(static_cast<unsigned char>(inverse & 0xff));
        compressed.push_back(static_cast<unsigned char>((inverse >> 8) & 0xff));
        compressed.insert(compressed.end(), scanlines.begin() + offset,
                          scanlines.begin() + offset + blockSize);
        offset += blockSize;
    }
    appendBigEndian32(compressed, adler32(scanlines));

    output.bytes = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    std::vector<unsigned char> header(13, 0);
    header[3] = static_cast<unsigned char>(size & 0xff);
    header[2] = static_cast<unsigned char>((size >> 8) & 0xff);
    header[1] = static_cast<unsigned char>((size >> 16) & 0xff);
    header[0] = static_cast<unsigned char>((size >> 24) & 0xff);
    header[7] = header[3];
    header[6] = header[2];
    header[5] = header[1];
    header[4] = header[0];
    header[8] = 8;  // bit depth
    header[9] = 6;  // RGBA
    appendPngChunk(output.bytes, "IHDR", header);
    appendPngChunk(output.bytes, "IDAT", compressed);
    appendPngChunk(output.bytes, "IEND", {});
    return true;
}

static bool resizeToSquareRgba(const unsigned char* source,
                               int sourceWidth,
                               int sourceHeight,
                               int size,
                               std::vector<unsigned char>& output,
                               std::string& error) {
    if (!source || sourceWidth <= 0 || sourceHeight <= 0 || size <= 0) {
        error = "invalid decoded image dimensions";
        return false;
    }

    output.assign(static_cast<std::size_t>(size) * size * 4, 0);
    const double scale = std::min(static_cast<double>(size) / sourceWidth,
                                  static_cast<double>(size) / sourceHeight);
    const int drawWidth = std::max(1, static_cast<int>(std::round(sourceWidth * scale)));
    const int drawHeight = std::max(1, static_cast<int>(std::round(sourceHeight * scale)));
    const int offsetX = (size - drawWidth) / 2;
    const int offsetY = (size - drawHeight) / 2;

    for (int y = 0; y < drawHeight; ++y) {
        const double sourceY = std::clamp((y + 0.5) * sourceHeight / drawHeight - 0.5,
                                          0.0, static_cast<double>(sourceHeight - 1));
        const int y0 = static_cast<int>(sourceY);
        const int y1 = std::min(y0 + 1, sourceHeight - 1);
        const double fy = sourceY - y0;
        for (int x = 0; x < drawWidth; ++x) {
            const double sourceX = std::clamp((x + 0.5) * sourceWidth / drawWidth - 0.5,
                                              0.0, static_cast<double>(sourceWidth - 1));
            const int x0 = static_cast<int>(sourceX);
            const int x1 = std::min(x0 + 1, sourceWidth - 1);
            const double fx = sourceX - x0;
            const int destinationX = offsetX + x;
            const int destinationY = offsetY + y;
            unsigned char* destination = output.data() +
                (static_cast<std::size_t>(destinationY) * size + destinationX) * 4;
            for (int channel = 0; channel < 4; ++channel) {
                const auto pixel = [source, sourceWidth, channel](int px, int py) -> double {
                    return source[(static_cast<std::size_t>(py) * sourceWidth + px) * 4 + channel];
                };
                const double top = pixel(x0, y0) * (1.0 - fx) + pixel(x1, y0) * fx;
                const double bottom = pixel(x0, y1) * (1.0 - fx) + pixel(x1, y1) * fx;
                destination[channel] = static_cast<unsigned char>(std::clamp(
                    std::round(top * (1.0 - fy) + bottom * fy), 0.0, 255.0));
            }
        }
    }
    return true;
}

struct IcnsEntrySpec {
    const char type[5];
    int size;
};

static bool generateMacIcon(const fs::path& source,
                            const fs::path& resourcesDir,
                            std::function<void(const std::string&)>& log) {
    if (!fs::exists(source) || !fs::is_regular_file(source)) {
        log("[ERROR] AppImage icon source not found: " + AssetPath::toStored(source));
        return false;
    }

    int sourceWidth = 0;
    int sourceHeight = 0;
    int sourceChannels = 0;
    // Renderer uses bottom-left texture orientation globally.  Filesystem
    // images used for Finder icons are top-left oriented, so explicitly
    // disable that renderer setting for this decode and restore it after.
    stbi_set_flip_vertically_on_load(0);
    unsigned char* decoded = stbi_load(source.string().c_str(), &sourceWidth, &sourceHeight,
                                       &sourceChannels, 4);
    stbi_set_flip_vertically_on_load(1);
    if (!decoded) {
        const char* reason = stbi_failure_reason();
        log("[ERROR] Cannot decode AppImage icon " + AssetPath::toStored(source) + ": " +
            (reason ? reason : "unknown image decode error"));
        return false;
    }

    constexpr IcnsEntrySpec entries[] = {
        {{'i', 'c', 'p', '4', '\0'}, 16},
        {{'i', 'c', 'p', '5', '\0'}, 32},
        {{'i', 'c', 'p', '6', '\0'}, 64},
        {{'i', 'c', '0', '7', '\0'}, 128},
        {{'i', 'c', '0', '8', '\0'}, 256},
        {{'i', 'c', '0', '9', '\0'}, 512},
        {{'i', 'c', '1', '0', '\0'}, 1024},
        {{'i', 'c', '1', '1', '\0'}, 32},
        {{'i', 'c', '1', '2', '\0'}, 64},
        {{'i', 'c', '1', '3', '\0'}, 256},
        {{'i', 'c', '1', '4', '\0'}, 512},
        {{'i', 'c', '1', '5', '\0'}, 1024}
    };

    struct EncodedEntry {
        const char* type;
        IconPng png;
    };
    std::vector<EncodedEntry> encoded;
    encoded.reserve(std::size(entries));

    for (const IcnsEntrySpec& entry : entries) {
        std::vector<unsigned char> rgba;
        std::string error;
        if (!resizeToSquareRgba(decoded, sourceWidth, sourceHeight, entry.size, rgba, error)) {
            stbi_image_free(decoded);
            log("[ERROR] Cannot resize AppImage icon " + AssetPath::toStored(source) + " to " +
                std::to_string(entry.size) + "x" + std::to_string(entry.size) + ": " + error);
            return false;
        }

        IconPng png;
        if (!encodeRgbaPng(rgba, entry.size, png, error)) {
            stbi_image_free(decoded);
            log("[ERROR] Cannot encode AppImage icon PNG " + AssetPath::toStored(source) + " at " +
                std::to_string(entry.size) + "x" + std::to_string(entry.size) + ": " + error);
            return false;
        }
        encoded.push_back({entry.type, std::move(png)});
    }
    stbi_image_free(decoded);

    std::size_t totalSize = 8;
    for (const EncodedEntry& entry : encoded) totalSize += 8 + entry.png.bytes.size();
    if (totalSize > std::numeric_limits<std::uint32_t>::max()) {
        log("[ERROR] Generated AppIcon.icns is too large");
        return false;
    }

    std::vector<unsigned char> icns{'i', 'c', 'n', 's'};
    appendBigEndian32(icns, static_cast<std::uint32_t>(totalSize));
    for (const EncodedEntry& entry : encoded) {
        icns.insert(icns.end(), entry.type, entry.type + 4);
        appendBigEndian32(icns, static_cast<std::uint32_t>(8 + entry.png.bytes.size()));
        icns.insert(icns.end(), entry.png.bytes.begin(), entry.png.bytes.end());
    }

    const unsigned char pngSignature[] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    bool containsPng = false;
    for (std::size_t i = 8; i + sizeof(pngSignature) <= icns.size(); ++i) {
        if (std::equal(std::begin(pngSignature), std::end(pngSignature), icns.begin() + i)) {
            containsPng = true;
            break;
        }
    }
    if (icns.size() != totalSize || !containsPng) {
        log("[ERROR] Generated AppIcon.icns failed internal validation");
        return false;
    }

    const fs::path output = resourcesDir / "AppIcon.icns";
    std::ofstream file(output, std::ios::binary);
    if (!file) {
        log("[ERROR] Cannot open AppIcon.icns for writing: " + AssetPath::toStored(output));
        return false;
    }
    file.write(reinterpret_cast<const char*>(icns.data()), static_cast<std::streamsize>(icns.size()));
    if (!file) {
        log("[ERROR] Failed writing AppIcon.icns: " + AssetPath::toStored(output));
        return false;
    }
    file.close();

    std::error_code ec;
    const auto writtenSize = fs::file_size(output, ec);
    if (ec || writtenSize != totalSize) {
        log("[ERROR] AppIcon.icns size validation failed: " + AssetPath::toStored(output));
        return false;
    }
    log("[OK] AppIcon.icns generated internally from " + AssetPath::toStored(source));
    return true;
}

static std::string xmlEscape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char c : value) {
        switch (c) {
        case '&': escaped += "&amp;"; break;
        case '<': escaped += "&lt;"; break;
        case '>': escaped += "&gt;"; break;
        case '"': escaped += "&quot;"; break;
        case '\'': escaped += "&apos;"; break;
        default: escaped += c; break;
        }
    }
    return escaped;
}

static std::string makeBundleIdentifier(const std::string& gameName) {
    std::string suffix;
    for (unsigned char c : gameName) {
        if (std::isalnum(c)) suffix += static_cast<char>(std::tolower(c));
        else suffix += '_';
    }
    if (suffix.empty()) suffix = "game";
    return "com.recubin." + suffix;
}

static bool writeMacInfoPlist(const fs::path& bundleDir,
                              const std::string& gameName,
                              bool hasIcon,
                              std::function<void(const std::string&)>& log) {
    const fs::path plistPath = bundleDir / "Contents/Info.plist";
    std::ofstream plist(plistPath);
    if (!plist) {
        log("[ERROR] Cannot write Info.plist: " + AssetPath::toStored(plistPath));
        return false;
    }

    const std::string escapedName = xmlEscape(gameName);
    plist << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
          << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
             "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
          << "<plist version=\"1.0\">\n<dict>\n"
          << "\t<key>CFBundleDevelopmentRegion</key>\n\t<string>en</string>\n"
          << "\t<key>CFBundleDisplayName</key>\n\t<string>" << escapedName << "</string>\n"
          << "\t<key>CFBundleExecutable</key>\n\t<string>RecubinEngine</string>\n"
          << "\t<key>CFBundleIdentifier</key>\n\t<string>" << xmlEscape(makeBundleIdentifier(gameName)) << "</string>\n"
          << "\t<key>CFBundleInfoDictionaryVersion</key>\n\t<string>6.0</string>\n"
          << "\t<key>CFBundleName</key>\n\t<string>" << escapedName << "</string>\n"
          << "\t<key>CFBundlePackageType</key>\n\t<string>APPL</string>\n"
          << "\t<key>CFBundleShortVersionString</key>\n\t<string>1.0</string>\n"
          << "\t<key>CFBundleVersion</key>\n\t<string>1</string>\n";
    if (hasIcon) {
        plist << "\t<key>CFBundleIconFile</key>\n\t<string>AppIcon.icns</string>\n";
    }
    plist << "\t<key>NSHighResolutionCapable</key>\n\t<true/>\n</dict>\n</plist>\n";
    if (!plist) {
        log("[ERROR] Failed while writing Info.plist: " + AssetPath::toStored(plistPath));
        return false;
    }
    log("[OK] Info.plist written");
    return true;
}

static std::string shellQuote(const fs::path& path) {
    std::string quoted = "'";
    for (const char c : AssetPath::toStored(path)) {
        if (c == '\'') quoted += "'\\''";
        else quoted += c;
    }
    quoted += "'";
    return quoted;
}

static bool signMacBundle(const fs::path& bundleDir,
                          std::function<void(const std::string&)>& log) {
    // RecubinEngine is built with a linker ad-hoc signature.  Once copied
    // into an App Bundle, sign the finished bundle so Finder sees one
    // consistent signature covering its executable and resources.
    const std::string quotedBundle = shellQuote(bundleDir);
    const std::string signCommand = "/usr/bin/codesign --force --deep --sign - " + quotedBundle;
    if (std::system(signCommand.c_str()) != 0) {
        log("[ERROR] macOS ad-hoc signing failed for: " + AssetPath::toStored(bundleDir));
        return false;
    }

    const std::string verifyCommand =
        "/usr/bin/codesign --verify --deep --strict --verbose=2 " + quotedBundle;
    if (std::system(verifyCommand.c_str()) != 0) {
        log("[ERROR] macOS signature verification failed for: " + AssetPath::toStored(bundleDir));
        return false;
    }
    log("[OK] macOS App Bundle ad-hoc signed and verified");
    return true;
}
#endif

// ---- public API ----

bool Packager::package(const Config& cfg, std::function<void(const std::string&)> log) {
    if (!RecubinUUID::isValid(cfg.applicationId)) {
        log("[ERROR] Invalid or missing ApplicationId; save the scene before packaging.");
        return false;
    }
    // Keep the package content root at the same relative location on every
    // platform.  On macOS it is Contents/Resources so startup.yaml and all
    // existing assets remain addressable as assets/... and shaders/....
    const fs::path gameName = AssetPath::fromStored(cfg.gameName);
#ifdef __APPLE__
    const fs::path outputDir = AssetPath::fromStored(cfg.outputDir);
    const fs::path bundleDir = outputDir / AssetPath::fromStored(cfg.gameName + ".app");
    const fs::path contentsDir = bundleDir / "Contents";
    const fs::path macOsDir = contentsDir / "MacOS";
    const fs::path resourcesDir = contentsDir / "Resources";
    fs::path gameDir = resourcesDir;
#else
    const fs::path outputDir = AssetPath::fromStored(cfg.outputDir);
    const fs::path bundleDir = outputDir / gameName;
    fs::path gameDir = bundleDir;
#endif
    std::error_code ec;
    fs::create_directories(gameDir, ec);
    if (ec) { log("[ERROR] Cannot create output folder: " + AssetPath::toStored(gameDir)); return false; }

    log("Output: " + AssetPath::toStored(bundleDir));

    // Create subdirs
    for (const char* sub : { "assets/image", "assets/sound", "assets/scripts", "assets/scenes", "assets/models", "assets/anims", "assets/fonts" }) {
        fs::create_directories(gameDir / sub, ec);
    }

    // GUI文字の字形と縮小計算はフォントメトリクスに依存するため、ゲームから直接参照
    // されていなくてもエンジン標準フォントを必ず同梱する。起動場所の違いに対応して、
    // CWD、エディター実行ファイルの隣、その親、macOS App BundleのResourcesを探索する。
    {
        const fs::path editorDir = AssetPath::fromStored(cfg.engineExePath).parent_path();
        const std::vector<fs::path> fontCandidates = {
            fs::path("assets/fonts"),
            editorDir / "assets/fonts",
            editorDir.parent_path() / "assets/fonts",
            editorDir.parent_path() / "Resources/assets/fonts"
        };
        fs::path fontSrcDir;
        for (const fs::path& candidate : fontCandidates) {
            if (fs::exists(candidate / "DotGothic16-Regular.ttf") &&
                fs::is_regular_file(candidate / "DotGothic16-Regular.ttf")) {
                fontSrcDir = candidate;
                break;
            }
        }
        if (fontSrcDir.empty()) {
            log("[ERROR] Required runtime font not found: assets/fonts/DotGothic16-Regular.ttf");
            return false;
        }

        int fontCount = 0;
        std::error_code fontEc;
        for (fs::recursive_directory_iterator it(fontSrcDir, fontEc), end;
             !fontEc && it != end; it.increment(fontEc)) {
            if (!it->is_regular_file()) continue;
            const fs::path relative = fs::relative(it->path(), fontSrcDir, fontEc);
            if (fontEc || !copyFile(it->path(), gameDir / "assets/fonts" / relative, log)) {
                log("[ERROR] Failed to package runtime font: " + AssetPath::toStored(it->path()));
                return false;
            }
            ++fontCount;
        }
        if (fontEc || fontCount == 0) {
            log("[ERROR] Failed to enumerate runtime fonts: " + AssetPath::toStored(fontSrcDir));
            return false;
        }
        log("[OK] Runtime fonts copied: " + std::to_string(fontCount) + " file(s)");
    }

    // Copy shader files (shaders/*.glsl) — Renderer looks for them at "shaders/..." relative to cwd
    {
        fs::path shaderSrcDir("shaders");
        fs::path shaderDstDir = gameDir / "shaders";
        fs::create_directories(shaderDstDir, ec);
        int shaderCount = 0;
        if (fs::exists(shaderSrcDir)) {
            for (auto& entry : fs::directory_iterator(shaderSrcDir, ec)) {
                if (entry.path().extension() == ".glsl") {
                    if (copyFile(entry.path(), shaderDstDir / entry.path().filename(), log)) {
                        ++shaderCount;
                    }
                }
            }
        }
        if (shaderCount > 0) log("[OK] Shaders copied: " + std::to_string(shaderCount) + " file(s)");
        else log("[WARN] No .glsl files found in shaders/ — rendering may be broken");
    }

    // Load scene YAML
    const fs::path scenePath = AssetPath::fromStored(cfg.scenePath);
    std::ifstream sceneFile(scenePath, std::ios::binary);
    if (!sceneFile.is_open()) {
        log("[ERROR] Cannot open scene: " + cfg.scenePath);
        return false;
    }
    std::stringstream ss;
    ss << sceneFile.rdbuf();
    YAML::Node sceneNode;
    try {
        sceneNode = YAML::Load(ss.str());
    } catch (const std::exception& e) {
        log("[ERROR] Failed to parse scene YAML: " + std::string(e.what()));
        return false;
    }

    // The packaged scene must carry the live System identity, even when an old
    // scene omitted it or contains a stale value from a previous package.
    YAML::Node rootNode = findYamlMapValue(sceneNode, "Root");
    YAML::Node systemNode;
    // Current SceneLoader saves a virtual System as a flat Root map (there is
    // no ClassName field); preserve that format and inject into Root itself.
    const YAML::Node rootClassName = findYamlMapValue(rootNode, "ClassName");
    if (rootNode.IsMap() && !rootClassName.IsScalar()) {
        systemNode = rootNode;
    } else {
        systemNode = findSystemNode(rootNode);
        if (!systemNode || !systemNode.IsMap()) systemNode = findSystemNode(sceneNode);
    }
    if (!systemNode || !systemNode.IsMap()) {
        log("[ERROR] Scene YAML has no System root; cannot inject ApplicationId.");
        return false;
    }
    YAML::Node properties = findYamlMapValue(systemNode, "Properties");
    if (!properties || !properties.IsMap()) properties = systemNode["Properties"] = YAML::Node(YAML::NodeType::Map);
    properties["ApplicationId"] = cfg.applicationId;

    const std::string rawAppIconPath = findRootAppIconPath(sceneNode);
    const std::string legacyWalkContentPath = findLegacyWalkContentPath(sceneNode);

    // Collect all asset paths referenced in the scene
    std::vector<std::string> rawPaths;
    std::vector<std::string> rawDirPaths;
    collectPaths(sceneNode, rawPaths, rawDirPaths);

    // Deduplicate
    std::sort(rawPaths.begin(), rawPaths.end());
    rawPaths.erase(std::unique(rawPaths.begin(), rawPaths.end()), rawPaths.end());
    std::sort(rawDirPaths.begin(), rawDirPaths.end());
    rawDirPaths.erase(std::unique(rawDirPaths.begin(), rawDirPaths.end()), rawDirPaths.end());

    // Process each referenced file
    std::unordered_map<std::string, std::string> pathMap; // old -> new (relative to gameDir)
    for (const std::string& rawPath : rawPaths) {
        fs::path src = AssetPath::fromStored(rawPath);
        // Only the retired scene-header reference used scene-relative paths.
        // Animation Instance ContentPath values follow normal project lookup.
        if (!fs::exists(src) && rawPath == legacyWalkContentPath && src.is_relative()) {
            const fs::path legacySource = scenePath.parent_path() / src;
            if (fs::exists(legacySource)) src = legacySource;
        }
        if (!fs::exists(src)) {
            log("[WARN] File not found, skipping: " + rawPath);
            continue;
        }

        if (isScript(rawPath)) {
            fs::path dstDir = gameDir / "assets/scripts";
            std::string compiled = compileLuauInProc(src, dstDir, log);
            if (!compiled.empty()) {
                fs::path rel = fs::relative(AssetPath::fromStored(compiled), gameDir, ec);
                const std::string storedRel = AssetPath::toStored(rel);
                pathMap[rawPath] = storedRel;
                log("[OK] Compiled: " + AssetPath::toStored(src.filename()) + " -> " + storedRel);
            } else {
                // Fallback: copy source (compile failed)
                fs::path dst = dstDir / src.filename();
                if (copyFile(src, dst, log))
                    pathMap[rawPath] = AssetPath::toStored(fs::path("assets/scripts") / src.filename());
            }
        } else {
            // Determine destination subdir from file extension
            std::string sub = assetSubdir(rawPath);
            // For relative paths, preserve directory structure under the subdir
            bool isRel = src.is_relative();
            fs::path dst;
            // Animation clips use the normal project-relative lookup rule and
            // are collected into the package's stable assets/anims namespace.
            if (isAnimationClip(rawPath)) {
                dst = gameDir / "assets/anims" / src.filename();
            } else if (isRel) {
                dst = gameDir / src;
            } else {
                dst = gameDir / sub / src.filename();
            }
            if (copyFile(src, dst, log)) {
                fs::path rel = fs::relative(dst, gameDir, ec);
                const std::string storedRel = AssetPath::toStored(rel);
                pathMap[rawPath] = storedRel;
                log("[OK] Copied: " + rawPath + " -> " + storedRel);
            }
        }
    }

    // Terrain の DataPath はチャンク保存先ディレクトリなので、ファイル用の copyFile 経路ではなく
    // ディレクトリごと再帰コピーする。未編集（ディレクトリ未作成）の地形はシードから自動生成
    // されるため、存在しない場合は警告のみでスキップする。
    for (const std::string& rawDir : rawDirPaths) {
        fs::path src = AssetPath::fromStored(rawDir);
        if (!fs::exists(src) || !fs::is_directory(src)) {
            log("[WARN] Terrain data dir not found, skipping: " + rawDir);
            continue;
        }
        fs::path dst;
        std::string newRel;
        if (src.is_relative()) {
            dst = gameDir / src;   // 相対はそのままの位置に同梱
            newRel = AssetPath::toStored(src);
        } else {
            dst = gameDir / "terrain" / src.filename();
            newRel = AssetPath::toStored(fs::path("terrain") / src.filename());
        }
        std::error_code dirEc;
        fs::create_directories(dst.parent_path(), dirEc);
        fs::copy(src, dst, fs::copy_options::recursive | fs::copy_options::overwrite_existing, dirEc);
        if (dirEc) {
            log("[WARN] Terrain data copy failed: " + rawDir + " : " + dirEc.message());
            continue;
        }
        if (newRel != rawDir) pathMap[rawDir] = newRel;
        log("[OK] Terrain data: " + rawDir + " -> " + newRel);
    }

    // Rewrite paths in YAML and write the scene file
    rewritePaths(sceneNode, pathMap);
    {
        YAML::Emitter emit;
        emit << sceneNode;
        fs::path sceneOut = gameDir / "assets/scenes" / AssetPath::fromStored(cfg.gameName + ".yaml");
        std::ofstream outFile(sceneOut);
        if (!outFile) { log("[ERROR] Cannot write scene YAML to: " + AssetPath::toStored(sceneOut)); return false; }
        outFile << emit.c_str();
        log("[OK] Scene written: " + AssetPath::toStored(sceneOut));
    }

#ifdef __APPLE__
    // The editor executable is passed in engineExePath, but the game runtime
    // is always the sibling RecubinEngine binary.  Never silently package the
    // editor: a missing runtime makes the package unusable.
    if (cfg.engineExePath.empty()) {
        log("[ERROR] Editor executable path is empty; cannot locate RecubinEngine");
        return false;
    }
    const fs::path exeDir = AssetPath::fromStored(cfg.engineExePath).parent_path();
    const fs::path runtime = exeDir / "RecubinEngine";
    if (!fs::exists(runtime) || !fs::is_regular_file(runtime)) {
        log("[ERROR] RecubinEngine not found next to editor executable: " + AssetPath::toStored(runtime));
        return false;
    }
    fs::create_directories(macOsDir, ec);
    if (ec || !copyFile(runtime, macOsDir / "RecubinEngine", log)) {
        log("[ERROR] Failed to package RecubinEngine");
        return false;
    }
    const fs::path packagedRuntime = macOsDir / "RecubinEngine";
    fs::permissions(packagedRuntime,
                    fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                    fs::perm_options::add, ec);
    if (ec) {
        log("[ERROR] Failed to set executable permission on " + AssetPath::toStored(packagedRuntime) +
            " : " + ec.message());
        return false;
    }
    log("[OK] Runtime: " + AssetPath::toStored(packagedRuntime));
#else
    // Windows keeps the existing folder package layout, but must also require
    // the actual game runtime instead of falling back to the editor binary.
    if (cfg.engineExePath.empty()) {
        log("[ERROR] Editor executable path is empty; cannot locate RecubinEngine.exe");
        return false;
    }
    const fs::path exeDir = AssetPath::fromStored(cfg.engineExePath).parent_path();
    const fs::path runtime = exeDir / "RecubinEngine.exe";
    if (!fs::exists(runtime) || !fs::is_regular_file(runtime)) {
        log("[ERROR] RecubinEngine.exe not found next to editor executable: " + AssetPath::toStored(runtime));
        return false;
    }
    if (!copyFile(runtime, gameDir / "RecubinEngine.exe", log)) {
        log("[ERROR] Failed to package RecubinEngine.exe");
        return false;
    }
    log("[OK] Runtime: RecubinEngine.exe");

    // Copy launcher.exe
    fs::path launcher = exeDir / "launcher.exe";
    if (fs::exists(launcher)) {
        copyFile(launcher, gameDir / "launcher.exe", log);
        log("[OK] launcher.exe");
    } else {
        log("[WARN] launcher.exe not found — run 'py build.py launcher Release' first");
    }

    // Copy all DLLs from the same directory
    for (auto& entry : fs::directory_iterator(exeDir, ec)) {
        if (entry.path().extension() == ".dll") {
            if (copyFile(entry.path(), gameDir / entry.path().filename(), log)) {
                log("[OK] DLL: " + AssetPath::toStored(entry.path().filename()));
            }
        }
    }
#endif

#ifdef __APPLE__
    bool hasMacIcon = false;
    if (!rawAppIconPath.empty()) {
        const auto iconMapping = pathMap.find(rawAppIconPath);
        if (iconMapping == pathMap.end()) {
            log("[ERROR] Root AppImage icon was not packaged: " + rawAppIconPath);
            return false;
        }
        const fs::path packagedIcon = gameDir / AssetPath::fromStored(iconMapping->second);
        if (!generateMacIcon(packagedIcon, resourcesDir, log)) return false;
        hasMacIcon = true;
    }
    if (!writeMacInfoPlist(bundleDir, cfg.gameName, hasMacIcon, log)) return false;
#endif

    // Write startup.yaml (read by RecubinEngine on launch)
    {
        YAML::Emitter startup;
        startup << YAML::BeginMap
                << YAML::Key << "GameName"   << YAML::Value << cfg.gameName
                << YAML::Key << "ApplicationId" << YAML::Value << cfg.applicationId
                << YAML::Key << "StartScene" << YAML::Value
                << ("assets/scenes/" + AssetPath::toStored(gameName) + ".yaml")
                << YAML::EndMap;
        std::ofstream startupFile(gameDir / "startup.yaml");
        if (startupFile) {
            startupFile << startup.c_str();
            log("[OK] startup.yaml written");
        }
    }

    // Write README.txt
    {
        std::ofstream readme(gameDir / "README.txt");
        if (readme) {
            readme << cfg.gameName << "\n\n";
            readme << "起動方法:\n";
#ifdef __APPLE__
            readme << "  Finderで「" << cfg.gameName << ".app」をダブルクリックします。\n";
#else
            readme << "  RecubinEngine.exe\n\n";
            readme << "または launcher.exe から起動するとデスクトップショートカットを作成できます。\n";
#endif
        }
    }

#ifdef __APPLE__
    if (!signMacBundle(bundleDir, log)) return false;
#endif

    log("[DONE] Package created: " + AssetPath::toStored(bundleDir));
    return true;
}

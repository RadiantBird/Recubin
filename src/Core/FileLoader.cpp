#include <Core/FileLoader.hpp>
#include <Util/AssetGuard.hpp>
#include <Util/AssetPath.hpp>
#include <fstream>
#include <sstream>
#include <iostream>

// 読み込み対象（シェーダ/シーンYAML/地形リージョンYAML/スクリプト/バイトコード）は
// 十分小さいため、これを超えるファイルは破損/攻撃とみなして拒否し OOM を防ぐ。
// メッシュ(GLB)は cgltf が直接読むため本ローダを通らず、この上限の対象外。
static constexpr std::streamsize MAX_FILE_BYTES = 128 * 1024 * 1024; // 128MB

#ifdef _WIN32
#include <windows26.h>
static std::wstring utf8_to_wstring(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}
#endif

std::string FileLoader::readText(const std::string& filePath) {
    if (!AssetGuard::allow(filePath)) return "";
    const std::string normalizedPath = AssetPath::normalize(filePath);
#ifdef _WIN32
    std::ifstream fileStream(utf8_to_wstring(normalizedPath), std::ios::in | std::ios::ate);
#else
    std::ifstream fileStream(normalizedPath, std::ios::in | std::ios::ate);
#endif

    if (!fileStream.is_open()) {
        std::cerr << "[FileLoader] Error: Could not open text file: " << filePath << std::endl;
        return "";
    }

    std::streamsize size = fileStream.tellg();
    if (size > MAX_FILE_BYTES) {
        std::cerr << "[FileLoader] Error: File exceeds size limit ("
                  << size << " > " << MAX_FILE_BYTES << " bytes): " << filePath << std::endl;
        return "";
    }
    fileStream.seekg(0, std::ios::beg);

    std::stringstream sstr;
    sstr << fileStream.rdbuf();
    return sstr.str();
}

std::vector<char> FileLoader::readBinary(const std::string& filePath) {
    if (!AssetGuard::allow(filePath)) return {};
    const std::string normalizedPath = AssetPath::normalize(filePath);
#ifdef _WIN32
    std::ifstream fileStream(utf8_to_wstring(normalizedPath), std::ios::binary | std::ios::ate);
#else
    std::ifstream fileStream(normalizedPath, std::ios::binary | std::ios::ate);
#endif

    if (!fileStream.is_open()) {
        std::cerr << "[FileLoader] Error: Could not open binary file: " << filePath << std::endl;
        return {};
    }

    std::streamsize size = fileStream.tellg();
    if (size < 0) return {};
    if (size > MAX_FILE_BYTES) {
        std::cerr << "[FileLoader] Error: File exceeds size limit ("
                  << size << " > " << MAX_FILE_BYTES << " bytes): " << filePath << std::endl;
        return {};
    }

    fileStream.seekg(0, std::ios::beg);

    std::vector<char> buffer(static_cast<size_t>(size));
    if (!fileStream.read(buffer.data(), size)) {
        std::cerr << "[FileLoader] Error: Failed to read data from: " << filePath << std::endl;
        return {};
    }

    return buffer;
}

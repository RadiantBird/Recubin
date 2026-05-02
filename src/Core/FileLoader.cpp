#include <Core/FileLoader.hpp>
#include <fstream>
#include <sstream>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
static std::wstring utf8_to_wstring(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}
#endif

std::string FileLoader::readText(const std::string& filePath) {
#ifdef _WIN32
    std::ifstream fileStream(utf8_to_wstring(filePath), std::ios::in);
#else
    std::ifstream fileStream(filePath, std::ios::in);
#endif

    if (!fileStream.is_open()) {
        std::cerr << "[FileLoader] Error: Could not open text file: " << filePath << std::endl;
        return "";
    }

    std::stringstream sstr;
    sstr << fileStream.rdbuf();
    return sstr.str();
}

std::vector<char> FileLoader::readBinary(const std::string& filePath) {
#ifdef _WIN32
    std::ifstream fileStream(utf8_to_wstring(filePath), std::ios::binary | std::ios::ate);
#else
    std::ifstream fileStream(filePath, std::ios::binary | std::ios::ate);
#endif

    if (!fileStream.is_open()) {
        std::cerr << "[FileLoader] Error: Could not open binary file: " << filePath << std::endl;
        return {};
    }

    std::streamsize size = fileStream.tellg();
    if (size < 0) return {};

    fileStream.seekg(0, std::ios::beg);

    std::vector<char> buffer(static_cast<size_t>(size));
    if (!fileStream.read(buffer.data(), size)) {
        std::cerr << "[FileLoader] Error: Failed to read data from: " << filePath << std::endl;
        return {};
    }

    return buffer;
}

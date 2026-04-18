#include <Core/FileLoader.hpp>
#include <fstream>
#include <sstream>
#include <iostream>

std::string FileLoader::readText(const std::string& filePath) {
    std::ifstream fileStream(filePath, std::ios::in);

    if (!fileStream.is_open()) {
        std::cerr << "[FileLoader] Error: Could not open text file: " << filePath << std::endl;
        return "";
    }

    std::stringstream sstr;
    sstr << fileStream.rdbuf();
    return sstr.str();
}

std::vector<char> FileLoader::readBinary(const std::string& filePath) {
    std::ifstream fileStream(filePath, std::ios::binary | std::ios::ate);

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

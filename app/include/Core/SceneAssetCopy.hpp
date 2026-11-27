#pragma once
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>
#include <yaml-cpp/yaml.h>

namespace SceneAssetCopy {
struct Result {
    bool success = false;
    std::string error;
    std::unordered_map<std::string, std::string> rewrites;
    std::vector<std::filesystem::path> createdFiles;
};

Result copyDependencies(const YAML::Node& scene,
                        const std::filesystem::path& oldBase,
                        const std::filesystem::path& newBase);
}

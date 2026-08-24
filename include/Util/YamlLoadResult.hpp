#pragma once

#include <yaml-cpp/yaml.h>
#include <string>
#include <string_view>

struct YamlLoadResult {
    YAML::Node node;
    bool success = false;
    bool loadFailed = false;
    std::string error;
};

using GuardedYamlDocument = YamlLoadResult;

struct YamlSaveResult {
    bool success = false;
    std::string error;
};

YamlLoadResult loadYamlText(std::string_view text, std::string_view source);
YamlLoadResult loadYamlFile(const std::string& path);
YamlSaveResult saveYamlFileGuarded(const std::string& path,
                                   const YAML::Node& node,
                                   bool loadFailed);

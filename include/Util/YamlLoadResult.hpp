#pragma once

#include <yaml-cpp/yaml.h>
#include <string>
#include <string_view>

enum class YamlFailureKind {
    None,
    Io,
    Parse,
    Guard,
    Emit,
};

struct YamlLoadResult {
    YAML::Node node;
    bool success = false;
    bool loadFailed = false;
    YamlFailureKind failureKind = YamlFailureKind::None;
    std::string error;
};

using GuardedYamlDocument = YamlLoadResult;

struct YamlSaveResult {
    bool success = false;
    YamlFailureKind failureKind = YamlFailureKind::None;
    std::string error;
};

YamlLoadResult loadYamlText(std::string_view text, std::string_view source);
YamlLoadResult loadYamlFile(const std::string& path);
YamlSaveResult saveYamlFileGuarded(const std::string& path,
                                   const YAML::Node& node,
                                   bool loadFailed);

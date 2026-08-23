#pragma once
#include <filesystem>
#include <string>

struct SystemExtensionPermissions {
    bool io = false;
    bool ipc = false;
    bool external = false;
    static constexpr int SCHEMA_VERSION = 1;
};

namespace SystemExtensionConsent {
bool shouldWarn(const std::filesystem::path& applicationRoot,
                const SystemExtensionPermissions& permissions);
bool read(const std::filesystem::path& applicationRoot,
          SystemExtensionPermissions& permissions);
bool write(const std::filesystem::path& applicationRoot,
           const SystemExtensionPermissions& permissions);
}

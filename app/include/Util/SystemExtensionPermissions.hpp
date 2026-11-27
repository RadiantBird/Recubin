#pragma once
#include <filesystem>
#include <string>

struct SystemExtensionPermissions {
    bool io = false;
    bool ipc = false;
    bool external = false;
    bool executingProcess = false;
    static constexpr int SCHEMA_VERSION = 2;
};

namespace SystemExtensionConsent {
bool shouldWarn(const std::filesystem::path& applicationRoot,
                const SystemExtensionPermissions& permissions);
bool read(const std::filesystem::path& applicationRoot,
          SystemExtensionPermissions& permissions);
bool write(const std::filesystem::path& applicationRoot,
           const SystemExtensionPermissions& permissions);
}

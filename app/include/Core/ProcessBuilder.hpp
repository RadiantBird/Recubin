#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ProcessBuilder {

enum class CompilerKind {
    Msvc,
    ClangCl,
    Gnu,
};

struct BuildRequest {
    std::filesystem::path sourcePath;
    std::filesystem::path dllPath;
    std::filesystem::path assetBase;
    std::vector<std::filesystem::path> headerPaths;
    std::vector<std::filesystem::path> includeDirectories;
    // Compiler executable selected in Studio; required when a rebuild is needed.
    std::filesystem::path compilerPath;
};

struct BuildResult {
    bool success = false;
    bool rebuilt = false;
    bool compilerFound = false;
    int exitCode = -1;
    std::string compiler;
    CompilerKind compilerKind = CompilerKind::Msvc;
    std::string log;
};

std::optional<CompilerKind> compilerKindForPath(const std::filesystem::path& path);

// Builds a Process DLL when it is missing or older than its source/header
// dependencies. The existing DLL is replaced only after a successful build.
BuildResult buildIfNeeded(const BuildRequest& request);

} // namespace ProcessBuilder

#include <Core/SceneAssetCopy.hpp>
#include <Core/AssetDependencies.hpp>
#include <Util/AssetPath.hpp>
#include <fstream>
#include <algorithm>
#include <array>

namespace SceneAssetCopy {
namespace {
struct ByteComparison {
    bool equal = false;
    std::string error;
};

ByteComparison sameBytes(const std::filesystem::path& a,
                         const std::filesystem::path& b) {
    std::error_code ec;
    const auto leftSize = std::filesystem::file_size(a, ec);
    if (ec) {
        return {false, "Cannot inspect asset " + AssetPath::toStored(a) +
                       ": " + ec.message()};
    }
    ec.clear();
    const auto rightSize = std::filesystem::file_size(b, ec);
    if (ec) {
        return {false, "Cannot inspect asset " + AssetPath::toStored(b) +
                       ": " + ec.message()};
    }
    if (leftSize != rightSize) return {false, {}};
    std::ifstream left(a, std::ios::binary), right(b, std::ios::binary);
    if (!left.is_open()) {
        return {false, "Cannot read asset " + AssetPath::toStored(a)};
    }
    if (!right.is_open()) {
        return {false, "Cannot read asset " + AssetPath::toStored(b)};
    }
    std::array<char, 64 * 1024> x{}, y{};
    while (left && right) {
        left.read(x.data(), x.size());
        right.read(y.data(), y.size());
        if (left.gcount() != right.gcount() ||
            !std::equal(x.begin(), x.begin() + left.gcount(), y.begin())) {
            return {false, {}};
        }
    }
    if (!left.eof() || !right.eof()) {
        return {false, "Cannot finish reading assets " + AssetPath::toStored(a) +
                       " and " + AssetPath::toStored(b)};
    }
    return {true, {}};
}

std::filesystem::path suffixedExternalName(
    const std::filesystem::path& name, int suffix, bool directory) {
    if (directory) {
        std::filesystem::path result = name;
        result += "_" + std::to_string(suffix);
        return result;
    }
    const std::filesystem::path stem = name.stem();
    const std::filesystem::path extension = name.extension();
    std::filesystem::path result = stem;
    result += "_" + std::to_string(suffix);
    result += extension;
    return result;
}
}

Result copyDependencies(const YAML::Node& scene, const std::filesystem::path& oldBase,
                        const std::filesystem::path& newBase) {
    Result result;
    std::vector<AssetDependencies::Dependency> dependencies;
    AssetDependencies::collect(scene, dependencies, oldBase);
    for (const auto& dependency : dependencies) {
        if (!dependency.exists) {
            if (dependency.autosave) continue;
            result.error = "Missing asset dependency: " + dependency.source;
            return result;
        }
        const auto files = AssetDependencies::listFiles(dependency, oldBase);
        if (!dependency.directory && files.empty()) {
            result.error = "Cannot enumerate asset dependency: " + dependency.source;
            return result;
        }
        const auto sourceRoot = AssetPath::resolveAgainst(dependency.source, oldBase);
        const auto relative = AssetPath::relativeTo(sourceRoot, oldBase);
        std::filesystem::path destinationRoot;
        if (relative) {
            bool escapes = false;
            for (const auto& component : AssetPath::fromStored(*relative))
                if (component == "..") escapes = true;
            if (!escapes) destinationRoot = newBase / AssetPath::fromStored(*relative);
        }
        if (destinationRoot.empty()) {
            std::filesystem::path externalName = sourceRoot.filename();
            if (externalName.empty()) externalName = "asset";
            destinationRoot = newBase / "external" / externalName;
            for (int suffix = 1;; ++suffix) {
                bool conflict = false;
                for (const auto& entry : files) {
                    const auto candidate = dependency.directory
                        ? destinationRoot / AssetPath::fromStored(entry.relative) : destinationRoot;
                    std::error_code ec;
                    const bool candidateExists = std::filesystem::exists(candidate, ec);
                    if (ec) {
                        result.error = "Cannot inspect asset destination " +
                                       AssetPath::toStored(candidate) + ": " + ec.message();
                        return result;
                    }
                    if (candidateExists) {
                        const auto comparison = sameBytes(candidate, entry.path);
                        if (!comparison.error.empty()) {
                            result.error = comparison.error;
                            return result;
                        }
                        if (!comparison.equal) {
                            conflict = true;
                            break;
                        }
                    }
                }
                if (!conflict) break;
                const std::filesystem::path suffixedName = suffixedExternalName(
                    externalName, suffix, dependency.directory);
                destinationRoot = newBase / "external" / suffixedName;
            }
        }
        std::error_code ec;
        if (dependency.directory) {
            std::filesystem::create_directories(destinationRoot, ec);
        }
        if (ec) {
            result.error = "Cannot create asset directory: " +
                           AssetPath::toStored(destinationRoot) + " (" + ec.message() + ")";
            return result;
        }
        for (const auto& entry : files) {
            const auto destination = dependency.directory
                ? destinationRoot / AssetPath::fromStored(entry.relative) : destinationRoot;
            ec.clear();
            std::filesystem::create_directories(destination.parent_path(), ec);
            if (ec) {
                result.error = "Cannot create asset directory: " +
                               AssetPath::toStored(destination.parent_path()) +
                               " (" + ec.message() + ")";
                return result;
            }
            const bool destinationExists = std::filesystem::exists(destination, ec);
            if (ec) {
                result.error = "Cannot inspect asset destination " +
                               AssetPath::toStored(destination) + ": " + ec.message();
                return result;
            }
            if (destinationExists) {
                const auto comparison = sameBytes(destination, entry.path);
                if (!comparison.error.empty()) {
                    result.error = comparison.error;
                    return result;
                }
                if (!comparison.equal) {
                    result.error = "Asset collision: " +
                                   AssetPath::toStored(destination);
                    return result;
                }
                continue;
            }
            std::filesystem::copy_file(entry.path, destination, std::filesystem::copy_options::none, ec);
            if (ec) {
                result.error = "Cannot copy asset: " + AssetPath::toStored(entry.path) +
                               " to " + AssetPath::toStored(destination) +
                               " (" + ec.message() + ")";
                return result;
            }
            result.createdFiles.push_back(destination);
        }
        const auto stored = AssetPath::relativeTo(destinationRoot, newBase);
        if (!stored) { result.error = "Asset destination is not relative to scene"; return result; }
        result.rewrites[dependency.source] = *stored;
    }
    result.success = true;
    return result;
}
}

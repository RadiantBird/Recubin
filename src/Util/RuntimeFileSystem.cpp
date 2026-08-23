#include <Util/RuntimeFileSystem.hpp>
#include <Util/IPlatform.hpp>
#include <Util/Platform.hpp>
#include <Util/AssetGuard.hpp>
#include <Util/UUID.hpp>
#include <algorithm>
#include <fstream>
#include <cstdlib>
#include <functional>

namespace fs = std::filesystem;
namespace {
constexpr std::uintmax_t MAX_FILE_SIZE = 128u * 1024u * 1024u;

bool isWithin(const fs::path& root, const fs::path& candidate) {
    auto rootIt = root.begin();
    auto candidateIt = candidate.begin();
    for (; rootIt != root.end(); ++rootIt, ++candidateIt) {
        if (candidateIt == candidate.end() || *rootIt != *candidateIt) return false;
    }
    return true;
}

RuntimeFileResult failure(std::string message) {
    return RuntimeFileResult{false, {}, std::move(message)};
}
}

RuntimeFileSystem::RuntimeFileSystem(std::string applicationId, Namespace space,
                                     bool externalAllowed, fs::path base)
    : m_external(externalAllowed) {
    m_invalidApplicationId = !RecubinUUID::isValid(applicationId);
    m_userDataRoot = base.empty() ? getPlatform().userDataRoot() : base;
    m_base = m_userDataRoot.lexically_normal();
    m_applicationRoot = (m_base / applicationId).lexically_normal();
    m_root = (m_applicationRoot / (space == Namespace::Runtime ? "runtime" : "editor")).lexically_normal();
    std::error_code error;
    fs::create_directories(m_root, error);
}

fs::path RuntimeFileSystem::resolve(const std::string& raw, bool allowMissing,
                                    std::string& error) const {
    if (m_invalidApplicationId) {
        error = "invalid application id";
        return {};
    }
    if (raw.empty()) { error = "empty path"; return {}; }
    const fs::path input(raw);
    if (input.is_absolute() && !m_external) {
        error = "absolute paths require external file access";
        return {};
    }
    if (!input.is_absolute()) {
        for (const auto& part : input) {
            if (part == "..") { error = "parent traversal is forbidden"; return {}; }
        }
    }
    const fs::path candidate = input.is_absolute() ? input : m_root / input;
    std::error_code canonicalError;
    fs::path resolved = fs::weakly_canonical(candidate, canonicalError);
    if (canonicalError) {
        error = "cannot canonicalize path: " + canonicalError.message();
        return {};
    }
    if (!input.is_absolute() && !isWithin(m_root, resolved)) {
        error = "path escapes namespace";
        return {};
    }
    if (!allowMissing) {
        std::error_code existsError;
        if (!fs::exists(resolved, existsError)) {
            error = existsError ? existsError.message() : "path does not exist";
            return {};
        }
    }
    return resolved;
}

RuntimeFileResult RuntimeFileSystem::read(const std::string& path) const {
    std::string error;
    const fs::path resolved = resolve(path, false, error);
    if (resolved.empty()) return failure(error);
    std::error_code statusError;
    if (!fs::is_regular_file(resolved, statusError)) return failure("not a regular file");
    std::ifstream input(resolved, std::ios::binary);
    if (!input) return failure("read failed");
    std::string data((std::istreambuf_iterator<char>(input)), {});
    if (data.size() > MAX_FILE_SIZE) return failure("file exceeds 128 MiB");
    RuntimeFileResult result{true, std::move(data), {}};
    result.exists = true; result.isFile = true; result.size = result.value.size();
    return result;
}

RuntimeFileResult RuntimeFileSystem::write(const std::string& path, const std::string& data) {
    if (data.size() > MAX_FILE_SIZE) return failure("file exceeds 128 MiB");
    std::string error;
    const fs::path resolved = resolve(path, true, error);
    if (resolved.empty()) return failure(error);
    std::error_code directoryError;
    fs::create_directories(resolved.parent_path(), directoryError);
    if (directoryError) return failure(directoryError.message());
    fs::path temporary = resolved; temporary += ".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) return failure("write failed");
    output.write(data.data(), static_cast<std::streamsize>(data.size()));
    output.close();
    if (!output) { std::error_code ignored; fs::remove(temporary, ignored); return failure("write failed"); }
    fs::path backup = resolved;
    backup += ".backup-" + std::to_string(std::hash<std::string>{}(resolved.string()));
    std::error_code backupError;
    const bool hadDestination = fs::exists(resolved, backupError);
    if (backupError) { std::error_code ignored; fs::remove(temporary, ignored); return failure(backupError.message()); }
    fs::remove(backup, backupError);
    if (backupError) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        return failure(backupError.message());
    }
    if (hadDestination) {
        fs::rename(resolved, backup, backupError);
        if (backupError) { std::error_code ignored; fs::remove(temporary, ignored); return failure(backupError.message()); }
    }
    std::error_code renameError;
    fs::rename(temporary, resolved, renameError);
    if (renameError) {
        std::error_code restoreError;
        if (hadDestination) fs::rename(backup, resolved, restoreError);
        std::error_code cleanupError;
        fs::remove(temporary, cleanupError);
        if (restoreError) return failure("atomic replace failed; restore failed: " + restoreError.message());
        return failure(renameError.message());
    }
    if (hadDestination) { std::error_code ignored; fs::remove(backup, ignored); }
    return RuntimeFileResult{true};
}

RuntimeFileResult RuntimeFileSystem::append(const std::string& path, const std::string& data) {
    RuntimeFileResult current = read(path);
    if (!current && current.error != "path does not exist") return current;
    if (current.value.size() > MAX_FILE_SIZE - data.size()) return failure("file exceeds 128 MiB");
    return write(path, current.value + data);
}

RuntimeFileResult RuntimeFileSystem::inspect(const std::string& path) const {
    std::string error;
    const fs::path resolved = resolve(path, true, error);
    if (resolved.empty()) return failure(error);
    std::error_code statusError;
    const bool present = fs::exists(resolved, statusError);
    if (statusError) return failure(statusError.message());
    RuntimeFileResult result{true}; result.exists = present;
    if (present) {
        result.isFile = fs::is_regular_file(resolved, statusError);
        result.isDirectory = fs::is_directory(resolved, statusError);
        if (result.isFile) result.size = fs::file_size(resolved, statusError);
    }
    return statusError ? failure(statusError.message()) : result;
}
RuntimeFileResult RuntimeFileSystem::exists(const std::string& path) const { return inspect(path); }
RuntimeFileResult RuntimeFileSystem::isFile(const std::string& path) const { auto r=inspect(path); r.exists = r.success && r.isFile; return r; }
RuntimeFileResult RuntimeFileSystem::isDirectory(const std::string& path) const { auto r=inspect(path); r.exists = r.success && r.isDirectory; return r; }

RuntimeFileResult RuntimeFileSystem::list(const std::string& path, std::vector<RuntimeFileEntry>& entries) const {
    std::string error; const fs::path resolved = resolve(path, false, error);
    if (resolved.empty()) return failure(error);
    std::error_code iteratorError; fs::directory_iterator iterator(resolved, iteratorError);
    if (iteratorError) return failure(iteratorError.message());
    for (const auto& item : iterator) {
        std::error_code itemError; RuntimeFileEntry entry;
        entry.name = item.path().filename().string(); entry.path = item.path().lexically_relative(m_root).generic_string();
        entry.isDirectory = item.is_directory(itemError); if (itemError) return failure(itemError.message());
        if (!entry.isDirectory) entry.size = item.file_size(itemError);
        if (itemError) return failure(itemError.message()); entries.push_back(std::move(entry));
    }
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
    return RuntimeFileResult{true};
}

RuntimeFileResult RuntimeFileSystem::createDirectory(const std::string& path) { std::string e; auto p=resolve(path,true,e); if(p.empty())return failure(e); std::error_code c; fs::create_directories(p,c); return c?failure(c.message()):RuntimeFileResult{true}; }
RuntimeFileResult RuntimeFileSystem::copy(const std::string& source,const std::string& destination,bool overwrite){std::string e;auto a=resolve(source,false,e),b=resolve(destination,true,e);if(a.empty()||b.empty())return failure(e);std::error_code c;fs::copy_file(a,b,overwrite?fs::copy_options::overwrite_existing:fs::copy_options::none,c);return c?failure(c.message()):RuntimeFileResult{true};}
RuntimeFileResult RuntimeFileSystem::move(const std::string& source,const std::string& destination,bool overwrite){std::string e;auto a=resolve(source,false,e),b=resolve(destination,true,e);if(a.empty()||b.empty())return failure(e);std::error_code c;if(overwrite){fs::remove(b,c);if(c)return failure(c.message());}fs::rename(a,b,c);return c?failure(c.message()):RuntimeFileResult{true};}
RuntimeFileResult RuntimeFileSystem::remove(const std::string& path){std::string e;auto p=resolve(path,false,e);if(p.empty())return failure(e);if(p==m_root)return failure("cannot remove namespace root");std::error_code c;if(fs::is_directory(p,c)&&!fs::is_empty(p,c))return failure("directory is not empty");fs::remove(p,c);return c?failure(c.message()):RuntimeFileResult{true};}
RuntimeFileResult RuntimeFileSystem::removeTree(const std::string& path){std::string e;auto p=resolve(path,false,e);if(p.empty())return failure(e);const char* homeValue=std::getenv("HOME");const char* profileValue=std::getenv("USERPROFILE");const fs::path home=homeValue?fs::path(homeValue):(profileValue?fs::path(profileValue):fs::path{});if(p==m_root||p==m_applicationRoot||p==m_base||p==m_userDataRoot||(!home.empty()&&p==home)||p==p.root_path())return failure("cannot remove protected root");std::error_code c;fs::remove_all(p,c);return c?failure(c.message()):RuntimeFileResult{true};}

RuntimeFileResult RuntimeFileSystem::readTextFile(const std::string& seedPath,
                                                  const std::string& storageId) {
    if (!RecubinUUID::isValid(storageId)) return failure("invalid storage id");
    const std::string relative = "textfiles/" + storageId + ".txt";
    const auto overlay = read(relative);
    if (overlay) return overlay;
    if (overlay.error != "path does not exist") return overlay;
    if (!AssetGuard::allow(seedPath)) return failure("seed path denied");
    std::ifstream seed(seedPath, std::ios::binary);
    if (!seed) return failure("seed read failed");
    std::string data((std::istreambuf_iterator<char>(seed)), {});
    if (data.size() > MAX_FILE_SIZE) return failure("file exceeds 128 MiB");
    const auto saved = write(relative, data);
    if (!saved) return saved;
    return RuntimeFileResult{true, std::move(data), {}};
}
RuntimeFileResult RuntimeFileSystem::writeTextFile(const std::string& storageId,const std::string& content){if(!RecubinUUID::isValid(storageId))return failure("invalid storage id");return write("textfiles/"+storageId+".txt",content);}

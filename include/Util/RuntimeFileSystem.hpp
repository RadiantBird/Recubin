#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct RuntimeFileResult {
    bool success = false;
    std::string value;
    std::string error;
    bool exists = false;
    bool isFile = false;
    bool isDirectory = false;
    std::uintmax_t size = 0;
    explicit operator bool() const { return success; }
};
struct RuntimeFileEntry { std::string name; std::string path; bool isDirectory = false; std::uintmax_t size = 0; };

class RuntimeFileSystem {
public:
    enum class Namespace { Runtime, Editor };
    RuntimeFileSystem(std::string applicationId, Namespace space,
                      bool externalAllowed = false, std::filesystem::path base = {});
    RuntimeFileResult read(const std::string& path) const;
    RuntimeFileResult write(const std::string& path, const std::string& data);
    RuntimeFileResult append(const std::string& path, const std::string& data);
    RuntimeFileResult exists(const std::string& path) const;
    RuntimeFileResult isFile(const std::string& path) const;
    RuntimeFileResult isDirectory(const std::string& path) const;
    RuntimeFileResult list(const std::string& path, std::vector<RuntimeFileEntry>& entries) const;
    RuntimeFileResult createDirectory(const std::string& path);
    RuntimeFileResult copy(const std::string& source, const std::string& destination, bool overwrite = false);
    RuntimeFileResult move(const std::string& source, const std::string& destination, bool overwrite = false);
    RuntimeFileResult remove(const std::string& path);
    RuntimeFileResult removeTree(const std::string& path);
    RuntimeFileResult readTextFile(const std::string& seedPath, const std::string& storageId);
    RuntimeFileResult writeTextFile(const std::string& storageId, const std::string& content);
    void setExternalAllowed(bool allowed) { m_external = allowed; }
    std::filesystem::path namespaceRoot() const { return m_root; }
private:
    std::filesystem::path resolve(const std::string& path, bool allowMissing, std::string& error) const;
    RuntimeFileResult inspect(const std::string& path) const;
    std::filesystem::path m_root, m_base, m_userDataRoot, m_applicationRoot;
    bool m_external = false;
    bool m_invalidApplicationId = false;
};

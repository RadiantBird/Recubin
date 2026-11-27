#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class CodeEditorPanel;
class Instance;
class Script;

class EmbeddedScriptWorkspace {
public:
    explicit EmbeddedScriptWorkspace(std::filesystem::path storageRoot);

    bool ensureWorkingFile(const std::shared_ptr<Script>& script, std::string& error);
    bool saveEditorBuffer(const std::shared_ptr<Script>& script,
                          const std::string& source, std::string& error);
    bool hasExternalChanges(Instance* root, bool& changed, std::string& error) const;
    bool synchronize(Instance* root,
                     const std::function<CodeEditorPanel*(Script*)>& findEditor,
                     bool& changed, std::string& error);
    void markSceneDirty();
    void markSceneSaved(Instance* root);
    bool cleanupAfterNormalExit(std::string& error);
    bool hasTrackedFiles() const;

private:
    struct Entry {
        std::weak_ptr<Script> script;
        std::filesystem::path path;
        std::string lastSyncedContents;
        bool savedInScene = false;
        bool hasUnsavedChanges = false;
    };

    std::filesystem::path m_storageRoot;
    std::unordered_map<Script*, Entry> m_entries;
    std::vector<std::filesystem::path> m_createdFiles;
    bool m_sceneSavedSinceLastChange = false;

    bool createWorkingFile(const std::shared_ptr<Script>& script,
                           std::filesystem::path& path, std::string& error);
    static bool readText(const std::filesystem::path& path, std::string& text,
                         std::string& error);
    static bool writeText(const std::filesystem::path& path, const std::string& text,
                          std::string& error);
};

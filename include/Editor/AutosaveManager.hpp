#pragma once

#include <Core/SceneLoader.hpp>
#include <Util/AssetPath.hpp>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

class AutosaveManager {
public:
    struct Config {
        std::chrono::milliseconds recoveryDebounce{1000};
        std::chrono::seconds snapshotInterval{300};
        std::chrono::seconds retryBackoff{5};
        std::size_t snapshotCount = 5;
    };

    struct RecoveryCandidate {
        std::filesystem::path directory;
        std::filesystem::path recoveryPath;
        std::filesystem::path lockPath;
        std::string logicalScenePath;
        bool untitled = false;
        std::filesystem::file_time_type modified{};
    };

    explicit AutosaveManager(std::filesystem::path storageRoot);
    AutosaveManager(std::filesystem::path storageRoot, Config config);
    ~AutosaveManager();

    bool beginSession(Instance* root, const std::string& logicalScenePath);
    bool beginSession(Instance* root, const std::string& logicalScenePath,
                      const SceneLoader::SceneDocumentMetadata& metadata);
    bool beginSession(Instance* root, const std::filesystem::path& logicalScenePath);
    bool beginSession(Instance* root, const std::filesystem::path& logicalScenePath,
                      const SceneLoader::SceneDocumentMetadata& metadata);
    void endSessionNormally();
    void markSceneChanged();
    void setRoot(Instance* root);
    void setSceneMetadata(const SceneLoader::SceneDocumentMetadata& metadata);
    bool update();
    bool update(std::chrono::steady_clock::time_point now);
    bool flushRecovery();

    std::vector<RecoveryCandidate> findCrashRecoveries() const;
    std::shared_ptr<RecoveryCandidate> findLatestCrashRecovery() const;
    std::shared_ptr<RecoveryCandidate> findCrashRecoveryForScene(
        const std::string& logicalScenePath) const;
    bool validateRecoveryCandidate(const RecoveryCandidate& candidate) const;
    bool discardRecovery(const RecoveryCandidate& candidate) const;

    bool isActive() const { return m_active; }
    bool isRecoveryDirty() const { return m_recoveryDirty; }
    bool isSnapshotDirty() const { return m_snapshotDirty; }
    const std::filesystem::path& logicalScenePath() const { return m_logicalScenePath; }
    const std::filesystem::path& autosaveDirectory() const { return m_sessionDirectory; }

private:
    bool saveRecovery(std::chrono::steady_clock::time_point now);
    bool saveSnapshot(std::chrono::steady_clock::time_point now);
    bool atomicWrite(const std::filesystem::path& target, const std::string& data) const;
    bool writeLock() const;
    static bool readCandidate(const std::filesystem::path& directory,
                              RecoveryCandidate& candidate);
    static std::string normalizedSceneName(const std::filesystem::path& path);
    void cleanupTransientFiles(const std::filesystem::path& directory) const;
    void resetTimers(std::chrono::steady_clock::time_point now);

    std::filesystem::path m_storageRoot;
    std::filesystem::path m_logicalScenePath;
    std::filesystem::path m_sessionDirectory;
    Config m_config;
    SceneLoader::SceneDocumentMetadata m_metadata;
    Instance* m_root = nullptr;
    bool m_active = false;
    bool m_recoveryDirty = false;
    bool m_snapshotDirty = false;
    std::chrono::steady_clock::time_point m_lastChange{};
    std::chrono::steady_clock::time_point m_lastSnapshot{};
    std::chrono::steady_clock::time_point m_lastRecoveryAttempt{};
    std::chrono::steady_clock::time_point m_lastSnapshotAttempt{};
};

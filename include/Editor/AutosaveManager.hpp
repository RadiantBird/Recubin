#pragma once

#include <Core/SceneLoader.hpp>
#include <Util/AssetPath.hpp>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <unordered_set>

class AutosaveManager {
public:
    using IoFailureCallback = std::function<void(
        const std::string& operation, const std::filesystem::path& path,
        const std::string& reason)>;
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
    void setIoFailureCallback(IoFailureCallback callback);

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
    bool atomicWrite(const std::filesystem::path& target, const std::string& data,
                     const std::string& operation) const;
    bool writeLock() const;
    bool readCandidate(const std::filesystem::path& directory,
                       RecoveryCandidate& candidate) const;
    static std::string normalizedSceneName(const std::filesystem::path& path);
    void cleanupTransientFiles(const std::filesystem::path& directory) const;
    void resetTimers(std::chrono::steady_clock::time_point now);
    void notifyIoFailure(const std::string& operation, const std::filesystem::path& path,
                         const std::string& reason) const;
    void notifyIoSuccess(const std::string& operation,
                         const std::filesystem::path& path) const;

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
    IoFailureCallback m_ioFailureCallback;
    mutable std::unordered_set<std::string> m_reportedIoFailures;
};

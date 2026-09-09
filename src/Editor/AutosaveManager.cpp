#include <Editor/AutosaveManager.hpp>
#include <Core/FileLoader.hpp>
#include <Util/Logger.hpp>
#include <fstream>
#include <algorithm>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {
using Clock = std::chrono::steady_clock;
std::string pathString(const std::filesystem::path& p) { return AssetPath::toStored(p); }

enum class RegularFileState { Regular, Missing, NotRegular, Error };

RegularFileState inspectRegularFile(const std::filesystem::path& path, std::error_code& ec) {
    ec.clear();
    const auto status = std::filesystem::status(path, ec);
    if (ec) {
        if (ec == std::errc::no_such_file_or_directory) {
            ec.clear();
            return RegularFileState::Missing;
        }
        return RegularFileState::Error;
    }
    if (!std::filesystem::exists(status)) return RegularFileState::Missing;
    if (!std::filesystem::is_regular_file(status)) return RegularFileState::NotRegular;
    return RegularFileState::Regular;
}
}

AutosaveManager::AutosaveManager(std::filesystem::path projectRoot)
    : AutosaveManager(std::move(projectRoot), Config{}) {}

AutosaveManager::AutosaveManager(std::filesystem::path projectRoot, Config config)
    : m_projectRoot(std::move(projectRoot)), m_config(config) {
    std::error_code ec;
    m_projectRoot = std::filesystem::absolute(m_projectRoot, ec);
    if (ec) m_projectRoot = std::filesystem::current_path();
}

AutosaveManager::~AutosaveManager() = default;

std::string AutosaveManager::normalizedSceneName(const std::filesystem::path& path) {
    if (path.empty()) return "Untitled.rcbn";
    auto name = AssetPath::toStored(path.filename());
    if (name.empty()) return "Untitled.rcbn";
    return name;
}

void AutosaveManager::resetTimers(Clock::time_point now) {
    m_lastChange = now;
    m_lastSnapshot = now;
    m_lastRecoveryAttempt = Clock::time_point{};
    m_lastSnapshotAttempt = Clock::time_point{};
}

bool AutosaveManager::beginSession(Instance* root, const std::string& logicalScenePath) {
    return beginSession(root, logicalScenePath, SceneLoader::SceneDocumentMetadata{});
}

bool AutosaveManager::beginSession(Instance* root, const std::string& logicalScenePath,
                                   const SceneLoader::SceneDocumentMetadata& metadata) {
    return beginSession(root, AssetPath::fromStored(logicalScenePath), metadata);
}

bool AutosaveManager::beginSession(Instance* root, const std::filesystem::path& logicalScenePath,
                                   const SceneLoader::SceneDocumentMetadata& metadata) {
    try {
        if (m_active) endSessionNormally();
        m_root = root;
        m_metadata = metadata;
        std::error_code ec;
        m_logicalScenePath = logicalScenePath.empty() ? std::filesystem::path{} :
            std::filesystem::absolute(logicalScenePath, ec);
        if (ec) m_logicalScenePath = logicalScenePath;
        m_sessionDirectory = m_projectRoot / ".autosave" / normalizedSceneName(m_logicalScenePath);
        std::filesystem::create_directories(m_sessionDirectory, ec);
        if (ec) {
            RCBN_ERROR("Autosave: cannot create " << pathString(m_sessionDirectory) << ": " << ec.message());
            return false;
        }
        m_active = true;
        m_recoveryDirty = false;
        m_snapshotDirty = false;
        resetTimers(Clock::now());
        cleanupTransientFiles(m_sessionDirectory);
        if (!writeLock()) {
            RCBN_ERROR("Autosave: failed to write session lock " << pathString(m_sessionDirectory));
            m_active = false;
            m_root = nullptr;
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        RCBN_ERROR("Autosave beginSession failed: " << e.what());
    } catch (...) { RCBN_ERROR("Autosave beginSession failed: unknown error"); }
    m_active = false;
    return false;
}

bool AutosaveManager::beginSession(Instance* root, const std::filesystem::path& logicalScenePath) {
    return beginSession(root, logicalScenePath, SceneLoader::SceneDocumentMetadata{});
}

void AutosaveManager::setRoot(Instance* root) { m_root = root; }

void AutosaveManager::setSceneMetadata(const SceneLoader::SceneDocumentMetadata& metadata) {
    m_metadata = metadata;
}

void AutosaveManager::markSceneChanged() {
    if (!m_active) return;
    m_recoveryDirty = true;
    m_snapshotDirty = true;
    m_lastChange = Clock::now();
}

bool AutosaveManager::writeLock() const {
    try {
        YAML::Emitter emitter;
        emitter << YAML::BeginMap << YAML::Key << "ScenePath" << YAML::Value;
        if (m_logicalScenePath.empty()) emitter << "";
        else emitter << YAML::DoubleQuoted << AssetPath::toStored(m_logicalScenePath);
        emitter << YAML::Key << "Untitled" << YAML::Value << m_logicalScenePath.empty()
                << YAML::EndMap;
        if (!emitter.good()) {
            RCBN_ERROR("Autosave: lock YAML emitter failed");
            return false;
        }
        return atomicWrite(m_sessionDirectory / "session.lock", emitter.c_str());
    } catch (const std::exception& e) {
        RCBN_ERROR("Autosave: lock serialization failed: " << e.what());
    } catch (...) { RCBN_ERROR("Autosave: lock serialization failed"); }
    return false;
}

bool AutosaveManager::atomicWrite(const std::filesystem::path& target, const std::string& data) const {
    try {
        std::filesystem::path tmp = target;
        tmp += ".tmp";
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) { RCBN_ERROR("Autosave: cannot open " << pathString(target) << ".tmp"); return false; }
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
        out.flush();
        if (!out) { RCBN_ERROR("Autosave: write failed " << pathString(target) << ".tmp"); return false; }
        out.close();
#ifdef _WIN32
        const auto wt = target.wstring(); const auto wm = tmp.wstring();
        if (std::filesystem::exists(target)) {
            if (!ReplaceFileW(wt.c_str(), wm.c_str(), nullptr, REPLACEFILE_WRITE_THROUGH, nullptr, nullptr)) {
                const auto replaceError = GetLastError();
                if (!MoveFileExW(wm.c_str(), wt.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                    RCBN_ERROR("Autosave: ReplaceFileW and MoveFileExW failed for " << pathString(target)
                               << " (errors " << replaceError << ", " << GetLastError() << ")");
                    return false;
                }
            }
        } else if (!MoveFileExW(wm.c_str(), wt.c_str(), MOVEFILE_WRITE_THROUGH)) {
            RCBN_ERROR("Autosave: MoveFileExW failed for " << pathString(target)
                       << " (error " << GetLastError() << ")");
            return false;
        }
#else
        std::error_code ec; std::filesystem::rename(tmp, target, ec);
        if (ec) { RCBN_ERROR("Autosave: atomic replace failed " << pathString(target) << ": " << ec.message()); return false; }
#endif
        return true;
    } catch (const std::exception& e) { RCBN_ERROR("Autosave write failed " << pathString(target) << ": " << e.what()); }
    catch (...) { RCBN_ERROR("Autosave write failed " << pathString(target)); }
    return false;
}

bool AutosaveManager::saveRecovery(Clock::time_point now) {
    if (!m_root) { RCBN_ERROR("Autosave: recovery save skipped because root is null"); m_lastRecoveryAttempt = now; return false; }
    const auto serialized = SceneLoader::serializeSceneResult(m_root, m_metadata);
    if (!serialized) { RCBN_ERROR("Autosave recovery serialization failed: " << serialized.message); m_lastRecoveryAttempt = now; return false; }
    const bool ok = atomicWrite(m_sessionDirectory / "recovery.rcbn", serialized.yaml);
    if (!ok) m_lastRecoveryAttempt = now;
    return ok;
}

bool AutosaveManager::saveSnapshot(Clock::time_point now) {
    m_lastSnapshotAttempt = now;
    if (!m_root) { RCBN_ERROR("Autosave: snapshot save skipped because root is null"); return false; }
    if (m_config.snapshotCount == 0) { RCBN_ERROR("Autosave: snapshot count is zero"); return false; }
    const auto serialized = SceneLoader::serializeSceneResult(m_root, m_metadata);
    if (!serialized) { RCBN_ERROR("Autosave snapshot serialization failed: " << serialized.message); return false; }
    std::vector<std::filesystem::path> slots;
    std::error_code ec;
    std::filesystem::path selected;
    for (std::size_t i = 0; i < m_config.snapshotCount; ++i) {
        auto candidate = m_sessionDirectory / ("snapshot_" + std::to_string(i) + ".rcbn");
        if (!std::filesystem::exists(candidate, ec)) { selected = candidate; break; }
        if (ec) { RCBN_ERROR("Autosave: snapshot existence check failed for " << pathString(candidate) << ": " << ec.message()); return false; }
        slots.push_back(candidate);
    }
    if (selected.empty() && slots.empty()) { RCBN_ERROR("Autosave: no snapshot slot available"); return false; }
    if (selected.empty()) {
        std::filesystem::path oldest;
        std::filesystem::file_time_type oldestTime{};
        bool haveOldest = false;
        for (const auto& slot : slots) {
            std::error_code mtimeError;
            const auto mtime = std::filesystem::last_write_time(slot, mtimeError);
            if (mtimeError) {
                RCBN_ERROR("Autosave: cannot read snapshot mtime " << pathString(slot) << ": " << mtimeError.message());
                return false;
            }
            if (!haveOldest || mtime < oldestTime) {
                oldest = slot;
                oldestTime = mtime;
                haveOldest = true;
            }
        }
        if (!haveOldest) {
            RCBN_ERROR("Autosave: no valid snapshot slot available");
            return false;
        }
        selected = oldest;
    }
    const bool ok = atomicWrite(selected, serialized.yaml);
    if (ok) m_lastSnapshot = now;
    return ok;
}

bool AutosaveManager::flushRecovery() {
    if (!m_active || !m_recoveryDirty) return true;
    const bool ok = saveRecovery(Clock::now());
    if (ok) m_recoveryDirty = false;
    return ok;
}

bool AutosaveManager::update() { return update(Clock::now()); }

bool AutosaveManager::update(Clock::time_point now) {
    if (!m_active) return false;
    bool changed = false;
    if (m_recoveryDirty && now - m_lastChange >= m_config.recoveryDebounce &&
        (m_lastRecoveryAttempt == Clock::time_point{} || now - m_lastRecoveryAttempt >= m_config.retryBackoff)) {
        if (saveRecovery(now)) { m_recoveryDirty = false; changed = true; }
    }
    if (m_snapshotDirty && now - m_lastSnapshot >= m_config.snapshotInterval &&
        (m_lastSnapshotAttempt == Clock::time_point{} || now - m_lastSnapshotAttempt >= m_config.retryBackoff)) {
        if (saveSnapshot(now)) { m_snapshotDirty = false; changed = true; }
    }
    return changed;
}

void AutosaveManager::cleanupTransientFiles(const std::filesystem::path& directory) const {
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(directory, ec)) {
        if (e.path().extension() == ".tmp") {
            std::filesystem::remove(e.path(), ec);
            if (ec) RCBN_ERROR("Autosave: failed to remove temporary file " << pathString(e.path()) << ": " << ec.message());
            else RCBN_LOG("Autosave removed stale temporary file " << pathString(e.path()));
        }
    }
    if (ec) RCBN_ERROR("Autosave: temporary-file directory iteration failed for " << pathString(directory) << ": " << ec.message());
}

void AutosaveManager::endSessionNormally() {
    if (!m_active) return;
    std::error_code ec;
    std::filesystem::remove(m_sessionDirectory / "session.lock", ec);
    if (ec) RCBN_ERROR("Autosave: failed to remove session lock: " << ec.message());
    std::filesystem::remove(m_sessionDirectory / "recovery.rcbn", ec);
    if (ec) RCBN_ERROR("Autosave: failed to remove recovery: " << ec.message());
    cleanupTransientFiles(m_sessionDirectory);
    m_active = false; m_recoveryDirty = false; m_snapshotDirty = false; m_root = nullptr;
}

bool AutosaveManager::readCandidate(const std::filesystem::path& directory, RecoveryCandidate& candidate) {
    try {
        const auto lock = directory / "session.lock";
        const auto recovery = directory / "recovery.rcbn";
        std::error_code lockError;
        const auto lockState = inspectRegularFile(lock, lockError);
        if (lockState == RegularFileState::Error) {
            RCBN_ERROR("Autosave: cannot inspect lock " << pathString(lock) << ": " << lockError.message());
            return false;
        }
        if (lockState == RegularFileState::Missing) {
            RCBN_ERROR("Autosave: recovery candidate rejected; missing lock " << pathString(lock));
            return false;
        }
        if (lockState == RegularFileState::NotRegular) {
            RCBN_ERROR("Autosave: recovery candidate rejected; lock is not a regular file " << pathString(lock));
            return false;
        }
        std::error_code recoveryError;
        const auto recoveryState = inspectRegularFile(recovery, recoveryError);
        if (recoveryState == RegularFileState::Error) {
            RCBN_ERROR("Autosave: cannot inspect recovery " << pathString(recovery) << ": " << recoveryError.message());
            return false;
        }
        if (recoveryState == RegularFileState::Missing) return false;
        if (recoveryState == RegularFileState::NotRegular) {
            RCBN_ERROR("Autosave: recovery candidate rejected; recovery is not a regular file " << pathString(recovery));
            return false;
        }
        const auto lockText = FileLoader::readText(AssetPath::toStored(lock));
        if (lockText.empty()) {
            RCBN_ERROR("Autosave: cannot read lock " << pathString(lock));
            return false;
        }
        YAML::Node node = YAML::Load(lockText);
        candidate.directory = directory; candidate.lockPath = lock; candidate.recoveryPath = recovery;
        candidate.logicalScenePath = node["ScenePath"] ? node["ScenePath"].as<std::string>() : "";
        candidate.untitled = node["Untitled"] && node["Untitled"].as<bool>();
        auto loaded = SceneLoader::loadSceneResult(AssetPath::toStored(recovery));
        if (!loaded) { RCBN_ERROR("Autosave: corrupt recovery " << pathString(recovery) << ": " << loaded.message); return false; }
        std::error_code mtimeError;
        candidate.modified = std::filesystem::last_write_time(recovery, mtimeError);
        if (mtimeError) {
            RCBN_ERROR("Autosave: cannot read recovery mtime " << pathString(recovery) << ": " << mtimeError.message());
            return false;
        }
        return true;
    } catch (const std::exception& e) { RCBN_ERROR("Autosave candidate rejected " << pathString(directory) << ": " << e.what()); }
    catch (...) { RCBN_ERROR("Autosave candidate rejected " << pathString(directory)); }
    return false;
}

std::vector<AutosaveManager::RecoveryCandidate> AutosaveManager::findCrashRecoveries() const {
    std::vector<RecoveryCandidate> result; std::error_code ec;
    const auto root = m_projectRoot / ".autosave";
    if (!std::filesystem::is_directory(root, ec)) return result;
    for (const auto& e : std::filesystem::directory_iterator(root, ec)) {
        if (ec) break;
        std::error_code entryError;
        if (!e.is_directory(entryError)) {
            if (entryError) RCBN_ERROR("Autosave: candidate directory check failed for " << pathString(e.path()) << ": " << entryError.message());
            continue;
        }
        if (m_active && e.path().lexically_normal() == m_sessionDirectory.lexically_normal()) continue;
        std::error_code lockPresenceError;
        const auto lockPath = e.path() / "session.lock";
        const auto lockState = inspectRegularFile(lockPath, lockPresenceError);
        if (lockState == RegularFileState::Missing) continue;
        if (lockState == RegularFileState::Error) {
            RCBN_ERROR("Autosave: cannot inspect lock " << pathString(lockPath) << ": " << lockPresenceError.message());
            continue;
        }
        if (lockState == RegularFileState::NotRegular) {
            RCBN_ERROR("Autosave: recovery candidate rejected; lock is not a regular file " << pathString(lockPath));
            continue;
        }
        RecoveryCandidate c; if (readCandidate(e.path(), c)) result.push_back(std::move(c));
    }
    if (ec) RCBN_ERROR("Autosave: recovery directory iteration failed for " << pathString(root) << ": " << ec.message());
    return result;
}

std::shared_ptr<AutosaveManager::RecoveryCandidate> AutosaveManager::findLatestCrashRecovery() const {
    auto all = findCrashRecoveries(); if (all.empty()) return nullptr;
    auto it = std::max_element(all.begin(), all.end(), [](const auto& a, const auto& b) { return a.modified < b.modified; });
    return std::make_shared<RecoveryCandidate>(*it);
}

std::shared_ptr<AutosaveManager::RecoveryCandidate> AutosaveManager::findCrashRecoveryForScene(const std::string& path) const {
    if (path.empty()) {
        for (auto& c : findCrashRecoveries()) if (c.untitled && c.logicalScenePath.empty()) return std::make_shared<RecoveryCandidate>(c);
        return nullptr;
    }
    std::error_code ec;
    const auto absolutePath = std::filesystem::absolute(AssetPath::fromStored(path), ec);
    if (ec) { RCBN_ERROR("Autosave: cannot normalize scene path " << path << ": " << ec.message()); return nullptr; }
    const auto absolute = AssetPath::toStored(absolutePath.lexically_normal());
    for (auto& c : findCrashRecoveries()) {
        std::error_code candidateError;
        const auto candidatePath = std::filesystem::absolute(AssetPath::fromStored(c.logicalScenePath), candidateError);
        if (!candidateError && AssetPath::toStored(candidatePath.lexically_normal()) == absolute)
            return std::make_shared<RecoveryCandidate>(c);
    }
    return nullptr;
}

bool AutosaveManager::validateRecoveryCandidate(const RecoveryCandidate& candidate) const {
    RecoveryCandidate current;
    if (!readCandidate(candidate.directory, current)) return false;
    std::error_code mtimeError;
    const auto currentMtime = std::filesystem::last_write_time(candidate.recoveryPath, mtimeError);
    if (mtimeError) {
        RCBN_ERROR("Autosave: cannot revalidate recovery mtime " << pathString(candidate.recoveryPath)
                   << ": " << mtimeError.message());
        return false;
    }
    if (currentMtime != candidate.modified) {
        RCBN_ERROR("Autosave: recovery changed during candidate interaction " << pathString(candidate.recoveryPath));
        return false;
    }
    return current.lockPath == candidate.lockPath &&
           current.recoveryPath == candidate.recoveryPath &&
           current.logicalScenePath == candidate.logicalScenePath &&
           current.untitled == candidate.untitled;
}

bool AutosaveManager::discardRecovery(const RecoveryCandidate& candidate) const {
    std::error_code ec;
    std::filesystem::remove(candidate.recoveryPath, ec);
    if (ec) RCBN_ERROR("Autosave: failed to discard recovery " << pathString(candidate.recoveryPath) << ": " << ec.message());
    std::filesystem::remove(candidate.lockPath, ec);
    if (ec) RCBN_ERROR("Autosave: failed to discard lock " << pathString(candidate.lockPath) << ": " << ec.message());
    cleanupTransientFiles(candidate.directory);
    std::error_code checkError;
    const bool removed = !std::filesystem::exists(candidate.recoveryPath, checkError) &&
                         !std::filesystem::exists(candidate.lockPath, checkError);
    if (checkError) RCBN_ERROR("Autosave: discard verification failed for " << pathString(candidate.directory) << ": " << checkError.message());
    return removed && !checkError;
}

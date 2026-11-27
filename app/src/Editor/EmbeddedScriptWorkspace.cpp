#include <Editor/EmbeddedScriptWorkspace.hpp>

#include <Editor/CodeEditorPanel.hpp>
#include <Instances/Instance.hpp>
#include <Instances/Script.hpp>
#include <Util/AssetPath.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <unordered_set>

namespace {

std::string filesystemError(const std::string& operation,
                            const std::filesystem::path& path,
                            const std::error_code& error) {
    return operation + " '" + AssetPath::toStored(path) + "': " + error.message();
}

void collectEmbeddedScripts(Instance* instance,
                            std::vector<std::shared_ptr<Script>>& scripts) {
    if (!instance) return;
    if (instance->IsA("Script")) {
        auto script = std::static_pointer_cast<Script>(instance->shared_from_this());
        if (script->IsEmbedded) scripts.push_back(std::move(script));
    }
    for (const auto& [name, child] : instance->children) {
        (void)name;
        if (child) collectEmbeddedScripts(child.get(), scripts);
    }
}

}

EmbeddedScriptWorkspace::EmbeddedScriptWorkspace(std::filesystem::path storageRoot)
    : m_storageRoot(std::move(storageRoot)) {}

bool EmbeddedScriptWorkspace::ensureWorkingFile(
    const std::shared_ptr<Script>& script, std::string& error) {
    if (!script) {
        error = "Cannot create an embedded script working file for a null Script";
        return false;
    }
    if (!script->IsEmbedded) {
        error = "Cannot create an embedded script working file for external Script '" +
            script->getFullPath() + "'";
        return false;
    }

    const auto existing = m_entries.find(script.get());
    if (existing != m_entries.end()) {
        if (existing->second.script.lock() != script) {
            m_entries.erase(existing);
        } else {
            std::error_code ec;
            if (!std::filesystem::is_regular_file(existing->second.path, ec) || ec) {
                error = ec ? filesystemError("Unable to inspect embedded script working file",
                                             existing->second.path, ec)
                           : "Embedded script working file is missing: " +
                                 AssetPath::toStored(existing->second.path);
                return false;
            }
            script->Path = AssetPath::toStored(existing->second.path);
            return true;
        }
    }

    std::filesystem::path path;
    if (!createWorkingFile(script, path, error)) return false;
    m_entries.emplace(script.get(), Entry{script, path, script->Source, false, false});
    m_createdFiles.push_back(path);
    script->Path = AssetPath::toStored(path);
    return true;
}

bool EmbeddedScriptWorkspace::saveEditorBuffer(
    const std::shared_ptr<Script>& script, const std::string& source,
    std::string& error) {
    if (!ensureWorkingFile(script, error)) return false;

    auto entry = m_entries.find(script.get());
    if (entry == m_entries.end()) {
        error = "Embedded Script tracking state was lost for '" +
            script->getFullPath() + "'";
        return false;
    }

    std::string diskContents;
    if (!readText(entry->second.path, diskContents, error)) return false;
    const bool externalChanged = diskContents != entry->second.lastSyncedContents;
    if (externalChanged && diskContents != source) {
        error = "External edit conflict for Script '" + script->getFullPath() +
            "'. Keep both the Code Editor buffer and working file; resolve the conflict "
            "before saving.";
        return false;
    }

    if (diskContents != source) {
        std::string latestDiskContents;
        if (!readText(entry->second.path, latestDiskContents, error)) return false;
        if (latestDiskContents != diskContents) {
            error = "External edit conflict for Script '" + script->getFullPath() +
                "' while saving '" + AssetPath::toStored(entry->second.path) + "'";
            return false;
        }
        if (!writeText(entry->second.path, source, error)) return false;
    }

    if (script->Source != source) {
        script->Source = source;
        entry->second.hasUnsavedChanges = true;
    }
    entry->second.lastSyncedContents = source;
    return true;
}

bool EmbeddedScriptWorkspace::createWorkingFile(
    const std::shared_ptr<Script>& script, std::filesystem::path& path,
    std::string& error) {
    std::error_code ec;
    const auto directory = m_storageRoot / "temp" / "embedded-scripts";
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        error = filesystemError("Unable to create embedded script directory", directory, ec);
        return false;
    }

    std::mt19937_64 randomEngine;
    try {
        std::random_device randomDevice;
        randomEngine.seed(
            (static_cast<std::uint64_t>(randomDevice()) << 32) ^
            static_cast<std::uint64_t>(randomDevice()) ^
            static_cast<std::uint64_t>(
                std::chrono::steady_clock::now().time_since_epoch().count()));
    } catch (const std::exception& exception) {
        error = "Unable to generate a unique embedded script filename: ";
        error += exception.what();
        return false;
    }
    for (int attempt = 0; attempt < 32; ++attempt) {
        std::ostringstream uniqueId;
        uniqueId << std::hex << std::setfill('0')
                 << std::setw(16) << randomEngine()
                 << std::setw(16) << randomEngine();
        const auto candidate = directory /
            ("__Recubin_" + uniqueId.str() + scriptExtensionSuffix(script->ScriptExt));
        std::ofstream output(candidate, std::ios::binary | std::ios::out | std::ios::noreplace);
        if (!output.is_open()) {
            ec.clear();
            if (std::filesystem::exists(candidate, ec) && !ec) continue;
            error = "Unable to create embedded script working file '" +
                AssetPath::toStored(candidate) + "'";
            return false;
        }
        output.write(script->Source.data(),
                     static_cast<std::streamsize>(script->Source.size()));
        output.flush();
        if (!output.good()) {
            output.close();
            std::error_code removeError;
            std::filesystem::remove(candidate, removeError);
            error = "Unable to write embedded script working file '" +
                AssetPath::toStored(candidate) + "'";
            if (removeError) error += ": cleanup failed: " + removeError.message();
            return false;
        }
        output.close();
        if (output.fail()) {
            std::error_code removeError;
            std::filesystem::remove(candidate, removeError);
            error = "Unable to close embedded script working file '" +
                AssetPath::toStored(candidate) + "'";
            if (removeError) error += ": cleanup failed: " + removeError.message();
            return false;
        }
        path = candidate;
        return true;
    }

    error = "Unable to allocate a unique embedded script working file in '" +
        AssetPath::toStored(directory) + "'";
    return false;
}

bool EmbeddedScriptWorkspace::readText(const std::filesystem::path& path,
                                       std::string& text, std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        error = "Unable to read embedded script working file '" +
            AssetPath::toStored(path) + "'";
        return false;
    }
    std::ostringstream contents;
    contents << input.rdbuf();
    if (input.bad()) {
        error = "Failed while reading embedded script working file '" +
            AssetPath::toStored(path) + "'";
        return false;
    }
    text = contents.str();
    return true;
}

bool EmbeddedScriptWorkspace::writeText(const std::filesystem::path& path,
                                        const std::string& text, std::string& error) {
    std::ofstream output(path, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!output.is_open()) {
        error = "Unable to write embedded script working file '" +
            AssetPath::toStored(path) + "'";
        return false;
    }
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.flush();
    if (!output.good()) {
        error = "Failed while writing embedded script working file '" +
            AssetPath::toStored(path) + "'";
        return false;
    }
    output.close();
    if (output.fail()) {
        error = "Failed while closing embedded script working file '" +
            AssetPath::toStored(path) + "'";
        return false;
    }
    return true;
}

bool EmbeddedScriptWorkspace::synchronize(
    Instance* root, const std::function<CodeEditorPanel*(Script*)>& findEditor,
    bool& changed, std::string& error) {
    changed = false;
    if (!root) {
        error = "Cannot synchronize embedded scripts without a Scene root";
        return false;
    }

    std::vector<std::shared_ptr<Script>> scripts;
    collectEmbeddedScripts(root, scripts);
    for (const auto& script : scripts) {
        if (!ensureWorkingFile(script, error)) return false;
        auto entry = m_entries.find(script.get());
        if (entry == m_entries.end()) {
            error = "Embedded Script tracking state was lost for '" +
                script->getFullPath() + "'";
            return false;
        }

        std::string diskContents;
        if (!readText(entry->second.path, diskContents, error)) return false;
        const bool externalChanged = diskContents != entry->second.lastSyncedContents;
        CodeEditorPanel* editor = findEditor ? findEditor(script.get()) : nullptr;
        const std::string previousSource = script->Source;
        if (externalChanged) {
            const std::string internalContents =
                editor && editor->isDirty() ? editor->editorText() : script->Source;
            if (internalContents != entry->second.lastSyncedContents &&
                internalContents != diskContents) {
                error = "External edit conflict for Script '" + script->getFullPath() +
                    "'. Keep both the Code Editor buffer and working file; resolve the conflict "
                    "before saving.";
                return false;
            }
            script->Source = diskContents;
            if (editor) editor->reloadEmbeddedSource();
            entry->second.lastSyncedContents = diskContents;
            if (script->Source != previousSource)
                entry->second.hasUnsavedChanges = true;
        } else if (editor && editor->isDirty()) {
            if (!editor->save()) {
                error = "Unable to save the Code Editor buffer for Script '" +
                    script->getFullPath() + "'";
                return false;
            }
            if (!readText(entry->second.path, diskContents, error)) return false;
        }

        if (script->Source != diskContents) {
            changed = true;
            std::string latestDiskContents;
            if (!readText(entry->second.path, latestDiskContents, error)) return false;
            if (latestDiskContents != entry->second.lastSyncedContents) {
                error = "External edit conflict for Script '" + script->getFullPath() +
                    "' while synchronizing '" + AssetPath::toStored(entry->second.path) + "'";
                return false;
            }
            if (!writeText(entry->second.path, script->Source, error)) return false;
            entry->second.lastSyncedContents = script->Source;
            entry->second.hasUnsavedChanges = true;
        }
        if (script->Source != previousSource) {
            changed = true;
            entry->second.hasUnsavedChanges = true;
        }
    }

    return true;
}

bool EmbeddedScriptWorkspace::hasExternalChanges(
    Instance* root, bool& changed, std::string& error) const {
    changed = false;
    if (!root) {
        error = "Cannot inspect embedded scripts without a Scene root";
        return false;
    }

    std::vector<std::shared_ptr<Script>> scripts;
    collectEmbeddedScripts(root, scripts);
    for (const auto& script : scripts) {
        const auto entry = m_entries.find(script.get());
        if (entry == m_entries.end() || entry->second.script.lock() != script) continue;

        std::string diskContents;
        if (!readText(entry->second.path, diskContents, error)) return false;
        if (diskContents != entry->second.lastSyncedContents) {
            changed = true;
            return true;
        }
    }
    return true;
}

void EmbeddedScriptWorkspace::markSceneDirty() {
    m_sceneSavedSinceLastChange = false;
}

void EmbeddedScriptWorkspace::markSceneSaved(Instance* root) {
    m_sceneSavedSinceLastChange = true;
    std::vector<std::shared_ptr<Script>> scripts;
    collectEmbeddedScripts(root, scripts);
    std::unordered_set<Script*> savedScripts;
    for (const auto& script : scripts) {
        savedScripts.insert(script.get());
        const auto entry = m_entries.find(script.get());
        if (entry == m_entries.end() || entry->second.script.lock() != script) continue;
        entry->second.savedInScene = true;
        entry->second.hasUnsavedChanges = false;
    }
    for (auto& [script, entry] : m_entries) {
        const auto trackedScript = entry.script.lock();
        if (trackedScript && savedScripts.contains(trackedScript.get())) continue;
        entry.savedInScene = false;
    }
}

bool EmbeddedScriptWorkspace::cleanupAfterNormalExit(std::string& error) {
    if (!m_sceneSavedSinceLastChange) return true;
    std::vector<std::filesystem::path> retainedFiles;
    for (const auto& path : m_createdFiles) {
        const auto entry = std::find_if(
            m_entries.begin(), m_entries.end(),
            [&path](const auto& candidate) { return candidate.second.path == path; });
        if (entry == m_entries.end() || !entry->second.savedInScene ||
            entry->second.hasUnsavedChanges) {
            retainedFiles.push_back(path);
            continue;
        }
        std::error_code ec;
        std::filesystem::remove(path, ec);
        if (ec) {
            error = filesystemError("Unable to remove embedded script working file", path, ec);
            return false;
        }
    }
    m_createdFiles = std::move(retainedFiles);
    for (auto entry = m_entries.begin(); entry != m_entries.end();) {
        const bool stillTracked = std::find(m_createdFiles.begin(), m_createdFiles.end(),
                                            entry->second.path) != m_createdFiles.end();
        if (!stillTracked) entry = m_entries.erase(entry);
        else ++entry;
    }
    return true;
}

bool EmbeddedScriptWorkspace::hasTrackedFiles() const {
    return !m_createdFiles.empty();
}

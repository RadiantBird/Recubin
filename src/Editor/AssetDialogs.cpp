#include <Editor/AssetDialogs.hpp>
#include <Editor/CommandHistory.hpp>
#include <Editor/Localization.hpp>
#include <Editor/SceneHierarchyPanel.hpp>
#include <Instances/Instance.hpp>
#include <Util/IPlatform.hpp>
#include <Util/Logger.hpp>
#include <Util/Platform.hpp>
#include <include/imgui/imgui.h>
#include <algorithm>
#include <cstdio>
#include <unordered_set>

namespace {
const std::vector<FileFilter>& assetFilters() {
    static const std::vector<FileFilter> filters = {
        {"Recubin Asset (*.rcaet)", "*.rcaet"}};
    return filters;
}

std::string formatBytes(std::uint64_t bytes) {
    char buffer[32];
    if (bytes >= 1024ull * 1024ull)
        std::snprintf(buffer, sizeof(buffer), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    else if (bytes >= 1024ull)
        std::snprintf(buffer, sizeof(buffer), "%.1f KB", static_cast<double>(bytes) / 1024.0);
    else
        std::snprintf(buffer, sizeof(buffer), "%llu B", static_cast<unsigned long long>(bytes));
    return buffer;
}

bool canEmbed(const AssetDependencies::Dependency& dependency) {
    return dependency.exists && !dependency.autosave;
}

const char* statusLabel(const AssetDependencies::Dependency& dependency) {
    if (!dependency.exists) return Loc::t(Loc::LocKey::AssetStatusMissing);
    if (dependency.autosave) return Loc::t(Loc::LocKey::AssetStatusAutosave);
    if (dependency.executable) return Loc::t(Loc::LocKey::AssetStatusExecutable);
    if (dependency.absolute) return Loc::t(Loc::LocKey::AssetStatusAbsolute);
    return Loc::t(Loc::LocKey::AssetStatusOk);
}

bool isWarningStatus(const AssetDependencies::Dependency& dependency) {
    return !dependency.exists || dependency.autosave || dependency.executable;
}

Instance* systemOf(Instance& instance) {
    return instance.IsA("System") ? &instance : instance.findFirstAncestorSystem();
}

constexpr ImVec4 WARNING_COLOR = ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
} // namespace

bool AssetDialogs::canExport(const std::vector<Instance*>& roots) {
    return std::any_of(roots.begin(), roots.end(), [](Instance* root) {
        return root && AssetExporter::isExportable(*root);
    });
}

void AssetDialogs::requestExport(const std::vector<Instance*>& roots) {
    m_plan = AssetExporter::prepare(roots);
    if (!m_plan) {
        showMessage(Loc::t(Loc::LocKey::AssetExportTitle),
                    {Loc::t(Loc::LocKey::AssetNothingToExport)});
        return;
    }
    std::snprintf(m_name, sizeof(m_name), "%s", m_plan.defaultName.c_str());
    m_mode = 0;
    m_embed.assign(m_plan.dependencies.size(), 0);
    for (std::size_t i = 0; i < m_plan.dependencies.size(); ++i) {
        const auto& dependency = m_plan.dependencies[i];
        m_embed[i] = canEmbed(dependency) && !dependency.executable;
    }
    m_exportError.clear();
    m_openExport = true;
}

void AssetDialogs::requestImport(const std::shared_ptr<Instance>& parent, const std::string& path) {
    if (!parent) return;
    m_importParent = parent;
    if (path.empty()) m_doPickImport = true;
    else              m_pendingImportPath = path;
}

void AssetDialogs::showMessage(const std::string& title, std::vector<std::string> lines) {
    m_messageTitle = title;
    m_messageLines = std::move(lines);
    m_openMessage = true;
}

void AssetDialogs::render() {
    // OSのファイルダイアログはポップアップの外で開く。
    if (m_doPickImport) {
        m_doPickImport = false;
        const std::string path = getPlatform().openFileDialog(assetFilters());
        if (!path.empty()) m_pendingImportPath = path;
    }
    if (!m_pendingImportPath.empty()) {
        const std::string path = std::move(m_pendingImportPath);
        m_pendingImportPath.clear();
        beginImport(path);
    }

    renderExportDialog();
    renderExecutableConfirm();
    renderMessage();

    if (m_doSaveExport) {
        m_doSaveExport = false;
        const std::string path = getPlatform().saveFileDialog(assetFilters(), "rcaet");
        if (!path.empty()) runExport(path);
    }
}

void AssetDialogs::renderExportDialog() {
    if (m_openExport) {
        ImGui::OpenPopup("###AssetExport");
        m_openExport = false;
    }
    const std::string title = std::string(Loc::t(Loc::LocKey::AssetExportTitle)) + "###AssetExport";
    ImGui::SetNextWindowSize(ImVec2(620, 520), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_NoResize)) return;

    if (m_closeExport) {
        m_closeExport = false;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    ImGui::TextUnformatted(Loc::t(Loc::LocKey::AssetNameLabel));
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##assetname", m_name, sizeof(m_name));

    ImGui::Spacing();
    ImGui::RadioButton(Loc::t(Loc::LocKey::AssetModeStructure), &m_mode, 0);
    ImGui::RadioButton(Loc::t(Loc::LocKey::AssetModeEmbed), &m_mode, 1);

    ImGui::Separator();
    ImGui::TextUnformatted(Loc::t(Loc::LocKey::AssetDependenciesHeader));
    const auto& dependencies = m_plan.dependencies;
    if (dependencies.empty()) {
        ImGui::TextDisabled("%s", Loc::t(Loc::LocKey::AssetNoDependencies));
    } else {
        const bool embedding = m_mode == 1;
        ImGui::BeginDisabled(!embedding);
        if (ImGui::SmallButton(Loc::t(Loc::LocKey::AssetSelectAll))) {
            for (std::size_t i = 0; i < dependencies.size(); ++i)
                m_embed[i] = canEmbed(dependencies[i]) && !dependencies[i].executable;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(Loc::t(Loc::LocKey::AssetClearAll)))
            std::fill(m_embed.begin(), m_embed.end(), 0);
        ImGui::EndDisabled();

        if (ImGui::BeginTable("##assetdeps", 4,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_BordersInnerH,
                              ImVec2(0, 190))) {
            ImGui::TableSetupColumn("##embed", ImGuiTableColumnFlags_WidthFixed, 28.0f);
            ImGui::TableSetupColumn("##status", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("##size", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("##path", ImGuiTableColumnFlags_WidthStretch);
            for (std::size_t i = 0; i < dependencies.size(); ++i) {
                const auto& dependency = dependencies[i];
                ImGui::PushID(static_cast<int>(i));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::BeginDisabled(!embedding || !canEmbed(dependency));
                bool checked = m_embed[i] != 0;
                if (ImGui::Checkbox("##embed", &checked)) m_embed[i] = checked;
                ImGui::EndDisabled();
                ImGui::TableNextColumn();
                if (isWarningStatus(dependency))
                    ImGui::TextColored(WARNING_COLOR, "%s", statusLabel(dependency));
                else
                    ImGui::TextUnformatted(statusLabel(dependency));
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(formatBytes(dependency.size).c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(dependency.source.c_str());
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (embedding) {
            std::uint64_t total = 0;
            bool executableChosen = false;
            for (std::size_t i = 0; i < dependencies.size(); ++i) {
                if (!m_embed[i] || !canEmbed(dependencies[i])) continue;
                total += dependencies[i].size;
                executableChosen = executableChosen || dependencies[i].executable;
            }
            ImGui::Text("%s: %s", Loc::t(Loc::LocKey::AssetTotalSize), formatBytes(total).c_str());
            if (executableChosen)
                ImGui::TextColored(WARNING_COLOR, "%s", Loc::t(Loc::LocKey::AssetExecutableHint));
        }
    }

    if (!m_plan.materialNames.empty()) {
        std::string joined;
        for (const auto& name : m_plan.materialNames) joined += (joined.empty() ? "" : ", ") + name;
        ImGui::Text("%s: %s", Loc::t(Loc::LocKey::AssetMaterialsLabel), joined.c_str());
    }

    if (!m_plan.warnings.empty()) {
        ImGui::Separator();
        ImGui::TextUnformatted(Loc::t(Loc::LocKey::AssetWarningsHeader));
        ImGui::BeginChild("##assetwarnings", ImVec2(0, 90), true);
        for (const auto& warning : m_plan.warnings) ImGui::TextWrapped("%s", warning.c_str());
        ImGui::EndChild();
    }

    if (!m_exportError.empty()) ImGui::TextColored(WARNING_COLOR, "%s", m_exportError.c_str());

    ImGui::Spacing();
    ImGui::BeginDisabled(m_name[0] == '\0');
    if (ImGui::Button(Loc::t(Loc::LocKey::AssetExportButton), ImVec2(120, 0))) m_doSaveExport = true;
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(Loc::t(Loc::LocKey::Cancel), ImVec2(100, 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void AssetDialogs::runExport(const std::string& path) {
    AssetExporter::Options options;
    options.name = m_name;
    if (m_mode == 1) {
        for (std::size_t i = 0; i < m_plan.dependencies.size(); ++i) {
            if (m_embed[i]) options.embedSources.push_back(m_plan.dependencies[i].source);
        }
    }
    const AssetContainer::Result result = AssetExporter::write(m_plan, options, path);
    if (!result) {
        m_exportError = result.message;
        RCBN_ERROR("Asset export failed: " << result.message);
        return;
    }
    RCBN_LOG("Asset exported: " << path);
    m_exportError.clear();
    m_closeExport = true;
}

void AssetDialogs::beginImport(const std::string& path) {
    auto loaded = std::make_unique<AssetImporter::Loaded>();
    const AssetImporter::Result result = AssetImporter::load(path, *loaded);
    if (!result) {
        RCBN_ERROR("Asset import failed: " << path << ": " << result.message);
        showMessage(Loc::t(Loc::LocKey::AssetImportErrorTitle), {path, result.message});
        return;
    }
    m_loaded = std::move(loaded);
    if (m_loaded->executables.empty()) {
        executeImport(false);
        return;
    }
    m_openExecutableConfirm = true;
}

void AssetDialogs::renderExecutableConfirm() {
    if (m_openExecutableConfirm) {
        ImGui::OpenPopup("###AssetExecutables");
        m_openExecutableConfirm = false;
    }
    const std::string title =
        std::string(Loc::t(Loc::LocKey::AssetExecutableTitle)) + "###AssetExecutables";
    ImGui::SetNextWindowSize(ImVec2(620, 0), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    if (!m_loaded) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    ImGui::TextColored(WARNING_COLOR, "%s", Loc::t(Loc::LocKey::AssetExecutableMessage));
    ImGui::Spacing();
    ImGui::BeginChild("##assetexecutables", ImVec2(580, 120), true);
    for (const auto& executable : m_loaded->executables) {
        ImGui::TextWrapped("%s  (%s)", executable.file.c_str(), formatBytes(executable.size).c_str());
        ImGui::TextDisabled("%s", executable.hash.c_str());
    }
    ImGui::EndChild();
    ImGui::Spacing();

    // 既定（先頭）は安全側の「除外して読み込む」。
    if (ImGui::Button(Loc::t(Loc::LocKey::AssetExecutableExclude))) {
        ImGui::CloseCurrentPopup();
        executeImport(false);
    }
    ImGui::SameLine();
    if (ImGui::Button(Loc::t(Loc::LocKey::AssetExecutableExtract))) {
        ImGui::CloseCurrentPopup();
        executeImport(true);
    }
    ImGui::SameLine();
    if (ImGui::Button(Loc::t(Loc::LocKey::Cancel))) {
        m_loaded.reset();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void AssetDialogs::executeImport(bool allowExecutables) {
    const std::shared_ptr<Instance> parent = m_importParent.lock();
    // 読み込み済みの内容は1回の取り込みで使い切る（ダイアログ側に残さない）。
    const std::unique_ptr<AssetImporter::Loaded> loadedOwner = std::move(m_loaded);
    if (!loadedOwner) return;
    const AssetImporter::Loaded& loaded = *loadedOwner;
    if (!parent || !history) return;

    Instance* system = systemOf(*parent);
    Instance* materialService = system ? system->getChild("MaterialService") : nullptr;

    AssetImporter::Options options;
    options.allowExecutables = allowExecutables;
    AssetImporter::Imported imported;
    const AssetImporter::Result result =
        AssetImporter::instantiate(loaded, options, materialService, imported);
    if (!result) {
        RCBN_ERROR("Asset import failed: " << result.message);
        showMessage(Loc::t(Loc::LocKey::AssetImportErrorTitle), {result.message});
        return;
    }

    // Materialを先に追加し、ルートは親内で名前を一意化して1つのUndo単位にする。
    auto group = std::make_unique<CompositeCommand>();
    if (materialService) {
        const auto serviceShared = materialService->shared_from_this();
        for (const auto& material : imported.materialsToAdd)
            group->add(std::make_unique<AddInstanceCommand>(serviceShared, material));
    }
    std::unordered_set<std::string> taken;
    for (const auto& root : imported.roots) {
        root->Name = SceneHierarchyPanel::uniqueName(parent, root->Name, &taken);
        taken.insert(root->Name);
        group->add(std::make_unique<AddInstanceCommand>(parent, root));
    }
    history->execute(std::move(group));

    if (selectedInstance && selectedInstances) {
        selectedInstances->clear();
        for (const auto& root : imported.roots) selectedInstances->push_back(root.get());
        *selectedInstance = imported.roots.empty() ? nullptr : imported.roots.front().get();
    }

    for (const auto& warning : imported.warnings) RCBN_WARN("Asset import: " << warning);
    RCBN_LOG("Asset imported: " << loaded.name);
    if (!imported.warnings.empty())
        showMessage(Loc::t(Loc::LocKey::AssetImportDoneTitle), imported.warnings);
}

void AssetDialogs::renderMessage() {
    if (m_openMessage) {
        ImGui::OpenPopup("###AssetMessage");
        m_openMessage = false;
    }
    const std::string title = m_messageTitle + "###AssetMessage";
    if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    for (const auto& line : m_messageLines) ImGui::TextWrapped("%s", line.c_str());
    ImGui::Spacing();
    if (ImGui::Button(Loc::t(Loc::LocKey::OK), ImVec2(100, 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

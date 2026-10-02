#include <Editor/ProfilerPanel.hpp>

#include <Editor/Localization.hpp>
#include <Util/FrameProfiler.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <vector>

namespace {
void drawSection(
    const char* sectionName,
    Loc::LocKey labelKey,
    const ImVec4& color
) {
    FrameProfiler::SectionSnapshot snapshot;
    const bool available =
        FrameProfiler::get().getSectionSnapshot(sectionName, snapshot) &&
        snapshot.count != 0;

    ImGui::TextUnformatted(Loc::t(labelKey));
    if (!available) {
        ImGui::TextDisabled("%s", Loc::t(Loc::LocKey::ProfilerNoSamples));
        ImGui::Dummy(ImVec2(0.0f, 94.0f));
        return;
    }

    char overlay[192];
    std::snprintf(
        overlay,
        sizeof(overlay),
        "%s %.2f ms   %s %.2f ms   %s %.2f ms",
        Loc::t(Loc::LocKey::ProfilerCurrent), snapshot.latestMs,
        Loc::t(Loc::LocKey::ProfilerAverage), snapshot.averageMs,
        Loc::t(Loc::LocKey::ProfilerPeak), snapshot.peakMs
    );
    const float scaleMax = std::max(1.0f, snapshot.peakMs * 1.1f);
    const std::string graphId = std::string("##ProfilerGraph_") + sectionName;
    ImGui::PushStyleColor(ImGuiCol_PlotLines, color);
    ImGui::PushStyleColor(
        ImGuiCol_PlotLinesHovered,
        ImVec4(
            std::min(color.x + 0.2f, 1.0f),
            std::min(color.y + 0.2f, 1.0f),
            std::min(color.z + 0.2f, 1.0f),
            color.w
        )
    );
    ImGui::PlotLines(
        graphId.c_str(),
        snapshot.samples.data(),
        static_cast<int>(snapshot.count),
        0,
        overlay,
        0.0f,
        scaleMax,
        ImVec2(-1.0f, 100.0f)
    );
    ImGui::PopStyleColor(2);
}

// ローカライズ済みキーまたはリテラル文字列のどちらでも表せるラベル。
struct Label {
    const char* text = nullptr;
    Loc::LocKey key{};
    bool localized = false;

    Label() = default;
    Label(const char* literal) : text(literal) {}
    Label(Loc::LocKey locKey) : key(locKey), localized(true) {}

    bool empty() const { return !localized && text == nullptr; }
    const char* resolve() const { return localized ? Loc::t(key) : text; }
};

enum class EntryKind { Timing, GpuTiming, Counter };

struct Entry {
    EntryKind kind;
    const char* name;
    Label label;
};

struct TableDef {
    const char* id;
    Label title;
    Label hint;
    std::vector<Entry> entries;
};

Entry timing(const char* name, Loc::LocKey key) {
    return {EntryKind::Timing, name, Label(key)};
}
Entry rawTiming(const char* name) {
    return {EntryKind::Timing, name, Label(name)};
}
Entry gpuTiming(const char* name, Loc::LocKey key) {
    return {EntryKind::GpuTiming, name, Label(key)};
}
Entry counter(const char* name, Loc::LocKey key) {
    return {EntryKind::Counter, name, Label(key)};
}
Entry rawCounter(const char* name) {
    return {EntryKind::Counter, name, Label(name)};
}

using Cells = std::array<std::string, 3>;

std::string formatMs(float value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.2f ms", value);
    return buffer;
}

template <typename Snapshot>
Cells timingCells(const Snapshot& snapshot) {
    return {
        formatMs(snapshot.latestMs),
        formatMs(snapshot.averageMs),
        formatMs(snapshot.peakMs)
    };
}

Cells counterCells(const FrameProfiler::CounterSnapshot& snapshot) {
    char latest[32];
    char average[32];
    char peak[32];
    std::snprintf(latest, sizeof(latest), "%lld", snapshot.latest);
    std::snprintf(average, sizeof(average), "%.1f", snapshot.average);
    std::snprintf(peak, sizeof(peak), "%lld", snapshot.peak);
    return {latest, average, peak};
}

// サンプルが無い項目は全セル "--" を返す。
Cells entryCells(const Entry& entry) {
    const Cells none{"--", "--", "--"};
    switch (entry.kind) {
    case EntryKind::Timing: {
        FrameProfiler::SectionSnapshot snapshot;
        if (!FrameProfiler::get().getSectionSnapshot(entry.name, snapshot) ||
            snapshot.count == 0) return none;
        return timingCells(snapshot);
    }
    case EntryKind::GpuTiming: {
        FrameProfiler::GpuSnapshot snapshot;
        if (!FrameProfiler::get().getGpuSnapshot(entry.name, snapshot) ||
            snapshot.count == 0) return none;
        return timingCells(snapshot);
    }
    case EntryKind::Counter: {
        FrameProfiler::CounterSnapshot snapshot;
        if (!FrameProfiler::get().getCounterSnapshot(entry.name, snapshot) ||
            snapshot.count == 0) return none;
        return counterCells(snapshot);
    }
    }
    return none;
}

bool beginMetricTable(const char* id) {
    constexpr ImGuiTableFlags flags =
        ImGuiTableFlags_BordersInnerH |
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable(id, 4, flags)) return false;
    ImGui::TableSetupColumn(
        Loc::t(Loc::LocKey::ProfilerSection),
        ImGuiTableColumnFlags_WidthStretch, 1.8f
    );
    ImGui::TableSetupColumn(Loc::t(Loc::LocKey::ProfilerCurrent));
    ImGui::TableSetupColumn(Loc::t(Loc::LocKey::ProfilerAverage));
    ImGui::TableSetupColumn(Loc::t(Loc::LocKey::ProfilerPeak));
    ImGui::TableHeadersRow();
    return true;
}

void drawEntryRow(const Entry& entry) {
    const Cells cells = entryCells(entry);
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(entry.label.resolve());
    for (int column = 0; column < 3; ++column) {
        ImGui::TableSetColumnIndex(column + 1);
        if (cells[column] == "--") ImGui::TextDisabled("--");
        else ImGui::TextUnformatted(cells[column].c_str());
    }
}

void drawTableBody(const TableDef& table) {
    if (!table.hint.empty()) {
        ImGui::TextWrapped("%s", table.hint.resolve());
    }
    if (!beginMetricTable(table.id)) return;
    for (const Entry& entry : table.entries) drawEntryRow(entry);
    ImGui::EndTable();
}

void appendMarkdownTable(std::string& out, const TableDef& table) {
    out += "## ";
    out += table.title.resolve();
    out += "\n\n| ";
    out += Loc::t(Loc::LocKey::ProfilerSection);
    out += " | ";
    out += Loc::t(Loc::LocKey::ProfilerCurrent);
    out += " | ";
    out += Loc::t(Loc::LocKey::ProfilerAverage);
    out += " | ";
    out += Loc::t(Loc::LocKey::ProfilerPeak);
    out += " |\n|---|---:|---:|---:|\n";
    for (const Entry& entry : table.entries) {
        out += "| ";
        out += entry.label.resolve();
        for (const std::string& cell : entryCells(entry)) {
            out += " | ";
            out += cell;
        }
        out += " |\n";
    }
    out += "\n";
}

// グラフ表示される3区間。UIではグラフ、Markdownでは表として出力する。
const TableDef& frameSectionsTable() {
    static const TableDef table{
        "##ProfilerFrameSectionsTable", "Frame Sections", Label(),
        {
            timing("render", Loc::LocKey::ProfilerRendering),
            timing("physics", Loc::LocKey::ProfilerPhysics),
            timing("luau", Loc::LocKey::ProfilerScripts),
        }
    };
    return table;
}

const TableDef& gpuTimingTable() {
    static const TableDef table{
        "##ProfilerGpuTimingTable",
        Label(Loc::LocKey::ProfilerGpuTiming),
        Label(Loc::LocKey::ProfilerGpuTimingHint),
        {
            gpuTiming("gpuTotal", Loc::LocKey::ProfilerGpuTotal),
            gpuTiming("gpuShadow", Loc::LocKey::ProfilerShadow),
            gpuTiming("gpuMain", Loc::LocKey::ProfilerMainGeometry),
            gpuTiming("gpuSurfaceMarks", Loc::LocKey::ProfilerSurfaceMarks),
            gpuTiming("gpuExtras", Loc::LocKey::ProfilerExtras),
        }
    };
    return table;
}

// GPUタイミング以外の折りたたみ表。UI表示とMarkdownコピーで同じ定義を共有する。
const std::vector<TableDef>& breakdownTables() {
    static const std::vector<TableDef> tables{
        {
            "##ProfilerRenderingBreakdownTable",
            Label(Loc::LocKey::ProfilerRenderingBreakdown),
            Label(Loc::LocKey::ProfilerNestedHint),
            {
                timing("shadow", Loc::LocKey::ProfilerShadow),
                timing("main", Loc::LocKey::ProfilerMainGeometry),
                rawTiming("render.instanceCollect"),
                rawTiming("render.shadowCull"),
                rawTiming("render.shadowUpload"),
                rawTiming("render.shadowDraw"),
                rawTiming("render.mainInstanceUpload"),
                rawTiming("render.mainInstanceDraw"),
                timing("surfaceMarks", Loc::LocKey::ProfilerSurfaceMarks),
                timing("extras", Loc::LocKey::ProfilerExtras),
                timing("highlights", Loc::LocKey::ProfilerHighlights),
                timing("constraints", Loc::LocKey::ProfilerConstraints),
                timing("renderDebug", Loc::LocKey::ProfilerRenderDebug),
                timing("terrain", Loc::LocKey::ProfilerTerrain),
                timing("weather", Loc::LocKey::ProfilerWeather),
                timing("particles", Loc::LocKey::ProfilerParticles),
                timing("selectionOutline", Loc::LocKey::ProfilerSelectionOutline),
                timing("postEffects", Loc::LocKey::ProfilerPostEffects),
                timing("surfaceGuiBakes", Loc::LocKey::ProfilerSurfaceGuiBakeTime),
                timing("ui", Loc::LocKey::ProfilerEditorUi),
                timing("swap", Loc::LocKey::ProfilerSwap),
            }
        },
        {
            "##ProfilerMainLoopTable",
            Label("Main Loop"),
            Label("CPU time of per-frame updates outside render/physics/luau."),
            {
                rawTiming("main.processInput"),
                rawTiming("main.humanoids"),
                rawTiming("main.terrains"),
                rawTiming("main.weather"),
                rawTiming("main.particles"),
            }
        },
        {
            "##ProfilerEditorUiBreakdownTable",
            Label("Editor UI Breakdown"),
            Label("Per-panel CPU time inside EditorManager::render()."),
            {
                rawTiming("ui.newFrame"),
                rawTiming("ui.renderPanels"),
                rawTiming("ui.toolbar"),
                rawTiming("ui.imguiRenderDrawData"),
                rawTiming("ui.sceneHierarchy"),
                rawTiming("ui.properties"),
                rawTiming("ui.viewportPanelTotal"),
                rawTiming("ui.viewportScene"),
                rawTiming("ui.viewportClick"),
                rawTiming("ui.viewportGizmo"),
                rawTiming("ui.viewportHoverPick"),
                rawTiming("ui.viewportHoverOutline"),
                rawTiming("ui.viewportFreeDrag"),
                rawTiming("ui.contentBrowser"),
                rawTiming("ui.console"),
                rawTiming("ui.animationEditor"),
                rawTiming("ui.welcome"),
                rawTiming("ui.profiler"),
            }
        },
        {
            "##ProfilerPhysicsBreakdownTable",
            Label("Physics Breakdown"),
            Label("Box3D internal step and Recubin physics synchronization time."),
            {
                rawTiming("physics.reconcileConstraints"),
                rawTiming("physics.staleScan"),
                rawTiming("physics.syncPivots"),
                rawTiming("physics.buoyancy"),
                rawTiming("physics.forces"),
                rawTiming("physics.gyro"),
                rawTiming("physics.box3dStep"),
                rawTiming("physics.maintainedVelocity"),
                rawTiming("physics.contactEvents"),
                rawTiming("physics.syncCubes"),
                rawCounter("physicsBodies"),
                rawCounter("physicsContacts"),
                rawCounter("physicsAwakeBodies"),
                rawCounter("physicsAwakeBeforeStep"),
                rawCounter("physicsAwakeAfterStep"),
                rawCounter("physicsAwakeAfterMaintain"),
            }
        },
        {
            "##ProfilerDrawCallsTable",
            Label("Draw Calls"),
            Label(
                "Main/Shadow = glDraw* calls per pass (an individually drawn "
                "object counts one per draw() call). Instanced = instanced "
                "batches (main + shadow). Terrain = chunk draws, already "
                "included in Main/Shadow. instanceUploadBytes = instance data "
                "sent via glBufferData."
            ),
            {
                rawCounter("drawCallsMain"),
                rawCounter("drawCallsShadow"),
                rawCounter("drawCallsInstanced"),
                rawCounter("drawCallsTerrain"),
                rawCounter("instanceUploadBytes"),
            }
        },
        {
            "##ProfilerDrawCountersTable",
            Label(Loc::LocKey::ProfilerDrawCounters),
            Label(),
            {
                counter("cubesDrawn", Loc::LocKey::ProfilerCubesDrawn),
                counter("cubesCulled", Loc::LocKey::ProfilerCubesCulled),
                counter("instanced", Loc::LocKey::ProfilerInstanced),
                counter("shadowCubes", Loc::LocKey::ProfilerShadowCubes),
                counter("shadowCubesCulled", Loc::LocKey::ProfilerShadowCubesCulled),
                rawCounter("shadowMapReused"),
                counter("surfaceGuiBaked", Loc::LocKey::ProfilerSurfaceGuiBaked),
                counter("surfaceGuiReused", Loc::LocKey::ProfilerSurfaceGuiReused),
                rawCounter("treeLightingNodes"),
                rawCounter("treeInstancesNodes"),
                rawCounter("treeShadowNodes"),
                rawCounter("treeMainNodes"),
                rawCounter("treeSurfaceMarkNodes"),
                rawCounter("treeGuiNodes"),
                rawCounter("baseCubesVisited"),
                rawCounter("surfaceGuiChildrenVisited"),
            }
        },
        {
            "##ProfilerTreeTraversalTable",
            Label("Tree Traversal"),
            Label(),
            {
                rawTiming("treeLighting"),
                rawTiming("treeInstances"),
                rawTiming("treeShadow"),
                rawTiming("treeMain"),
                rawTiming("treeSurfaceMarks"),
                rawTiming("treeGui"),
                rawTiming("treeGuiFonts"),
                rawCounter("treeLightingNodes"),
                rawCounter("treeInstancesNodes"),
                rawCounter("treeShadowNodes"),
                rawCounter("treeMainNodes"),
                rawCounter("treeSurfaceMarkNodes"),
                rawCounter("treeGuiNodes"),
                rawCounter("baseCubesVisited"),
                rawCounter("surfaceGuiChildrenVisited"),
            }
        },
    };
    return tables;
}

std::string buildMarkdownReport() {
    std::string out = "# Profiler\n\n";

    FrameProfiler::FrameSnapshot frame;
    if (FrameProfiler::get().getFrameSnapshot(frame)) {
        char line[192];
        std::snprintf(
            line, sizeof(line),
            "FPS: current %.1f / average %.1f (average frame %.2f ms, %zu frames)\n\n",
            frame.latestFps, frame.averageFps,
            frame.averageFrameMs, frame.count
        );
        out += line;
    } else {
        out += "FPS: no samples\n\n";
    }

    appendMarkdownTable(out, frameSectionsTable());
    if (FrameProfiler::get().isGpuTimingAvailable()) {
        appendMarkdownTable(out, gpuTimingTable());
    } else {
        out += "## ";
        out += gpuTimingTable().title.resolve();
        out += "\n\n";
        out += Loc::t(Loc::LocKey::ProfilerUnavailable);
        out += "\n\n";
    }
    for (const TableDef& table : breakdownTables()) {
        appendMarkdownTable(out, table);
    }
    return out;
}
}

ProfilerPanel::ProfilerPanel() : EditorPanel("###Profiler") {}

void ProfilerPanel::onRender() {
    if (dockspaceId != 0)
        ImGui::SetNextWindowDockID(dockspaceId, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(520.0f, 430.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title.c_str(), &isOpen)) {
        ImGui::End();
        return;
    }

    FrameProfiler::FrameSnapshot frameSnapshot;
    if (FrameProfiler::get().getFrameSnapshot(frameSnapshot)) {
        ImGui::Text(
            "%s — %s: %.1f FPS   %s: %.1f FPS",
            Loc::t(Loc::LocKey::ProfilerFrameRate),
            Loc::t(Loc::LocKey::ProfilerCurrent), frameSnapshot.latestFps,
            Loc::t(Loc::LocKey::ProfilerAverage), frameSnapshot.averageFps
        );
        ImGui::TextDisabled(
            "%s: %.2f ms",
            Loc::t(Loc::LocKey::ProfilerAverageFrameTime),
            frameSnapshot.averageFrameMs
        );
    } else {
        ImGui::Text(
            "%s: %s",
            Loc::t(Loc::LocKey::ProfilerFrameRate),
            Loc::t(Loc::LocKey::ProfilerNoSamples)
        );
    }
    if (ImGui::Button("Copy as Markdown")) {
        ImGui::SetClipboardText(buildMarkdownReport().c_str());
    }
    ImGui::Separator();

    if (ImGui::BeginChild(
            "##ProfilerScrollableBody",
            ImVec2(0.0f, 0.0f),
            false)) {
        const TableDef& gpuTable = gpuTimingTable();
        if (ImGui::CollapsingHeader(
                gpuTable.title.resolve(),
                ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextWrapped("%s", gpuTable.hint.resolve());
            if (!FrameProfiler::get().isGpuTimingAvailable()) {
                ImGui::TextDisabled(
                    "%s", Loc::t(Loc::LocKey::ProfilerUnavailable));
            } else {
                FrameProfiler::GpuSnapshot gpuTotalSnapshot;
                const bool hasGpuSamples =
                    FrameProfiler::get().getGpuSnapshot(
                        "gpuTotal", gpuTotalSnapshot) &&
                    gpuTotalSnapshot.count != 0;
                if (!hasGpuSamples) {
                    ImGui::TextDisabled(
                        "%s", Loc::t(Loc::LocKey::ProfilerNoSamples));
                } else if (beginMetricTable(gpuTable.id)) {
                    for (const Entry& entry : gpuTable.entries) {
                        drawEntryRow(entry);
                    }
                    ImGui::EndTable();
                }
            }
        }

        drawSection(
            "render", Loc::LocKey::ProfilerRendering,
            ImVec4(0.35f, 0.68f, 1.0f, 1.0f)
        );
        drawSection(
            "physics", Loc::LocKey::ProfilerPhysics,
            ImVec4(1.0f, 0.62f, 0.25f, 1.0f)
        );
        drawSection(
            "luau", Loc::LocKey::ProfilerScripts,
            ImVec4(0.38f, 0.86f, 0.50f, 1.0f)
        );

        for (const TableDef& table : breakdownTables()) {
            if (ImGui::CollapsingHeader(
                    table.title.resolve(),
                    ImGuiTreeNodeFlags_DefaultOpen)) {
                drawTableBody(table);
            }
        }
    }
    ImGui::EndChild();

    ImGui::End();
}

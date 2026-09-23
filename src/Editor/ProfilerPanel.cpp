#include <Editor/ProfilerPanel.hpp>

#include <Editor/Localization.hpp>
#include <Util/FrameProfiler.hpp>

#include <algorithm>
#include <cstdio>
#include <string>

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

void drawTimingRow(const char* sectionName, Loc::LocKey labelKey) {
    FrameProfiler::SectionSnapshot snapshot;
    const bool available =
        FrameProfiler::get().getSectionSnapshot(sectionName, snapshot) &&
        snapshot.count != 0;
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(Loc::t(labelKey));
    for (int column = 1; column < 4; ++column) {
        ImGui::TableSetColumnIndex(column);
        if (!available) {
            ImGui::TextDisabled("--");
            continue;
        }
        const float value = column == 1 ? snapshot.latestMs
            : (column == 2 ? snapshot.averageMs : snapshot.peakMs);
        ImGui::Text("%.2f ms", value);
    }
}

void drawGpuTimingRow(const char* metricName, Loc::LocKey labelKey) {
    FrameProfiler::GpuSnapshot snapshot;
    const bool available =
        FrameProfiler::get().getGpuSnapshot(metricName, snapshot) &&
        snapshot.count != 0;
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(Loc::t(labelKey));
    for (int column = 1; column < 4; ++column) {
        ImGui::TableSetColumnIndex(column);
        if (!available) {
            ImGui::TextDisabled("--");
            continue;
        }
        const float value = column == 1 ? snapshot.latestMs
            : (column == 2 ? snapshot.averageMs : snapshot.peakMs);
        ImGui::Text("%.2f ms", value);
    }
}

void drawCounterRow(const char* counterName, Loc::LocKey labelKey) {
    FrameProfiler::CounterSnapshot snapshot;
    const bool available =
        FrameProfiler::get().getCounterSnapshot(counterName, snapshot) &&
        snapshot.count != 0;
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(Loc::t(labelKey));
    ImGui::TableSetColumnIndex(1);
    if (available) ImGui::Text("%lld", snapshot.latest);
    else ImGui::TextDisabled("--");
    ImGui::TableSetColumnIndex(2);
    if (available) ImGui::Text("%.1f", snapshot.average);
    else ImGui::TextDisabled("--");
    ImGui::TableSetColumnIndex(3);
    if (available) ImGui::Text("%lld", snapshot.peak);
    else ImGui::TextDisabled("--");
}

void drawRawTimingRow(const char* sectionName) {
    FrameProfiler::SectionSnapshot snapshot;
    const bool available =
        FrameProfiler::get().getSectionSnapshot(sectionName, snapshot) &&
        snapshot.count != 0;
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(sectionName);
    for (int column = 1; column < 4; ++column) {
        ImGui::TableSetColumnIndex(column);
        if (!available) {
            ImGui::TextDisabled("--");
            continue;
        }
        const float value = column == 1 ? snapshot.latestMs
            : (column == 2 ? snapshot.averageMs : snapshot.peakMs);
        ImGui::Text("%.2f ms", value);
    }
}

void drawRawCounterRow(const char* counterName) {
    FrameProfiler::CounterSnapshot snapshot;
    const bool available =
        FrameProfiler::get().getCounterSnapshot(counterName, snapshot) &&
        snapshot.count != 0;
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(counterName);
    ImGui::TableSetColumnIndex(1);
    if (available) ImGui::Text("%lld", snapshot.latest);
    else ImGui::TextDisabled("--");
    ImGui::TableSetColumnIndex(2);
    if (available) ImGui::Text("%.1f", snapshot.average);
    else ImGui::TextDisabled("--");
    ImGui::TableSetColumnIndex(3);
    if (available) ImGui::Text("%lld", snapshot.peak);
    else ImGui::TextDisabled("--");
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
    ImGui::Separator();

    if (ImGui::BeginChild(
            "##ProfilerScrollableBody",
            ImVec2(0.0f, 0.0f),
            false)) {
        if (ImGui::CollapsingHeader(
                Loc::t(Loc::LocKey::ProfilerGpuTiming),
                ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextWrapped(
                "%s", Loc::t(Loc::LocKey::ProfilerGpuTimingHint));
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
                } else if (beginMetricTable("##ProfilerGpuTimingTable")) {
                    drawGpuTimingRow(
                        "gpuTotal", Loc::LocKey::ProfilerGpuTotal);
                    drawGpuTimingRow(
                        "gpuShadow", Loc::LocKey::ProfilerShadow);
                    drawGpuTimingRow(
                        "gpuMain", Loc::LocKey::ProfilerMainGeometry);
                    drawGpuTimingRow(
                        "gpuSurfaceMarks", Loc::LocKey::ProfilerSurfaceMarks);
                    drawGpuTimingRow(
                        "gpuExtras", Loc::LocKey::ProfilerExtras);
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

        if (ImGui::CollapsingHeader(
                Loc::t(Loc::LocKey::ProfilerRenderingBreakdown),
                ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextWrapped(
                "%s", Loc::t(Loc::LocKey::ProfilerNestedHint));
            if (beginMetricTable("##ProfilerRenderingBreakdownTable")) {
                drawTimingRow("shadow", Loc::LocKey::ProfilerShadow);
                drawTimingRow("main", Loc::LocKey::ProfilerMainGeometry);
                drawRawTimingRow("render.instanceCollect");
                drawRawTimingRow("render.shadowCull");
                drawRawTimingRow("render.shadowUpload");
                drawRawTimingRow("render.shadowDraw");
                drawRawTimingRow("render.mainInstanceUpload");
                drawRawTimingRow("render.mainInstanceDraw");
                drawTimingRow(
                    "surfaceMarks", Loc::LocKey::ProfilerSurfaceMarks);
                drawTimingRow("extras", Loc::LocKey::ProfilerExtras);
                drawTimingRow("highlights", Loc::LocKey::ProfilerHighlights);
                drawTimingRow("constraints", Loc::LocKey::ProfilerConstraints);
                drawTimingRow("renderDebug", Loc::LocKey::ProfilerRenderDebug);
                drawTimingRow("terrain", Loc::LocKey::ProfilerTerrain);
                drawTimingRow("weather", Loc::LocKey::ProfilerWeather);
                drawTimingRow("particles", Loc::LocKey::ProfilerParticles);
                drawTimingRow(
                    "selectionOutline", Loc::LocKey::ProfilerSelectionOutline);
                drawTimingRow("postEffects", Loc::LocKey::ProfilerPostEffects);
                drawTimingRow(
                    "surfaceGuiBakes",
                    Loc::LocKey::ProfilerSurfaceGuiBakeTime);
                drawTimingRow("ui", Loc::LocKey::ProfilerEditorUi);
                drawTimingRow("swap", Loc::LocKey::ProfilerSwap);
                ImGui::EndTable();
            }
        }

        if (ImGui::CollapsingHeader(
                "Editor UI Breakdown",
                ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextWrapped(
                "Per-panel CPU time inside EditorManager::render().");
            if (beginMetricTable("##ProfilerEditorUiBreakdownTable")) {
                drawRawTimingRow("ui.sceneHierarchy");
                drawRawTimingRow("ui.properties");
                drawRawTimingRow("ui.viewportPanelTotal");
                drawRawTimingRow("ui.viewportScene");
                drawRawTimingRow("ui.contentBrowser");
                drawRawTimingRow("ui.console");
                drawRawTimingRow("ui.animationEditor");
                drawRawTimingRow("ui.welcome");
                drawRawTimingRow("ui.profiler");
                ImGui::EndTable();
            }
        }

        if (ImGui::CollapsingHeader(
                "Physics Breakdown",
                ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextWrapped(
                "Box3D internal step and Recubin physics synchronization time.");
            if (beginMetricTable("##ProfilerPhysicsBreakdownTable")) {
                drawRawTimingRow("physics.buoyancy");
                drawRawTimingRow("physics.forces");
                drawRawTimingRow("physics.gyro");
                drawRawTimingRow("physics.box3dStep");
                drawRawTimingRow("physics.maintainedVelocity");
                drawRawTimingRow("physics.contactEvents");
                drawRawTimingRow("physics.syncCubes");
                drawRawCounterRow("physicsBodies");
                drawRawCounterRow("physicsContacts");
                drawRawCounterRow("physicsAwakeBodies");
                drawRawCounterRow("physicsAwakeBeforeStep");
                drawRawCounterRow("physicsAwakeAfterStep");
                drawRawCounterRow("physicsAwakeAfterMaintain");
                ImGui::EndTable();
            }
        }

        if (ImGui::CollapsingHeader(
                Loc::t(Loc::LocKey::ProfilerDrawCounters),
                ImGuiTreeNodeFlags_DefaultOpen) &&
            beginMetricTable("##ProfilerDrawCountersTable")) {
            drawCounterRow("cubesDrawn", Loc::LocKey::ProfilerCubesDrawn);
            drawCounterRow("cubesCulled", Loc::LocKey::ProfilerCubesCulled);
            drawCounterRow("instanced", Loc::LocKey::ProfilerInstanced);
            drawCounterRow("shadowCubes", Loc::LocKey::ProfilerShadowCubes);
            drawCounterRow(
                "shadowCubesCulled", Loc::LocKey::ProfilerShadowCubesCulled);
            drawRawCounterRow("shadowMapReused");
            drawCounterRow(
                "surfaceGuiBaked", Loc::LocKey::ProfilerSurfaceGuiBaked);
            drawCounterRow(
                "surfaceGuiReused", Loc::LocKey::ProfilerSurfaceGuiReused);
            drawRawCounterRow("treeLightingNodes");
            drawRawCounterRow("treeInstancesNodes");
            drawRawCounterRow("treeShadowNodes");
            drawRawCounterRow("treeMainNodes");
            drawRawCounterRow("treeSurfaceMarkNodes");
            drawRawCounterRow("treeGuiNodes");
            drawRawCounterRow("baseCubesVisited");
            drawRawCounterRow("surfaceGuiChildrenVisited");
            ImGui::EndTable();
        }

        if (ImGui::CollapsingHeader("Tree Traversal", ImGuiTreeNodeFlags_DefaultOpen) &&
            beginMetricTable("##ProfilerTreeTraversalTable")) {
            drawRawTimingRow("treeLighting");
            drawRawTimingRow("treeInstances");
            drawRawTimingRow("treeShadow");
            drawRawTimingRow("treeMain");
            drawRawTimingRow("treeSurfaceMarks");
            drawRawTimingRow("treeGui");
            drawRawTimingRow("treeGuiFonts");
            drawRawCounterRow("treeLightingNodes");
            drawRawCounterRow("treeInstancesNodes");
            drawRawCounterRow("treeShadowNodes");
            drawRawCounterRow("treeMainNodes");
            drawRawCounterRow("treeSurfaceMarkNodes");
            drawRawCounterRow("treeGuiNodes");
            drawRawCounterRow("baseCubesVisited");
            drawRawCounterRow("surfaceGuiChildrenVisited");
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();

    ImGui::End();
}

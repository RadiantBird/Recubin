#include <Editor/AudioDebugPanel.hpp>

#include <Core/AudioService.hpp>
#include <Editor/Localization.hpp>
#include <Instances/Sound.hpp>
#include <Util/Logger.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <vector>

namespace {
constexpr ImU32 COLOR_PEAK_L   = IM_COL32(90, 200, 255, 255);
constexpr ImU32 COLOR_PEAK_R   = IM_COL32(255, 170, 70, 255);
constexpr ImU32 COLOR_RMS      = IM_COL32(120, 255, 140, 255);
constexpr ImU32 COLOR_DELTA    = IM_COL32(255, 90, 90, 255);
constexpr ImU32 COLOR_INTERVAL = IM_COL32(220, 160, 255, 255);
constexpr ImU32 COLOR_VOLUME   = IM_COL32(120, 255, 140, 255);
constexpr ImU32 COLOR_MIX      = IM_COL32(255, 220, 90, 255);
constexpr ImU32 COLOR_CURSOR   = IM_COL32(90, 200, 255, 255);
constexpr ImU32 COLOR_DISTANCE = IM_COL32(255, 170, 70, 255);
constexpr ImU32 COLOR_PAN      = IM_COL32(220, 160, 255, 255);
constexpr ImU32 COLOR_PLAYING  = IM_COL32(255, 255, 255, 255);
constexpr ImU32 COLOR_WARNING  = IM_COL32(255, 80, 80, 255);

constexpr float CLICK_WARNING_DELTA = 0.3f;  // サンプル間の差がこれを超えると、聞こえるクリックになりやすい

struct PlotSeries {
    const char* name = "";
    ImU32 color = IM_COL32_WHITE;
    std::vector<ImVec2> points;  // x = 現在からの相対秒（負）、y = 値
    bool step = false;
};

struct PlotMarker {
    float x = 0.0f;
    ImU32 color = IM_COL32_WHITE;
};

ImU32 eventColor(const std::string& call) {
    if (call == "Play") return IM_COL32(90, 230, 120, 200);
    if (call == "Reset") return IM_COL32(255, 220, 80, 200);
    if (call == "Stop") return IM_COL32(255, 90, 90, 200);
    return IM_COL32(90, 160, 255, 200);
}

float valueAt(const std::vector<ImVec2>& points, float x, bool step) {
    if (points.empty()) return 0.0f;
    const auto upper = std::lower_bound(points.begin(), points.end(), x,
        [](const ImVec2& point, float value) { return point.x < value; });
    if (upper == points.begin()) return points.front().y;
    if (upper == points.end()) return points.back().y;
    const ImVec2& after = *upper;
    const ImVec2& before = *(upper - 1);
    if (step) return before.y;
    const float span = after.x - before.x;
    return span <= 0.0f ? after.y : before.y + (after.y - before.y) * ((x - before.x) / span);
}

// 時系列の描画。x は現在からの相対秒。ホバーで各系列の値を表示する。
void drawPlot(const char* id, float height, float xMin, float xMax, float yMin, float yMax,
              const std::vector<PlotSeries>& series, const std::vector<PlotMarker>& markers,
              float hline = std::numeric_limits<float>::quiet_NaN(), ImU32 hlineColor = COLOR_WARNING) {
    const float width = std::max(80.0f, ImGui::GetContentRegionAvail().x);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 corner(origin.x + width, origin.y + height);
    draw->AddRectFilled(origin, corner, IM_COL32(20, 22, 28, 255));
    draw->PushClipRect(origin, corner, true);

    const float ySpan = std::max(1.0e-9f, yMax - yMin);
    const auto toScreen = [&](float x, float y) {
        const float tx = (x - xMin) / std::max(1.0e-9f, xMax - xMin);
        const float ty = (y - yMin) / ySpan;
        return ImVec2(origin.x + tx * width, corner.y - std::clamp(ty, -0.05f, 1.05f) * height);
    };

    for (int i = 0; i <= 4; ++i) {
        const float value = yMin + ySpan * static_cast<float>(i) / 4.0f;
        const ImVec2 left = toScreen(xMin, value);
        draw->AddLine(ImVec2(origin.x, left.y), ImVec2(corner.x, left.y), IM_COL32(60, 64, 76, 255));
        char label[32];
        std::snprintf(label, sizeof(label), "%.3g", value);
        draw->AddText(ImVec2(origin.x + 3.0f, left.y - 13.0f), IM_COL32(150, 155, 170, 255), label);
    }
    for (const PlotMarker& marker : markers) {
        if (marker.x < xMin || marker.x > xMax) continue;
        const ImVec2 top = toScreen(marker.x, yMax);
        draw->AddLine(ImVec2(top.x, origin.y), ImVec2(top.x, corner.y), marker.color, 1.0f);
    }
    if (!std::isnan(hline)) {
        const ImVec2 line = toScreen(xMin, hline);
        draw->AddLine(ImVec2(origin.x, line.y), ImVec2(corner.x, line.y), hlineColor, 1.0f);
    }
    for (const PlotSeries& one : series) {
        std::vector<ImVec2> screen;
        screen.reserve(one.points.size() * (one.step ? 2 : 1));
        for (std::size_t i = 0; i < one.points.size(); ++i) {
            const ImVec2& point = one.points[i];
            if (point.x < xMin - 1.0f) continue;
            if (one.step && !screen.empty()) screen.push_back(toScreen(point.x, one.points[i - 1].y));
            screen.push_back(toScreen(point.x, point.y));
        }
        if (screen.size() >= 2)
            draw->AddPolyline(screen.data(), static_cast<int>(screen.size()), one.color, 0, 1.5f);
    }
    draw->PopClipRect();
    draw->AddRect(origin, corner, IM_COL32(90, 95, 110, 255));

    if (hovered) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const float x = xMin + (mouse.x - origin.x) / width * (xMax - xMin);
        draw->AddLine(ImVec2(mouse.x, origin.y), ImVec2(mouse.x, corner.y), IM_COL32(255, 255, 255, 90));
        ImGui::BeginTooltip();
        ImGui::Text("t = %.3f s", x);
        for (const PlotSeries& one : series)
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(one.color), "%s: %.4f", one.name,
                               valueAt(one.points, x, one.step));
        ImGui::EndTooltip();
    }
}

void legend(const std::vector<std::pair<const char*, ImU32>>& entries) {
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (i > 0) ImGui::SameLine();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(entries[i].second), "-- %s", entries[i].first);
    }
}

std::string timestampedName(const char* stem, const char* extension) {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char buffer[64];
    std::strftime(buffer, sizeof(buffer), "%Y%m%d_%H%M%S", &local);
    return std::string(stem) + "_" + buffer + extension;
}
} // namespace

// 出力経路の健全性を、ファイルや距離減衰に依らず確かめるための合成トーン。
struct AudioDebugPanel::Tone {
    ma_waveform waveform{};
    ma_sound sound{};
    bool waveformReady = false;
    bool soundReady = false;
    double stopAt = 0.0;
};

AudioDebugPanel::AudioDebugPanel() : EditorPanel("###AudioDebug") {}

AudioDebugPanel::~AudioDebugPanel() { stopTone(); }

double AudioDebugPanel::timeNow() const {
    return m_paused ? m_frozenNow : AudioDiagnostics::get().now();
}

void AudioDebugPanel::selectDefaultSound() {
    const auto& snapshots = AudioDiagnostics::get().latestSnapshots();
    if (snapshots.empty()) return;
    if (m_selectedKey.empty() || snapshots.find(m_selectedKey) == snapshots.end())
        m_selectedKey = snapshots.begin()->first;
}

void AudioDebugPanel::onRender() {
    if (dockspaceId != 0) ImGui::SetNextWindowDockID(dockspaceId, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(760.0f, 620.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title.c_str(), &isOpen)) {
        ImGui::End();
        return;
    }
    updateTone();
    runRepeatTest(ImGui::GetIO().DeltaTime);
    selectDefaultSound();

    auto& diag = AudioDiagnostics::get();
    if (ImGui::Button(m_paused ? "Resume" : "Pause")) {
        m_paused = !m_paused;
        m_frozenNow = AudioDiagnostics::now();
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) diag.clear();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderFloat("window (s)", &m_windowSeconds, 2.0f, 30.0f, "%.0f");
    ImGui::SameLine();
    if (ImGui::Button("Copy report")) {
        ImGui::SetClipboardText(buildFullReport().c_str());
        m_status = "Report copied to the clipboard";
    }
    ImGui::SameLine();
    if (ImGui::Button("Save files")) saveFiles();
    if (!m_status.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", m_status.c_str());
    }

    if (ImGui::BeginTabBar("##audiodebugtabs")) {
        if (ImGui::BeginTabItem("Overview")) { drawOverview(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Sounds")) { drawSounds(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Waveform")) { drawWaveform(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Events")) { drawEvents(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Test")) { drawTest(); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void AudioDebugPanel::drawOverview() {
    auto& diag = AudioDiagnostics::get();
    const AudioDiag::DeviceInfo device = diag.deviceInfo();
    if (!device.engineReady) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(COLOR_WARNING),
                           "The audio engine is not initialized (no output device?).");
        return;
    }
    ImGui::Text("Device: %s   backend: %s", device.hasDevice ? device.deviceName.c_str() : "(none)",
                device.backend.c_str());
    ImGui::Text("Sample rate %u Hz   channels %u   period %u frames x %u   expected interval %.2f ms",
                device.sampleRate, device.channels, device.periodFrames, device.periods,
                diag.expectedBlockInterval() * 1000.0);

    const AudioDiag::OutputStats& stats = diag.stats();
    ImGui::Separator();
    ImGui::Text("blocks %llu   silent %llu", static_cast<unsigned long long>(stats.blocks),
                static_cast<unsigned long long>(stats.silentBlocks));
    ImGui::Text("late callbacks %llu (max interval %.2f ms)   clipped blocks %llu   non-finite blocks %llu",
                static_cast<unsigned long long>(stats.lateCallbacks), stats.maxInterval * 1000.0,
                static_cast<unsigned long long>(stats.clippedBlocks),
                static_cast<unsigned long long>(stats.nonFiniteBlocks));
    const bool clicky = stats.maxDelta > CLICK_WARNING_DELTA;
    ImGui::Text("max peak %.3f   ", stats.maxPeak);
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::TextColored(clicky ? ImGui::ColorConvertU32ToFloat4(COLOR_WARNING) : ImVec4(1, 1, 1, 1),
                       "max sample delta %.3f%s", stats.maxDelta, clicky ? "  (click-like jump)" : "");

    const double now = timeNow();
    const float xMin = -m_windowSeconds;
    std::vector<PlotSeries> peaks{{"peak L", COLOR_PEAK_L, {}, false}, {"peak R", COLOR_PEAK_R, {}, false},
                                  {"rms", COLOR_RMS, {}, false}};
    std::vector<PlotSeries> deltas{{"max delta", COLOR_DELTA, {}, false}};
    std::vector<PlotSeries> intervals{{"interval ms", COLOR_INTERVAL, {}, false}};
    float peakMax = 0.1f, deltaMax = 0.1f, intervalMax = 5.0f;
    const auto& blocks = diag.blocks();
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        const float x = static_cast<float>(blocks[i].time - now);
        if (x < xMin) continue;
        if (x > 0.0f) break;
        peaks[0].points.push_back(ImVec2(x, blocks[i].peak[0]));
        peaks[1].points.push_back(ImVec2(x, blocks[i].peak[1]));
        peaks[2].points.push_back(ImVec2(x, blocks[i].rms));
        deltas[0].points.push_back(ImVec2(x, blocks[i].maxDelta));
        peakMax = std::max({peakMax, blocks[i].peak[0], blocks[i].peak[1]});
        deltaMax = std::max(deltaMax, blocks[i].maxDelta);
        if (i > 0) {
            const float interval = static_cast<float>((blocks[i].time - blocks[i - 1].time) * 1000.0);
            intervals[0].points.push_back(ImVec2(x, interval));
            intervalMax = std::max(intervalMax, interval);
        }
    }
    ImGui::Separator();
    ImGui::TextUnformatted("Output level (after all sounds are mixed)");
    legend({{"peak L", COLOR_PEAK_L}, {"peak R", COLOR_PEAK_R}, {"rms", COLOR_RMS}});
    drawPlot("##peaks", 130.0f, xMin, 0.0f, 0.0f, std::min(1.1f, peakMax * 1.15f), peaks, {});
    ImGui::TextUnformatted("Largest jump between adjacent samples per block (clicks / pops)");
    drawPlot("##deltas", 100.0f, xMin, 0.0f, 0.0f, std::max(deltaMax * 1.15f, CLICK_WARNING_DELTA * 1.2f),
             deltas, {}, CLICK_WARNING_DELTA);
    ImGui::TextUnformatted("Time between audio callbacks (ms). Spikes mean the audio thread was starved");
    const float expectedMs = static_cast<float>(diag.expectedBlockInterval() * 1000.0);
    drawPlot("##intervals", 90.0f, xMin, 0.0f, 0.0f, std::max(intervalMax * 1.15f, 5.0f), intervals, {},
             expectedMs > 0.0f ? expectedMs * 1.5f : std::numeric_limits<float>::quiet_NaN());
}

void AudioDebugPanel::drawSounds() {
    auto& diag = AudioDiagnostics::get();
    const auto& snapshots = diag.latestSnapshots();
    if (snapshots.empty()) {
        ImGui::TextDisabled("No sounds are registered. Sounds are tracked only while they are under a Workspace "
                            "(the Tool must be equipped and the game running).");
        return;
    }
    if (ImGui::BeginCombo("sound", m_selectedKey.c_str())) {
        for (const auto& [key, snapshot] : snapshots) {
            if (ImGui::Selectable(key.c_str(), key == m_selectedKey)) m_selectedKey = key;
        }
        ImGui::EndCombo();
    }
    const auto found = snapshots.find(m_selectedKey);
    if (found == snapshots.end()) return;
    const AudioDiag::SoundSnapshot& s = found->second;

    ImGui::Text("path: %s   group: %s   loaded: %s", s.path.c_str(), s.group.c_str(), s.loaded ? "yes" : "NO");
    ImGui::Text("playing: %s   at end: %s   looping: %s   length %.3f s   cursor %.3f s",
                s.playing ? "yes" : "no", s.atEnd ? "yes" : "no", s.looping ? "yes" : "no", s.length, s.cursor);
    ImGui::Text("Volume %.3f   applied %.4f   mix %.4f   distance %.2f   pan %.3f   pitch %.3f   speed %.3f%s",
                s.propertyVolume, s.appliedVolume, s.mixVolume, s.distance, s.pan, s.pitch, s.speed,
                s.preservePitch ? "   (preserve pitch)" : "");
    ImGui::Text("sound world (%.1f, %.1f, %.1f)   listener (%.1f, %.1f, %.1f)   spatial mix %s",
                s.worldPosition[0], s.worldPosition[1], s.worldPosition[2], s.listenerPosition[0],
                s.listenerPosition[1], s.listenerPosition[2], s.spatialMixActive ? "active" : "off");
    for (const std::string& ancestor : s.ancestry) ImGui::TextDisabled("  ancestor %s", ancestor.c_str());
    if (s.distance > 0.0f && s.mixVolume < s.propertyVolume * 0.2f) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(COLOR_WARNING),
                           "Distance attenuation is cutting the volume to %.0f%%. The listener is far from this Sound.",
                           s.propertyVolume > 0.0f ? 100.0f * s.mixVolume / s.propertyVolume : 0.0f);
    }

    const auto seriesIt = diag.soundSeries().find(m_selectedKey);
    if (seriesIt == diag.soundSeries().end()) return;
    const double now = timeNow();
    const float xMin = -m_windowSeconds;
    std::vector<PlotSeries> cursor{{"cursor s", COLOR_CURSOR, {}, false}, {"playing", COLOR_PLAYING, {}, true}};
    std::vector<PlotSeries> volume{{"applied volume", COLOR_VOLUME, {}, false}, {"mix volume", COLOR_MIX, {}, false}};
    std::vector<PlotSeries> distance{{"distance", COLOR_DISTANCE, {}, false}};
    std::vector<PlotSeries> pan{{"pan", COLOR_PAN, {}, false}};
    float cursorMax = std::max(0.1f, s.length), volumeMax = 0.1f, distanceMax = 1.0f;
    for (const AudioDiag::SoundSample& sample : seriesIt->second) {
        const float x = static_cast<float>(sample.time - now);
        if (x < xMin) continue;
        if (x > 0.0f) break;
        cursor[0].points.push_back(ImVec2(x, sample.cursor));
        cursor[1].points.push_back(ImVec2(x, sample.playing ? cursorMax : 0.0f));
        volume[0].points.push_back(ImVec2(x, sample.appliedVolume));
        volume[1].points.push_back(ImVec2(x, sample.mixVolume));
        distance[0].points.push_back(ImVec2(x, sample.distance));
        pan[0].points.push_back(ImVec2(x, sample.pan));
        volumeMax = std::max({volumeMax, sample.appliedVolume, sample.mixVolume});
        distanceMax = std::max(distanceMax, sample.distance);
    }
    std::vector<PlotMarker> markers;
    for (const AudioDiag::SoundEvent& event : diag.events()) {
        if (event.sound != m_selectedKey) continue;
        markers.push_back({static_cast<float>(event.time - now), eventColor(event.call)});
    }
    ImGui::Separator();
    legend({{"Play", IM_COL32(90, 230, 120, 255)}, {"Reset", IM_COL32(255, 220, 80, 255)},
            {"Stop", IM_COL32(255, 90, 90, 255)}, {"Seek", IM_COL32(90, 160, 255, 255)}});
    ImGui::TextUnformatted("Playback position (the white step is 'playing')");
    drawPlot("##cursor", 110.0f, xMin, 0.0f, 0.0f, cursorMax * 1.05f, cursor, markers);
    ImGui::TextUnformatted("Volume handed to the audio engine");
    drawPlot("##volume", 100.0f, xMin, 0.0f, 0.0f, volumeMax * 1.15f, volume, markers);
    ImGui::TextUnformatted("Distance to the listener");
    drawPlot("##distance", 80.0f, xMin, 0.0f, 0.0f, distanceMax * 1.1f, distance, markers);
    ImGui::TextUnformatted("Pan (-1 left .. +1 right)");
    drawPlot("##pan", 70.0f, xMin, 0.0f, -1.1f, 1.1f, pan, markers);
}

void AudioDebugPanel::drawWaveform() {
    const auto& snapshots = AudioDiagnostics::get().latestSnapshots();
    const auto found = snapshots.find(m_selectedKey);
    std::string path = found != snapshots.end() ? found->second.path : std::string();
    ImGui::Text("selected sound: %s", m_selectedKey.empty() ? "(none)" : m_selectedKey.c_str());
    ImGui::Text("file: %s", path.empty() ? "(none)" : path.c_str());
    ImGui::BeginDisabled(path.empty());
    if (ImGui::Button("Analyze file")) m_waveform = AudioDiagnostics::analyzeWaveform(path);
    ImGui::EndDisabled();
    if (!m_waveform.ok) {
        if (!m_waveform.error.empty())
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(COLOR_WARNING), "%s", m_waveform.error.c_str());
        return;
    }
    const AudioDiag::WaveformAnalysis& w = m_waveform;
    ImGui::Text("%.3f s   %u Hz   %u ch   peak %.3f   rms %.4f   DC offset %.5f", w.duration, w.sampleRate,
                w.channels, w.peak, w.rms, w.dcOffset);
    ImGui::Text("first sample %.4f   last sample %.4f   leading silence %.1f ms", w.firstSample, w.lastSample,
                w.leadingSilenceMs);
    if (std::fabs(w.lastSample) > 0.02f)
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(COLOR_WARNING),
                           "The file ends abruptly (last sample %.3f): it can pop when it finishes.", w.lastSample);

    ImGui::SliderFloat("restart interval (ms)", &m_cutIntervalMs, 20.0f, 1000.0f, "%.0f");
    const auto cuts = AudioDiagnostics::analyzeCuts(w, m_cutIntervalMs / 1000.0);

    // 波形（上下のエンベロープ）と、巻き戻し位置の縦線
    const float width = std::max(100.0f, ImGui::GetContentRegionAvail().x);
    const float height = 150.0f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##waveform", ImVec2(width, height));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 corner(origin.x + width, origin.y + height);
    draw->AddRectFilled(origin, corner, IM_COL32(20, 22, 28, 255));
    const float mid = origin.y + height * 0.5f;
    draw->AddLine(ImVec2(origin.x, mid), ImVec2(corner.x, mid), IM_COL32(60, 64, 76, 255));
    const float scale = height * 0.5f / std::max(0.05f, w.peak);
    const std::size_t buckets = w.envelopeMax.size();
    for (std::size_t i = 0; i < buckets; ++i) {
        const float x = origin.x + width * (static_cast<float>(i) + 0.5f) / static_cast<float>(buckets);
        draw->AddLine(ImVec2(x, mid - w.envelopeMax[i] * scale), ImVec2(x, mid - w.envelopeMin[i] * scale),
                      IM_COL32(90, 200, 255, 255));
    }
    for (const AudioDiag::CutPoint& cut : cuts) {
        const float x = origin.x + width * static_cast<float>(cut.time / w.duration);
        const float magnitude = std::fabs(cut.value);
        draw->AddLine(ImVec2(x, origin.y), ImVec2(x, corner.y),
                      magnitude > 0.1f ? COLOR_WARNING : IM_COL32(255, 220, 80, 200), 1.5f);
    }
    draw->AddRect(origin, corner, IM_COL32(90, 95, 110, 255));

    ImGui::TextUnformatted("Each restart jumps from the value below back to ~0. A big value is a click:");
    if (cuts.empty()) ImGui::TextDisabled("The file is shorter than the restart interval; it is never cut.");
    for (const AudioDiag::CutPoint& cut : cuts) {
        const float magnitude = std::fabs(cut.value);
        ImGui::TextColored(magnitude > 0.1f ? ImGui::ColorConvertU32ToFloat4(COLOR_WARNING) : ImVec4(1, 1, 1, 1),
                           "  cut at %.3f s: sample %.4f%s", cut.time, cut.value,
                           magnitude > 0.1f ? "   <-- audible click" : "");
    }
}

void AudioDebugPanel::drawEvents() {
    const auto& events = AudioDiagnostics::get().events();
    if (ImGui::BeginTable("##events", 7,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Borders |
                              ImGuiTableFlags_Resizable,
                          ImVec2(0, ImGui::GetContentRegionAvail().y))) {
        ImGui::TableSetupColumn("t (s)");
        ImGui::TableSetupColumn("sound");
        ImGui::TableSetupColumn("call");
        ImGui::TableSetupColumn("playing");
        ImGui::TableSetupColumn("at end");
        ImGui::TableSetupColumn("cursor");
        ImGui::TableSetupColumn("volume");
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        for (std::size_t i = events.size(); i-- > 0;) {
            const AudioDiag::SoundEvent& event = events[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("%.3f", event.time);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(event.sound.c_str());
            ImGui::TableNextColumn();
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(eventColor(event.call)), "%s", event.call.c_str());
            ImGui::TableNextColumn(); ImGui::Text("%d -> %d", event.playingBefore, event.playingAfter);
            ImGui::TableNextColumn(); ImGui::Text("%d", event.atEndBefore);
            ImGui::TableNextColumn(); ImGui::Text("%.3f -> %.3f", event.cursorBefore, event.cursorAfter);
            ImGui::TableNextColumn(); ImGui::Text("%.4f", event.appliedVolume);
        }
        ImGui::EndTable();
    }
}

void AudioDebugPanel::drawTest() {
    auto& diag = AudioDiagnostics::get();
    ImGui::TextWrapped("Reproduce the symptom without a script, and bisect which stage causes it.");
    ImGui::SeparatorText("Bisect switches");
    ImGui::Checkbox("Bypass spatial mix (use Volume as-is, no distance attenuation or pan)",
                    &diag.settings().bypassSpatial);

    ImGui::SeparatorText("Gun-fire simulation on the selected sound");
    ImGui::Text("sound: %s", m_selectedKey.empty() ? "(none)" : m_selectedKey.c_str());
    ImGui::SliderFloat("interval (ms)", &m_repeatIntervalMs, 50.0f, 1000.0f, "%.0f");
    ImGui::Checkbox("Reset before Play", &m_resetBeforePlay);
    ImGui::SameLine();
    ImGui::Checkbox("Stop before Reset", &m_stopBeforeReset);
    ImGui::BeginDisabled(m_selectedKey.empty());
    if (ImGui::Button(m_repeating ? "Stop repeating" : "Start repeating")) {
        m_repeating = !m_repeating;
        m_repeatTimer = 0.0;
    }
    ImGui::SameLine();
    if (ImGui::Button("Fire once")) m_repeatTimer = 1.0e9;  // 次のrunRepeatTestで1回撃つ
    ImGui::EndDisabled();

    ImGui::SeparatorText("Output path check");
    if (ImGui::Button("Beep 440 Hz (0.3 s)")) startTone();
    ImGui::SameLine();
    ImGui::TextDisabled("A clean beep here but a bad Sound points at the file or the per-Sound path, "
                        "not the device.");
}

void AudioDebugPanel::runRepeatTest(double deltaSeconds) {
    if (m_selectedKey.empty() || !AudioService::instance) return;
    const bool fireOnce = m_repeatTimer >= 1.0e8;
    if (!m_repeating && !fireOnce) return;
    m_repeatTimer += deltaSeconds;
    if (!fireOnce && m_repeatTimer < m_repeatIntervalMs / 1000.0) return;
    m_repeatTimer = 0.0;
    for (const auto& sound : AudioService::instance->liveSounds()) {
        if (sound->getFullPath() != m_selectedKey) continue;
        if (m_stopBeforeReset) sound->stop();
        if (m_resetBeforePlay) sound->reset();
        sound->play();
        break;
    }
}

void AudioDebugPanel::startTone() {
    stopTone();
    if (!AudioService::instance) return;
    AudioService& audio = *AudioService::instance;
    auto tone = std::make_unique<Tone>();
    const ma_waveform_config config = ma_waveform_config_init(
        ma_format_f32, ma_engine_get_channels(&audio.engine), ma_engine_get_sample_rate(&audio.engine),
        ma_waveform_type_sine, 0.3, 440.0);
    if (ma_waveform_init(&config, &tone->waveform) != MA_SUCCESS) return;
    tone->waveformReady = true;
    if (ma_sound_init_from_data_source(&audio.engine, &tone->waveform, 0, &audio.groupSFX, &tone->sound) != MA_SUCCESS) {
        ma_waveform_uninit(&tone->waveform);
        return;
    }
    tone->soundReady = true;
    ma_sound_start(&tone->sound);
    tone->stopAt = AudioDiagnostics::now() + 0.3;
    m_tone = std::move(tone);
}

void AudioDebugPanel::stopTone() {
    if (!m_tone) return;
    if (m_tone->soundReady) ma_sound_uninit(&m_tone->sound);
    if (m_tone->waveformReady) ma_waveform_uninit(&m_tone->waveform);
    m_tone.reset();
}

void AudioDebugPanel::updateTone() {
    if (m_tone && AudioDiagnostics::now() >= m_tone->stopAt) stopTone();
}

std::string AudioDebugPanel::buildFullReport() const {
    std::ostringstream out;
    out << AudioDiagnostics::get().buildReport();
    char buffer[160];
    if (m_waveform.ok) {
        out << "\n## Waveform of `" << m_waveform.path << "`\n";
        std::snprintf(buffer, sizeof(buffer),
                      "- %.3f s, %u Hz, %u ch, peak %.3f, rms %.4f, DC %.5f\n- first %.4f, last %.4f, leading silence %.1f ms\n",
                      m_waveform.duration, m_waveform.sampleRate, m_waveform.channels, m_waveform.peak,
                      m_waveform.rms, m_waveform.dcOffset, m_waveform.firstSample, m_waveform.lastSample,
                      m_waveform.leadingSilenceMs);
        out << buffer;
        out << "- restart every " << m_cutIntervalMs << " ms:\n";
        for (const AudioDiag::CutPoint& cut : AudioDiagnostics::analyzeCuts(m_waveform, m_cutIntervalMs / 1000.0)) {
            std::snprintf(buffer, sizeof(buffer), "  - cut at %.3f s: sample %.4f\n", cut.time, cut.value);
            out << buffer;
        }
    }
    return out.str();
}

void AudioDebugPanel::saveFiles() {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories("logs", ec);
    const std::string reportPath = "logs/" + timestampedName("audio_debug", ".md");
    const std::string csvPath = "logs/" + timestampedName("audio_debug", ".csv");
    std::ofstream report(reportPath, std::ios::binary | std::ios::trunc);
    report << buildFullReport();
    std::ofstream csv(csvPath, std::ios::binary | std::ios::trunc);
    csv << AudioDiagnostics::get().buildCsv();
    if (!report || !csv) {
        m_status = "Failed to write files";
        RCBN_ERROR("Audio debug: failed to write " << reportPath << " / " << csvPath);
        return;
    }
    m_status = "Saved " + reportPath + " and " + csvPath;
    RCBN_LOG("Audio debug: saved " << reportPath << " and " << csvPath);
}

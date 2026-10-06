#pragma once

#include <Core/AudioDiagnostics.hpp>
#include <Editor/EditorPanel.hpp>
#include <include/imgui/imgui.h>
#include <memory>
#include <string>

// ===================================================
//  AudioDebugPanel — 音声の異常（ノイズ・途切れ・小音量）の調査用パネル。
//   Overview : デバイス・出力メーターと、ピーク/クリック指標/コールバック間隔の時系列
//   Sounds   : Soundごとの再生位置・音量・距離・パンの時系列と、Play/Reset/Stopの記録
//   Waveform : 音声ファイルの波形と、連射で巻き戻す位置の波形の値（クリックの見込み）
//   Events   : Soundへの呼び出しログ
//   Test     : 連射の再現（Reset→Play）、空間減衰のバイパス、テストトーン、レポート出力
// ===================================================
class AudioDebugPanel final : public EditorPanel {
public:
    ImGuiID dockspaceId = 0;

    AudioDebugPanel();
    ~AudioDebugPanel() override;
    void onRender() override;

private:
    struct Tone;

    float  m_windowSeconds = 10.0f;
    bool   m_paused = false;
    double m_frozenNow = 0.0;
    std::string m_selectedKey;

    // Waveform
    AudioDiag::WaveformAnalysis m_waveform;
    float  m_cutIntervalMs = 200.0f;

    // Test
    bool   m_repeating = false;
    float  m_repeatIntervalMs = 200.0f;
    double m_repeatTimer = 0.0;
    bool   m_resetBeforePlay = true;
    bool   m_stopBeforeReset = false;
    std::string m_status;
    std::unique_ptr<Tone> m_tone;

    double timeNow() const;
    void drawOverview();
    void drawSounds();
    void drawWaveform();
    void drawEvents();
    void drawTest();
    void selectDefaultSound();
    void runRepeatTest(double deltaSeconds);
    void updateTone();
    void startTone();
    void stopTone();
    std::string buildFullReport() const;
    void saveFiles();
};

#pragma once

enum class InputState {
    Editor,          // Editモード中(物理/スクリプト停止)
    Gameplay,        // Play/Pauseモード中の通常のゲームプレイ入力
    ProximityPrompt  // いずれかのProximityPromptを押し続けている間
};

struct SystemState {
    bool isPlaying           = false;
    bool isPaused            = false;
    bool viewportFocused     = false;
    bool viewportZoomEnabled = false;
    InputState inputState    = InputState::Editor;

    static SystemState& get();
};

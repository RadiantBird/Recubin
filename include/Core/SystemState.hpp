#pragma once

struct SystemState {
    bool isPlaying          = false;
    bool isPaused           = false;
    bool viewportFocused    = false;
    bool viewportZoomEnabled = false;

    static SystemState& get();
};

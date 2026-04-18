#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include <vector>
#include <string>

class AudioService {
public:
    ma_engine engine;
    ma_sound_group groupSFX;
    ma_sound_group groupBGM;

    bool initialize() {
        if (ma_engine_init(NULL, &engine) != MA_SUCCESS) return false;
        
        // グループの初期化（マスターの下にぶら下がる）
        ma_sound_group_init(&engine, 0, NULL, &groupBGM);
        ma_sound_group_init(&engine, 0, NULL, &groupSFX);
        return true;
    }

    // カテゴリー全体の音量を操作
    void setBGMVolume(float volume) { ma_sound_group_set_volume(&groupBGM, volume); }
    void setSFXVolume(float volume) { ma_sound_group_set_volume(&groupSFX, volume); }

    void uninit() {
        ma_sound_group_uninit(&groupBGM);
        ma_sound_group_uninit(&groupSFX);
        ma_engine_uninit(&engine);
    }
};
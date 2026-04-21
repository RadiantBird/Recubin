#pragma once
#include "miniaudio.h"
#include "Instances/Instance.hpp"
#include <vector>

class Sound; // 前方宣言

class AudioService : public Instance {
private:
    std::vector<Sound*> sounds;
public:
    static AudioService* instance;
    ma_engine engine;
    ma_sound_group groupSFX;
    ma_sound_group groupBGM;

    AudioService();
    bool initialize();
    void setBGMVolume(float volume);
    void setSFXVolume(float volume);
    void addSound(Sound* sound);
    void updateSounds();
    void uninit();
};
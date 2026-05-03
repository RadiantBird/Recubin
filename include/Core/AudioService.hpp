#pragma once
#include "miniaudio.h"
#include "Instances/Instance.hpp"
#include "Math/Vector3.hpp"
#include <vector>

class Sound; // 前方宣言

class AudioService : public Instance {
private:
    std::vector<std::weak_ptr<Sound>> sounds;
public:
    static AudioService* instance;
    ma_engine engine;
    ma_sound_group groupSFX;
    ma_sound_group groupBGM;

    AudioService();
    bool initialize();
    void setBGMVolume(float volume);
    void setSFXVolume(float volume);
    void addSound(const std::shared_ptr<Sound>& sound);
    void removeSound(const std::shared_ptr<Sound>& sound);
    void updateSounds(const Vector3& listenerPos, const Vector3& listenerRight);
    void playAutoPlaySounds();
    void stopAllSounds();
    void uninit();
    ~AudioService();
};
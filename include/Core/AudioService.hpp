#pragma once
#include "miniaudio.h"
#include "Instances/Instance.hpp"
#include "Math/Vector3.hpp"
#include <vector>

class Sound; // 前方宣言

class AudioService : public Instance {
private:
    mutable std::vector<std::weak_ptr<Sound>> sounds;
public:
    struct InitializationOps {
        ma_result (*engineInit)(const ma_engine_config*, ma_engine*);
        ma_result (*groupInit)(ma_engine*, ma_uint32, ma_sound_group*, ma_sound_group*);
        void (*groupUninit)(ma_sound_group*);
        void (*engineUninit)(ma_engine*);
    };

    static AudioService* instance;
    ma_engine engine{};
    ma_sound_group groupSFX{};
    ma_sound_group groupBGM{};
private:
    bool m_engineInitialized = false;
    bool m_groupSFXInitialized = false;
    bool m_groupBGMInitialized = false;
    InitializationOps m_initializationOps;
public:

    AudioService();
    // Explicit operation seam for deterministic initialization/rollback tests.
    // Production code should use the default constructor.
    explicit AudioService(const InitializationOps& initializationOps);
    bool initialize();
    void setBGMVolume(float volume);
    void setSFXVolume(float volume);
    void addSound(const std::shared_ptr<Sound>& sound);
    void removeSound(const std::shared_ptr<Sound>& sound);
    std::size_t registeredSoundCount() const;
    void updateSounds(const Vector3& listenerPos, const Vector3& listenerRight);
    void playAutoPlaySounds();
    void stopAllSounds();
    void uninit();
    ~AudioService();
};

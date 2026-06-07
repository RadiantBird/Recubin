#define MINIAUDIO_IMPLEMENTATION
#include "Core/AudioService.hpp"
#include "Instances/Sound.hpp"
#include <algorithm>

#ifdef _WIN32
    #include <windows.h>
    #undef getClassName
#endif

AudioService* AudioService::instance = nullptr;
AudioService::AudioService() : Instance("AudioService") {}

bool AudioService::initialize() {
    instance = this;
    if (ma_engine_init(NULL, &engine) != MA_SUCCESS) return false;
    ma_sound_group_init(&engine, 0, NULL, &groupBGM);
    ma_sound_group_init(&engine, 0, NULL, &groupSFX);
    return true;
}

void AudioService::setBGMVolume(float volume) { ma_sound_group_set_volume(&groupBGM, volume); }
void AudioService::setSFXVolume(float volume) { ma_sound_group_set_volume(&groupSFX, volume); }

void AudioService::addSound(const std::shared_ptr<Sound>& sound) {
    sounds.push_back(std::weak_ptr<Sound>(sound));
}

void AudioService::removeSound(const std::shared_ptr<Sound>& sound) {
    sounds.erase(std::remove_if(sounds.begin(), sounds.end(), [&](const std::weak_ptr<Sound>& w) {
        return w.lock() == sound;
    }), sounds.end());
}

void AudioService::playAutoPlaySounds() {
    for (auto it = sounds.begin(); it != sounds.end(); ) {
        if (auto s = it->lock()) {
            if (s->autoPlay) s->play();
            ++it;
        } else {
            it = sounds.erase(it);
        }
    }
}

void AudioService::stopAllSounds() {
    for (auto it = sounds.begin(); it != sounds.end(); ) {
        if (auto s = it->lock()) {
            s->stop();
            ++it;
        } else {
            it = sounds.erase(it);
        }
    }
}

void AudioService::updateSounds(const Vector3& listenerPos, const Vector3& listenerRight) {
    for (auto it = sounds.begin(); it != sounds.end(); ) {
        if (auto s = it->lock()) {
            s->update3D(listenerPos, listenerRight);
            ++it;
        } else {
            it = sounds.erase(it);
        }
    }
}

void AudioService::uninit() {
    ma_sound_group_uninit(&groupBGM);
    ma_sound_group_uninit(&groupSFX);
    ma_engine_uninit(&engine);
}

AudioService::~AudioService() {
    uninit();
}
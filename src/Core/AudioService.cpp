#define MINIAUDIO_IMPLEMENTATION
#include "Core/AudioService.hpp"
#include "Instances/Sound.hpp"
#include <algorithm>

#ifdef _WIN32
    #include <windows.h>
    #undef GetClassName
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

void AudioService::addSound(Sound* sound) {
    sounds.push_back(sound);
}

void AudioService::removeSound(Sound* sound) {
    sounds.erase(std::remove(sounds.begin(), sounds.end(), sound), sounds.end());
}

void AudioService::playAutoPlaySounds() {
    for (Sound* s : sounds) {
        if (s->autoPlay) s->play();
    }
}

void AudioService::stopAllSounds() {
    for (Sound* s : sounds) s->stop();
}

void AudioService::updateSounds(const Vector3& listenerPos, const Vector3& listenerRight) {
    for (Sound* sound : sounds) {
        sound->update3D(listenerPos, listenerRight);
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
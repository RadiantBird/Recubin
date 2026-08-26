#define MINIAUDIO_IMPLEMENTATION
#include "Core/AudioService.hpp"
#include "Instances/Sound.hpp"
#include <algorithm>

#ifdef _WIN32
    #include <windows26.h>
    #undef getClassName
#endif

AudioService* AudioService::instance = nullptr;
AudioService::AudioService()
    : AudioService({ma_engine_init, ma_sound_group_init,
                    ma_sound_group_uninit, ma_engine_uninit}) {}

AudioService::AudioService(const InitializationOps& initializationOps)
    : Instance("AudioService"), m_initializationOps(initializationOps) {}

bool AudioService::initialize() {
    if (m_engineInitialized) {
        // Initialization is idempotent, but this instance remains the
        // authoritative singleton even if another failed instance attempted
        // initialization in the meantime.
        instance = this;
        return true;
    }
    if (instance != nullptr && instance != this) return false;
    if (!m_initializationOps.engineInit || !m_initializationOps.groupInit ||
        !m_initializationOps.groupUninit || !m_initializationOps.engineUninit) {
        return false;
    }
    if (m_initializationOps.engineInit(nullptr, &engine) != MA_SUCCESS) {
        return false;
    }
    m_engineInitialized = true;
    if (m_initializationOps.groupInit(&engine, 0, nullptr, &groupBGM) != MA_SUCCESS) {
        uninit();
        return false;
    }
    m_groupBGMInitialized = true;
    if (m_initializationOps.groupInit(&engine, 0, nullptr, &groupSFX) != MA_SUCCESS) {
        uninit();
        return false;
    }
    m_groupSFXInitialized = true;
    instance = this;
    return true;
}

void AudioService::setBGMVolume(float volume) { if (m_groupBGMInitialized) ma_sound_group_set_volume(&groupBGM, volume); }
void AudioService::setSFXVolume(float volume) { if (m_groupSFXInitialized) ma_sound_group_set_volume(&groupSFX, volume); }

void AudioService::addSound(const std::shared_ptr<Sound>& sound) {
    if (!sound) return;
    sounds.erase(std::remove_if(sounds.begin(), sounds.end(), [&](const std::weak_ptr<Sound>& w) {
        return w.expired();
    }), sounds.end());
    const auto alreadyRegistered = std::find_if(sounds.begin(), sounds.end(), [&](const std::weak_ptr<Sound>& w) {
        return w.lock() == sound;
    });
    if (alreadyRegistered != sounds.end()) return;
    sounds.push_back(std::weak_ptr<Sound>(sound));
}

void AudioService::removeSound(const std::shared_ptr<Sound>& sound) {
    sounds.erase(std::remove_if(sounds.begin(), sounds.end(), [&](const std::weak_ptr<Sound>& w) {
        return w.expired() || w.lock() == sound;
    }), sounds.end());
}

std::size_t AudioService::registeredSoundCount() const {
    sounds.erase(std::remove_if(sounds.begin(), sounds.end(), [](const std::weak_ptr<Sound>& w) {
        return w.expired();
    }), sounds.end());
    return sounds.size();
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
    if (m_groupBGMInitialized) {
        m_initializationOps.groupUninit(&groupBGM);
        m_groupBGMInitialized = false;
    }
    if (m_groupSFXInitialized) {
        m_initializationOps.groupUninit(&groupSFX);
        m_groupSFXInitialized = false;
    }
    if (m_engineInitialized) {
        m_initializationOps.engineUninit(&engine);
        m_engineInitialized = false;
    }
    if (instance == this) instance = nullptr;
}

AudioService::~AudioService() {
    uninit();
}

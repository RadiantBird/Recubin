#pragma once
#include "Core/AudioService.hpp"
#include "Instances/Spatial.hpp"

class Sound : public Spatial {
private:
    ma_sound sound;
    bool loaded = false;
    bool playing = false;

public:
    Sound(AudioService& service, const std::string& path, bool isBGM = false);
    void play();
    void stop();
    void setLooping(bool loop);
    void update3D();
    ~Sound();
};
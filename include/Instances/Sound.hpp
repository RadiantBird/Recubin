#pragma once
#include "Core/AudioService.hpp"
#include "Instances/Spatial.hpp"

class Sound : public Spatial {
private:
    ma_sound sound;
    bool loaded = false;
    bool looping = false;
    std::string soundGroup = "SFX";

public:
    Sound(AudioService& service, const std::string& path = "");
    void play();
    void stop();
    void setLooping(bool loop);
    void update3D();

    virtual void setProperty(const std::string& name, const YAML::Node& value) override;
    virtual std::string GetClassName() override { return "Sound"; }
    virtual bool IsA(std::string name) override;

    ~Sound();
};
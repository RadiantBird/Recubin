#pragma once
#include "Core/AudioService.hpp"
#include "Instances/Spatial.hpp"

class Sound : public Spatial {
private:
    ma_sound sound;
    bool loaded = false;
    bool looping = false;
    float m_volume = 1.0f;
    std::string soundGroup = "SFX";
    std::string m_currentPath = "";

public:
    Sound(AudioService& service, const std::string& path = "");
    void play();
    void stop();
    void setLooping(bool loop);
    void update3D(const Vector3& listenerPos, const Vector3& listenerRight);

    virtual void setProperty(const std::string& name, const YAML::Node& value) override;
    virtual std::string getClassName() override { return "Sound"; }
    virtual bool IsA(std::string name) override;
    virtual void onAncestorChanged() override;

    bool autoPlay = false;

    std::string getContentPath() const { return m_currentPath; }
    bool isLooping()   const { return looping;    }
    bool isPlaying()   const;
    bool getAutoPlay() const { return autoPlay;   }
    void setVolume(float v);
    float getVolume()  const;
    std::string getSoundGroup() const { return soundGroup; }

    ~Sound();
};
#pragma once
#include "Core/AudioService.hpp"
#include "Instances/Spatial.hpp"

class TimeStretchNode;

struct SoundSpatialMix {
    float volume = 0.0f;
    float pan = 0.0f;
};

class Sound : public Spatial {
private:
    ma_sound sound{};
    AudioService* m_audioService = nullptr;
    std::unique_ptr<TimeStretchNode> m_timeStretchNode;
    bool loaded = false;
    bool looping = false;
    float m_volume = 1.0f;
    float m_speed = 1.0f;
    bool m_preservePitch = false;
    std::string soundGroup = "SFX";
    std::string m_currentPath = "";

    ma_sound_group* getTargetGroup() const;
    void applyLoadedProperties();
    void updatePlaybackRouting();
    void destroyTimeStretchNode();
    void resetTimeStretchProcessing();

public:
    Sound(AudioService& service, const std::string& path = "");
    // パスから音声を読み込む（FileRef.Source 経由でも使用）
    void loadFromFile(const std::string& path);
    void play();
    void stop();
    void setLooping(bool loop);
    void update3D(const Vector3& listenerPos, const Vector3& listenerRight);
    static SoundSpatialMix calculateSpatialMix(const Vector3& worldPos,
                                                const Vector3& listenerPos,
                                                const Vector3& listenerRight,
                                                float baseVolume);

    void  reset();                  // 再生位置を 0:00 へ
    void  seekSeconds(float sec);   // 任意秒へシーク（[0,length] にクランプ）
    float getPlaybackTime() const;  // 現在の再生位置（秒）
    float getLength() const;        // 全長（秒）, 取得失敗時 0
    void  setSpeed(float s);
    float getSpeed() const;
    void  setPreservePitch(bool b);
    bool  getPreservePitch() const;

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

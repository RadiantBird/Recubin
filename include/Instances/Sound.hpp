#pragma once
#include "Core/AudioService.hpp"
#include "Core/AudioDiagnostics.hpp"
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
    // 直近のupdate3Dの結果（診断表示用。再生には影響しない）
    float m_lastMixVolume = 0.0f;
    float m_lastDistance = 0.0f;
    float m_lastPan = 0.0f;
    // 空間ミックスを適用中か（update3Dが毎フレーム音量を決めている間はtrue）と、そのときのリスナー。
    // Play/Volume変更が、距離減衰を無視した素のVolumeを一瞬だけ設定しないために使う。
    bool m_spatialMixActive = false;
    Vector3 m_listenerPos;
    Vector3 m_listenerRight;
    float effectiveVolume() const;

    ma_sound_group* getTargetGroup() const;
    void applyLoadedProperties();
    void updatePlaybackRouting();
    void destroyTimeStretchNode();
    void resetTimeStretchProcessing();

public:
    // 距離減衰はメートルで定義する。ワールドはstud（1 stud = 0.05 m、20 stud = 1 m）なので換算する。
    static constexpr float STUDS_PER_METER = 20.0f;
    static constexpr float ROLLOFF_PER_METER = 0.5f;   // 音量 = 基準 / (1 + 距離[m] * この値)

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
    void  setSoundGroup(const std::string& group);

    virtual void setProperty(const std::string& name, const YAML::Node& value) override;
    virtual std::string getClassName() override { return "Sound"; }
    virtual bool IsA(std::string name) override;
    virtual void onAncestorChanged() override;
    std::shared_ptr<Instance> clone() const override;

    bool autoPlay = false;

    std::string getContentPath() const { return m_currentPath; }
    // 診断パネル用の現在状態（読み取りのみ）
    AudioDiag::SoundSnapshot debugSnapshot() const;
    bool isLooping()   const { return looping;    }
    bool isPlaying()   const;
    bool getAutoPlay() const { return autoPlay;   }
    void setVolume(float v);
    float getVolume()  const;
    std::string getSoundGroup() const { return soundGroup; }

    ~Sound();
};

#pragma once
#include "Core/AudioService.hpp"
#include "Instances/Spatial.hpp"

class Sound : public Spatial {
private:
    ma_sound sound;
    bool loaded = false;

public:
    // 音にSizeなんかないけど、適当にしといて
    Sound(AudioService& service, const std::string& path, bool isBGM = false) : Spatial(Vector3(0,0,0), Vector3(1,1,1), "Sound") {
        ma_uint32 flags = MA_SOUND_FLAG_DECODE; 
        // 3D機能は使わず、Recubin側で制御するためにSPATIALIZATIONはフラグに入れない方針
        
        ma_sound_group* targetGroup = isBGM ? &service.groupBGM : &service.groupSFX;

        if (ma_sound_init_from_file(&service.engine, path.c_str(), flags, targetGroup, NULL, &sound) == MA_SUCCESS) {
            loaded = true;
        }
    }

    void play() { if (loaded) ma_sound_start(&sound); }
    void stop() { if (loaded) ma_sound_stop(&sound); }
    void setLooping(bool loop) { if (loaded) ma_sound_set_looping(&sound, loop); }

    // Recubinの座標系から計算したボリュームとパンを適用する
    void update3D(const Vector3& listenerPos, const Vector3& listenerRight, const Vector3& sourcePos) {
        if (!loaded) return;

        Vector3 relativePos = sourcePos - listenerPos;
        float dist = relativePos.length();

        // 1. 距離減衰 (Recubinオリジナルの計算)
        float atten = 1.0f / (1.0f + dist * 0.1f); // 係数は適当
        ma_sound_set_volume(&sound, atten);

        // 2. パンニング (左右の定位)
        if (dist > 0.001f) {
            float pan = Vector3::Dot(relativePos.normalize(), listenerRight);
            ma_sound_set_pan(&sound, pan);
        }
    }

    ~Sound() { if (loaded) ma_sound_uninit(&sound); }
};
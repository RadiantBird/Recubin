#include "Instances/Sound.hpp"

Sound::Sound(AudioService& service, const std::string& path, bool isBGM) 
    : Spatial(Vector3(0,0,0), Vector3(1,1,1), "Sound") {
    ma_uint32 flags = MA_SOUND_FLAG_DECODE; 
    ma_sound_group* targetGroup = isBGM ? &service.groupBGM : &service.groupSFX;

    if (ma_sound_init_from_file(&service.engine, path.c_str(), flags, targetGroup, NULL, &sound) == MA_SUCCESS) {
        loaded = true;
        std::cout << "[DEBUG] Audio loaded: " << path << std::endl;
    } else {
        std::cout << "[ERROR] Failed to load audio: " << path << std::endl;
    }
}

void Sound::play() { if (loaded) { ma_sound_start(&sound); playing = true; } }
void Sound::stop() { if (loaded) { ma_sound_stop(&sound); playing = false; } }
void Sound::setLooping(bool loop) { if (loaded) ma_sound_set_looping(&sound, loop); }

void Sound::update3D() {
    if (!loaded) return;

    // Parentがいない、またはSpatialでない場合は、3D計算をせず標準音量で鳴らす
    if (!Parent || !Parent->IsA("Spatial")) {
        ma_sound_set_volume(&sound, 1.0f);
        ma_sound_set_pan(&sound, 0.0f); // 中央
        return;
    }

    // ここからは空間配置されている場合の計算
    Spatial* ps = static_cast<Spatial*>(Parent);
    Vector3 relativePos = this->Position - ps->Position;
    float dist = relativePos.length();

    // 距離減衰
    ma_sound_set_volume(&sound, 1.0f / (1.0f + dist * 0.1f));

    // パンニング
    if (dist > 0.001f) {
        ma_sound_set_pan(&sound, Vector3::Dot(relativePos.normalize(), Vector3(1, 0, 0)));
    }
}

Sound::~Sound() { if (loaded) ma_sound_uninit(&sound); }
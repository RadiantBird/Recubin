#include "Instances/Sound.hpp"

Sound::Sound(AudioService& service, const std::string& path) 
    : Spatial(Vector3(0,0,0), Vector3(1,1,1), "Sound") {
    service.addSound(this);
    if (!path.empty()) {
        ma_uint32 flags = MA_SOUND_FLAG_DECODE; 
        ma_sound_group* targetGroup = (soundGroup == "BGM") ? &service.groupBGM : &service.groupSFX;

        if (ma_sound_init_from_file(&service.engine, path.c_str(), flags, targetGroup, NULL, &sound) == MA_SUCCESS) {
            loaded = true;
            std::cout << "[DEBUG] Audio loaded: " << path << std::endl;
        } else {
            std::cout << "[ERROR] Failed to load audio: " << path << std::endl;
        }
    }
}

void Sound::play() { if (loaded) { ma_sound_start(&sound); } }
void Sound::stop() { if (loaded) { ma_sound_stop(&sound); } }
void Sound::setLooping(bool loop) { 
    this->looping = loop;
    if (loaded) ma_sound_set_looping(&sound, loop); 
}

void Sound::setProperty(const std::string& name, const YAML::Node& value) {
    if (name == "ContentPath") {
        std::string path = value.as<std::string>();
        if (loaded) {
            ma_sound_uninit(&sound);
            loaded = false;
        }
        
        if (AudioService::instance) {
            ma_uint32 flags = MA_SOUND_FLAG_DECODE;
            ma_sound_group* targetGroup = (soundGroup == "BGM") ? &AudioService::instance->groupBGM : &AudioService::instance->groupSFX;
            if (ma_sound_init_from_file(&AudioService::instance->engine, path.c_str(), flags, targetGroup, NULL, &sound) == MA_SUCCESS) {
                loaded = true;
                m_currentPath = path;
                if (looping) ma_sound_set_looping(&sound, true);
                std::cout << "[DEBUG] Audio loaded via setProperty: " << path << std::endl;
            }
        }
    } else if (name == "Looped") {
        setLooping(value.as<bool>());
    } else if (name == "SoundGroup") {
        this->soundGroup = value.as<std::string>();
        if (loaded && AudioService::instance && !m_currentPath.empty()) {
            ma_sound_uninit(&sound);
            loaded = false;
            ma_uint32 flags = MA_SOUND_FLAG_DECODE;
            ma_sound_group* targetGroup = (soundGroup == "BGM")
                ? &AudioService::instance->groupBGM
                : &AudioService::instance->groupSFX;
            if (ma_sound_init_from_file(&AudioService::instance->engine,
                    m_currentPath.c_str(), flags, targetGroup, NULL, &sound) == MA_SUCCESS) {
                loaded = true;
                if (looping) ma_sound_set_looping(&sound, true);
            }
        }
    } else if (name == "AutoPlay") {
        autoPlay = value.as<bool>();
    } else if (name == "Playing") {
        if (value.as<bool>()) {
            play();
            std::cout << "[DEBUG] Sound play triggered via setProperty\n";
        } else {
            stop();
            std::cout << "[DEBUG] Sound stop triggered via setProperty\n";
        }
    } else {
        Spatial::setProperty(name, value);
    }
}

bool Sound::IsA(std::string name) {
    return (name == "Sound") || Spatial::IsA(name);
}

void Sound::update3D() {
// ... (keep original logic) // <-は？
    if (!loaded) return;

    // Parentがいない、またはSpatialでない場合は、3D計算をせず標準音量で鳴らす
    auto parentPtr = Parent.lock();
    if (!parentPtr || !parentPtr->IsA("Spatial")) {
        ma_sound_set_volume(&sound, 1.0f);
        ma_sound_set_pan(&sound, 0.0f); // 中央
        return;
    }

    // ここからは空間配置されている場合の計算
    Spatial* ps = static_cast<Spatial*>(parentPtr.get());
    Vector3 relativePos = this->Position - ps->Position;
    float dist = relativePos.length();

    // 距離減衰
    ma_sound_set_volume(&sound, 1.0f / (1.0f + dist * 0.1f));

    // パンニング
    if (dist > 0.001f) {
        ma_sound_set_pan(&sound, Vector3::Dot(relativePos.normalize(), Vector3(1, 0, 0)));
    }
}

Sound::~Sound() {
    if (AudioService::instance) AudioService::instance->removeSound(this);
    if (loaded) ma_sound_uninit(&sound);
}
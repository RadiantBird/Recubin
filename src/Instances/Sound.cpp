#include "Instances/Sound.hpp"

Sound::Sound(AudioService& service, const std::string& path) 
    : Spatial(Vector3(0,0,0), Vector3(1,1,1), "Sound") {
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

void Sound::update3D(const Vector3& listenerPos, const Vector3& listenerRight) {
    if (!loaded) return;

    // 親が Spatial でない場合はグローバル再生
    auto parentPtr = Parent.lock();
    if (!parentPtr || !parentPtr->IsA("Spatial")) {
        ma_sound_set_volume(&sound, 1.0f);
        ma_sound_set_pan(&sound, 0.0f);
        return;
    }

    Spatial* ps = static_cast<Spatial*>(parentPtr.get());
    // Sound のワールド位置 = 親の Position + Sound 自身の Position
    Vector3 worldPos = ps->Position + this->Position;
    Vector3 toSound  = worldPos - listenerPos;
    float dist = toSound.length();

    // 距離減衰
    ma_sound_set_volume(&sound, 1.0f / (1.0f + dist * 0.1f));

    // 左右パンニング（カメラの right ベクトルを基準に）
    if (dist > 0.001f) {
        float pan = Vector3::Dot(toSound.normalize(), listenerRight);
        ma_sound_set_pan(&sound, pan);
    } else {
        ma_sound_set_pan(&sound, 0.0f);
    }
}

Sound::~Sound() {
    // AudioService の登録解除は weak_ptr の自然な消滅に任せるか、
    // 必要なら明示的に行う（ただし shared_from_this は使えない）。
    // ここでは ma_sound の解放を確実に行う。
    if (loaded) ma_sound_uninit(&sound);
}

void Sound::onAncestorChanged() {
    if (AudioService::instance && findFirstAncestorWorkspace()) {
        AudioService::instance->addSound(std::static_pointer_cast<Sound>(shared_from_this()));
    }
    Spatial::onAncestorChanged();
}
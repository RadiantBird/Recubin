#include "Instances/Sound.hpp"
#include "Core/TimeStretchNode.hpp"
#include <Util/Logger.hpp>
#include <Util/AssetGuard.hpp>
#include <algorithm>
#include <cmath>
#ifdef _WIN32
#include <windows26.h>
#endif

namespace {
    // pathはUTF-8前提（getPlatform().openFileDialog()等の返り値）。ma_sound_init_from_file()はナローパスを
    // ANSIコードページとして扱うため、日本語等の非ASCIIパスが化ける（FileLoader.cppの
    // utf8_to_wstring()と同じ変換をここでも行い、_wファミリーに渡す）。
#ifdef _WIN32
    std::wstring utf8ToWide(const std::string& str) {
        if (str.empty()) return std::wstring();
        int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), NULL, 0);
        std::wstring wstr(sizeNeeded, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), &wstr[0], sizeNeeded);
        return wstr;
    }
#endif
    ma_result initSoundFromFile(ma_engine* engine, const std::string& path, ma_uint32 flags,
                                 ma_sound_group* group, ma_sound* sound) {
#ifdef _WIN32
        return ma_sound_init_from_file_w(engine, utf8ToWide(path).c_str(), flags, group, NULL, sound);
#else
        return ma_sound_init_from_file(engine, path.c_str(), flags, group, NULL, sound);
#endif
    }
}

Sound::Sound(AudioService& service, const std::string& path)
    : Spatial(Vector3(0,0,0), Vector3(1,1,1), "Sound"),
      m_audioService(&service) {
    if (!path.empty()) loadFromFile(path);
}

void Sound::play() {
    if (!loaded) return;
    ma_sound_set_volume(&sound, m_volume);
    ma_sound_start(&sound);
}
void Sound::stop() { if (loaded) { ma_sound_stop(&sound); } }
bool Sound::isPlaying() const { return loaded && ma_sound_is_playing(const_cast<ma_sound*>(&sound)); }
void Sound::setVolume(float v) {
    m_volume = std::isfinite(v) ? std::clamp(v, 0.0f, 8.0f) : 1.0f;
    if (loaded) ma_sound_set_volume(&sound, m_volume);
}
float Sound::getVolume() const { return m_volume; }
void Sound::setLooping(bool loop) {
    this->looping = loop;
    if (loaded) ma_sound_set_looping(&sound, loop);
}

void Sound::reset() {
    if (loaded) {
        const bool rebuildTimeStretch = m_timeStretchNode != nullptr;
        if (rebuildTimeStretch) destroyTimeStretchNode();
        ma_sound_seek_to_pcm_frame(&sound, 0);
        if (rebuildTimeStretch) updatePlaybackRouting();
    }
}
void Sound::seekSeconds(float sec) {
    if (!loaded) return;
    if (sec < 0.0f) sec = 0.0f;
    float len = getLength();
    if (len > 0.0f && sec > len) sec = len;
    const bool rebuildTimeStretch = m_timeStretchNode != nullptr;
    if (rebuildTimeStretch) destroyTimeStretchNode();
    ma_sound_seek_to_second(&sound, sec);
    if (rebuildTimeStretch) updatePlaybackRouting();
}
float Sound::getPlaybackTime() const {
    if (!loaded) return 0.0f;
    float cur = 0.0f;
    if (ma_sound_get_cursor_in_seconds(const_cast<ma_sound*>(&sound), &cur) != MA_SUCCESS) return 0.0f;
    return cur;
}
float Sound::getLength() const {
    if (!loaded) return 0.0f;
    float len = 0.0f;
    if (ma_sound_get_length_in_seconds(const_cast<ma_sound*>(&sound), &len) != MA_SUCCESS) return 0.0f;
    return len;
}
void Sound::setSpeed(float s) {
    m_speed = TimeStretchNode::clampSpeed(s);
    updatePlaybackRouting();
}
float Sound::getSpeed() const { return m_speed; }
void Sound::setPreservePitch(bool b) {
    if (m_preservePitch == b) return;
    m_preservePitch = b;
    updatePlaybackRouting();
}
bool Sound::getPreservePitch() const { return m_preservePitch; }

void Sound::loadFromFile(const std::string& path) {
    if (!AssetGuard::allow(path)) return;
    if (loaded) {
        destroyTimeStretchNode();
        ma_sound_uninit(&sound);
        loaded = false;
    }
    if (m_audioService) {
        ma_uint32 flags = MA_SOUND_FLAG_DECODE;
        if (initSoundFromFile(&m_audioService->engine, path, flags,
                              getTargetGroup(), &sound) == MA_SUCCESS) {
            loaded = true;
            m_currentPath = path;
            applyLoadedProperties();
            std::cout << "[DEBUG] Audio loaded: " << path << std::endl;
        } else {
            RCBN_WARN("Failed to load audio: " << path);
        }
    }
}

void Sound::setProperty(const std::string& name, const YAML::Node& value) {
    if (name == "ContentPath") {
        loadFromFile(value.as<std::string>());
    } else if (name == "Looped") {
        setLooping(value.as<bool>());
    } else if (name == "SoundGroup") {
        this->soundGroup = value.as<std::string>();
        if (m_timeStretchNode) {
            resetTimeStretchProcessing();
        } else {
            updatePlaybackRouting();
        }
    } else if (name == "AutoPlay") {
        autoPlay = value.as<bool>();
    } else if (name == "Volume") {
        setVolume(value.as<float>());
    } else if (name == "Speed") {
        setSpeed(value.as<float>());
    } else if (name == "PreservePitch") {
        setPreservePitch(value.as<bool>());
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
        ma_sound_set_volume(&sound, m_volume);
        ma_sound_set_pan(&sound, 0.0f);
        return;
    }

    Spatial* ps = static_cast<Spatial*>(parentPtr.get());
    // Sound のワールド位置 = 親の Position + Sound 自身の Position
    Vector3 worldPos = ps->Position + this->Position;
    Vector3 toSound  = worldPos - listenerPos;
    float dist = toSound.length();

    // 距離減衰 (ユーザー設定音量に乗算)
    ma_sound_set_volume(&sound, m_volume / (1.0f + dist * 0.1f));

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
    if (loaded) {
        destroyTimeStretchNode();
        ma_sound_uninit(&sound);
    }
}

ma_sound_group* Sound::getTargetGroup() const {
    if (!m_audioService) return nullptr;
    return (soundGroup == "BGM")
        ? &m_audioService->groupBGM
        : &m_audioService->groupSFX;
}

void Sound::applyLoadedProperties() {
    if (!loaded) return;
    ma_sound_set_volume(&sound, m_volume);
    ma_sound_set_looping(&sound, looping);
    updatePlaybackRouting();
}

void Sound::updatePlaybackRouting() {
    if (!loaded) return;

    ma_sound_group* targetGroup = getTargetGroup();
    if (!targetGroup) return;

    if (!m_preservePitch || m_speed == 1.0f) {
        destroyTimeStretchNode();
        ma_node_attach_output_bus(&sound, 0, targetGroup, 0);
        ma_sound_set_pitch(&sound, m_speed);
        return;
    }

    ma_sound_set_pitch(&sound, 1.0f);
    if (m_timeStretchNode) {
        m_timeStretchNode->setSpeed(m_speed);
        ma_node_attach_output_bus(m_timeStretchNode->node(), 0, targetGroup, 0);
        return;
    }

    auto timeStretchNode = std::make_unique<TimeStretchNode>();
    const ma_uint32 channels = ma_node_get_output_channels(&sound, 0);
    const ma_uint32 sampleRate = ma_engine_get_sample_rate(&m_audioService->engine);
    if (!timeStretchNode->initialize(
            ma_engine_get_node_graph(&m_audioService->engine),
            channels, sampleRate, m_speed) ||
        ma_node_attach_output_bus(timeStretchNode->node(), 0, targetGroup, 0) != MA_SUCCESS ||
        ma_node_attach_output_bus(&sound, 0, timeStretchNode->node(), 0) != MA_SUCCESS) {
        RCBN_WARN("Failed to initialize PreservePitch time stretching; falling back to pitch-linked playback");
        ma_node_attach_output_bus(&sound, 0, targetGroup, 0);
        m_preservePitch = false;
        ma_sound_set_pitch(&sound, m_speed);
        return;
    }
    m_timeStretchNode = std::move(timeStretchNode);
}

void Sound::destroyTimeStretchNode() {
    if (!m_timeStretchNode) return;
    if (loaded) {
        if (ma_sound_group* targetGroup = getTargetGroup()) {
            ma_node_attach_output_bus(&sound, 0, targetGroup, 0);
        } else {
            ma_node_detach_output_bus(&sound, 0);
        }
    }
    m_timeStretchNode.reset();
}

void Sound::resetTimeStretchProcessing() {
    if (!m_timeStretchNode) return;
    destroyTimeStretchNode();
    updatePlaybackRouting();
}

void Sound::onAncestorChanged() {
    if (AudioService::instance && findFirstAncestorWorkspace()) {
        AudioService::instance->addSound(std::static_pointer_cast<Sound>(shared_from_this()));
    }
    else {
        stop(); // 削除されたのと同じなので、再生を停止する
    }
    Spatial::onAncestorChanged();
}

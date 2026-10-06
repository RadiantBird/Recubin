#include "Instances/Sound.hpp"
#include "Core/TimeStretchNode.hpp"
#include <include/Core/PropertyRegistry.hpp>
#include <Util/Logger.hpp>
#include <Util/AssetGuard.hpp>
#include <Util/AssetPath.hpp>
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

// Play/Stop/Reset/Seekの前後の状態を診断ログへ残す（診断が無効なら何もしない）。
class SoundCallTrace {
public:
    SoundCallTrace(const Sound& sound, const char* call)
        : m_sound(sound), m_active(AudioDiagnostics::get().enabled()) {
        if (!m_active) return;
        const AudioDiag::SoundSnapshot before = sound.debugSnapshot();
        m_event.time = AudioDiagnostics::now();
        m_event.sound = sound.getFullPath();
        m_event.call = call;
        m_event.playingBefore = before.playing;
        m_event.atEndBefore = before.atEnd;
        m_event.cursorBefore = before.cursor;
    }
    ~SoundCallTrace() {
        if (!m_active) return;
        const AudioDiag::SoundSnapshot after = m_sound.debugSnapshot();
        m_event.playingAfter = after.playing;
        m_event.cursorAfter = after.cursor;
        m_event.appliedVolume = after.appliedVolume;
        AudioDiagnostics::get().recordEvent(std::move(m_event));
    }
private:
    const Sound& m_sound;
    bool m_active;
    AudioDiag::SoundEvent m_event;
};

static const bool s_soundRegistered = [] {
    using namespace PropertyRegistry;
    registerClass("Sound", "Spatial", {
        custom("ContentPath", PropType::String,
            [](Instance* instance) {
                return PropValue(static_cast<Sound*>(instance)->getContentPath());
            },
            [](Instance* instance, const PropValue& value) {
                static_cast<Sound*>(instance)->loadFromFile(std::get<std::string>(value));
            }).filePath(AUDIO_EXTENSION).luaReadOnly(),
        field<&Sound::autoPlay>("AutoPlay"),
        method_prop<&Sound::isLooping, &Sound::setLooping>("Looped"),
        method_prop<&Sound::getSoundGroup, &Sound::setSoundGroup>("SoundGroup").luaReadOnly(),
        method_prop<&Sound::getVolume, &Sound::setVolume>("Volume", 0.0f, 8.0f, 0.01f),
        method_prop<&Sound::getSpeed, &Sound::setSpeed>("Speed", 0.25f, 4.0f, 0.01f),
        method_prop<&Sound::getPreservePitch, &Sound::setPreservePitch>("PreservePitch"),
    });
    return true;
}();

Sound::Sound(AudioService& service, const std::string& path)
    : Spatial(Vector3(0,0,0), Vector3(1,1,1), "Sound"),
      m_audioService(&service) {
    if (!path.empty()) loadFromFile(path);
}

void Sound::play() {
    SoundCallTrace trace(*this, "Play");
    if (!loaded) return;
    // 距離減衰中は、減衰後の音量で鳴らし始める。素のVolumeを設定すると、次のupdate3Dまでの
    // 1フレームだけ全音量で鳴り、遠いSoundの連射が「ぷっ」というノイズになる。
    ma_sound_set_volume(&sound, effectiveVolume());
    ma_sound_start(&sound);
}
void Sound::stop() {
    SoundCallTrace trace(*this, "Stop");
    if (loaded) { ma_sound_stop(&sound); }
}
bool Sound::isPlaying() const { return loaded && ma_sound_is_playing(const_cast<ma_sound*>(&sound)); }
void Sound::setVolume(float v) {
    m_volume = std::isfinite(v) ? std::clamp(v, 0.0f, 8.0f) : 1.0f;
    if (loaded) ma_sound_set_volume(&sound, effectiveVolume());
}
float Sound::getVolume() const { return m_volume; }

float Sound::effectiveVolume() const {
    if (!m_spatialMixActive) return m_volume;
    return calculateSpatialMix(getWorldCFrame().Position, m_listenerPos, m_listenerRight, m_volume).volume;
}
void Sound::setLooping(bool loop) {
    this->looping = loop;
    if (loaded) ma_sound_set_looping(&sound, loop);
}

void Sound::reset() {
    SoundCallTrace trace(*this, "Reset");
    if (loaded) {
        const bool rebuildTimeStretch = m_timeStretchNode != nullptr;
        if (rebuildTimeStretch) destroyTimeStretchNode();
        ma_sound_seek_to_pcm_frame(&sound, 0);
        if (rebuildTimeStretch) updatePlaybackRouting();
    }
}
void Sound::seekSeconds(float sec) {
    SoundCallTrace trace(*this, "Seek");
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

void Sound::setSoundGroup(const std::string& group) {
    soundGroup = group;
    if (m_timeStretchNode) {
        resetTimeStretchProcessing();
    } else {
        updatePlaybackRouting();
    }
}

void Sound::loadFromFile(const std::string& path) {
    if (path.empty()) {
        if (loaded) {
            destroyTimeStretchNode();
            ma_sound_uninit(&sound);
            loaded = false;
        }
        m_currentPath.clear();
        return;
    }
    if (!AssetGuard::allow(path)) return;
    const std::string normalizedPath = AssetPath::normalize(path);
    if (loaded) {
        destroyTimeStretchNode();
        ma_sound_uninit(&sound);
        loaded = false;
    }
    m_currentPath.clear();
    if (m_audioService) {
        ma_uint32 flags = MA_SOUND_FLAG_DECODE;
        if (initSoundFromFile(&m_audioService->engine, normalizedPath, flags,
                              getTargetGroup(), &sound) == MA_SUCCESS) {
            loaded = true;
            m_currentPath = normalizedPath;
            applyLoadedProperties();
            std::cout << "[DEBUG] Audio loaded: " << normalizedPath << std::endl;
        } else {
            RCBN_WARN("Failed to load audio: " << path);
        }
    }
}

void Sound::setProperty(const std::string& name, const YAML::Node& value) {
    if (PropertyRegistry::loadProperty(this, "Sound", name, value)) return;
    if (name == "Playing") {
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

std::shared_ptr<Instance> Sound::clone() const {
    // 音声エンジンへの参照を引き継ぎ、ContentPath(再読み込み)・音量等はスキーマで複製する。
    auto copy = std::make_shared<Sound>(*m_audioService);
    copy->Name = Name;
    copy->setCFrame(getCFrame());
    PropertyRegistry::cloneFields(this, copy.get(), "Sound");  // 基底 Spatial 分も集約
    for (auto const& [n, child] : children)
        copy->addChild(child->clone());
    return copy;
}

SoundSpatialMix Sound::calculateSpatialMix(const Vector3& worldPos,
                                           const Vector3& listenerPos,
                                           const Vector3& listenerRight,
                                           float baseVolume) {
    const Vector3 toSound = worldPos - listenerPos;
    const float dist = toSound.length();
    SoundSpatialMix mix;
    // distはstud。減衰の係数はメートル基準なので換算する（換算しないと20倍速く減衰する）。
    const float distanceMeters = dist / STUDS_PER_METER;
    mix.volume = baseVolume / (1.0f + distanceMeters * ROLLOFF_PER_METER);
    if (dist > 0.001f) {
        mix.pan = Vector3::Dot(toSound.normalize(), listenerRight);
    }
    return mix;
}

void Sound::update3D(const Vector3& listenerPos, const Vector3& listenerRight) {
    if (!loaded) return;

    // 親が Spatial でない場合はグローバル再生
    auto parentPtr = Parent.lock();
    if (!parentPtr || !parentPtr->IsA("Spatial") || AudioDiagnostics::get().settings().bypassSpatial) {
        ma_sound_set_volume(&sound, m_volume);
        ma_sound_set_pan(&sound, 0.0f);
        m_lastMixVolume = m_volume;
        m_lastDistance = 0.0f;
        m_lastPan = 0.0f;
        m_spatialMixActive = false;
        return;
    }
    m_spatialMixActive = true;
    m_listenerPos = listenerPos;
    m_listenerRight = listenerRight;

    // 親の回転・多段階の親子関係を含む完全なワールド変換から位置を求める。
    // Sound 自身が Spatial であるため、親が Spatial なら自身のローカル CFrame も含まれる。
    const SoundSpatialMix mix = calculateSpatialMix(
        getWorldCFrame().Position, listenerPos, listenerRight, m_volume);
    ma_sound_set_volume(&sound, mix.volume);
    ma_sound_set_pan(&sound, mix.pan);
    m_lastMixVolume = mix.volume;
    m_lastDistance = (getWorldCFrame().Position - listenerPos).length();
    m_lastPan = mix.pan;
}

AudioDiag::SoundSnapshot Sound::debugSnapshot() const {
    AudioDiag::SoundSnapshot snapshot;
    snapshot.name = Name;
    snapshot.path = m_currentPath;
    snapshot.group = soundGroup;
    snapshot.loaded = loaded;
    snapshot.looping = looping;
    snapshot.preservePitch = m_preservePitch;
    snapshot.propertyVolume = m_volume;
    snapshot.speed = m_speed;
    snapshot.mixVolume = m_lastMixVolume;
    snapshot.distance = m_lastDistance;
    snapshot.pan = m_lastPan;
    snapshot.spatialMixActive = m_spatialMixActive;
    {
        const Vector3 world = getWorldCFrame().Position;
        snapshot.worldPosition[0] = world.x; snapshot.worldPosition[1] = world.y; snapshot.worldPosition[2] = world.z;
        snapshot.listenerPosition[0] = m_listenerPos.x; snapshot.listenerPosition[1] = m_listenerPos.y;
        snapshot.listenerPosition[2] = m_listenerPos.z;
        // 祖先のSpatialのワールド位置（ツールが手に付いているか等の確認用）
        char line[160];
        int depth = 0;
        for (auto parent = Parent.lock(); parent && depth < 8; parent = parent->Parent.lock()) {
            const auto* spatial = dynamic_cast<const Spatial*>(parent.get());
            if (!spatial) continue;
            const Vector3 p = spatial->getWorldCFrame().Position;
            std::snprintf(line, sizeof(line), "%s (%.1f, %.1f, %.1f)", parent->Name.c_str(), p.x, p.y, p.z);
            snapshot.ancestry.push_back(line);
            ++depth;
        }
    }
    if (!loaded) return snapshot;
    ma_sound* raw = const_cast<ma_sound*>(&sound);
    snapshot.playing = ma_sound_is_playing(raw);
    snapshot.atEnd = ma_sound_at_end(raw);
    snapshot.cursor = getPlaybackTime();
    snapshot.length = getLength();
    snapshot.appliedVolume = ma_sound_get_volume(raw);
    snapshot.pan = ma_sound_get_pan(raw);
    snapshot.pitch = ma_sound_get_pitch(raw);
    return snapshot;
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
    if (findFirstAncestorWorkspace()) {
        if (AudioService::instance == m_audioService)
            m_audioService->addSound(std::static_pointer_cast<Sound>(shared_from_this()));
    }
    else {
        stop(); // 削除されたのと同じなので、再生を停止する
        m_spatialMixActive = false;
        if (AudioService::instance == m_audioService)
            m_audioService->removeSound(std::static_pointer_cast<Sound>(shared_from_this()));
    }
    Spatial::onAncestorChanged();
}

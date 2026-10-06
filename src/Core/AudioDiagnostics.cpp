#include "Core/AudioDiagnostics.hpp"
#include "Instances/Sound.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <sstream>

#ifdef _WIN32
    #include <windows26.h>
    #undef getClassName
#endif

namespace {
constexpr float CLIP_LEVEL = 0.9999f;
constexpr float SILENCE_PEAK = 1.0e-6f;
constexpr float LEADING_SILENCE_LEVEL = 0.01f;

std::string formatFixed(double value, int digits) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", digits, value);
    return buffer;
}

double percentile(std::vector<double> values, double fraction) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const std::size_t index = std::min(values.size() - 1,
        static_cast<std::size_t>(fraction * static_cast<double>(values.size() - 1) + 0.5));
    return values[index];
}

#ifdef _WIN32
std::wstring utf8ToWide(const std::string& text) {
    if (text.empty()) return std::wstring();
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(size), 0);
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}
#endif
} // namespace

// ---- 出力タップ（パススルーノード） ----
struct AudioDiagnostics::Tap {
    ma_node_base base{};
    AudioDiagnostics* owner = nullptr;
    std::uint32_t channels = 2;
    float last[2] = {0.0f, 0.0f};
    ma_sound_group* sfx = nullptr;
    ma_sound_group* bgm = nullptr;
    ma_node* endpoint = nullptr;

    static void process(ma_node* node, const float** inputs, ma_uint32* inputCount,
                        float** outputs, ma_uint32* outputCount) {
        auto* self = reinterpret_cast<Tap*>(node);
        const ma_uint32 frames = std::min(*inputCount, *outputCount);
        *inputCount = frames;
        *outputCount = frames;
        if (frames == 0) return;

        const std::uint32_t channels = self->channels;
        const float* in = inputs[0];
        float* out = outputs[0];
        if (in == nullptr || out == nullptr) return;

        AudioDiag::BlockStat stat;
        stat.time = AudioDiagnostics::now();
        stat.frames = frames;
        double sumSquares = 0.0;
        for (ma_uint32 frame = 0; frame < frames; ++frame) {
            for (std::uint32_t channel = 0; channel < channels; ++channel) {
                const float sample = in[frame * channels + channel];
                out[frame * channels + channel] = sample;
                if (!std::isfinite(sample)) { ++stat.nonFinite; continue; }
                const float magnitude = std::fabs(sample);
                const std::size_t slot = channel < 2 ? channel : 1;
                stat.peak[slot] = std::max(stat.peak[slot], magnitude);
                if (magnitude >= CLIP_LEVEL) ++stat.clipped;
                sumSquares += static_cast<double>(sample) * static_cast<double>(sample);
                if (channel < 2) {
                    const float previous = frame == 0 ? self->last[channel] : in[(frame - 1) * channels + channel];
                    if (std::isfinite(previous))
                        stat.maxDelta = std::max(stat.maxDelta, std::fabs(sample - previous));
                }
            }
        }
        if (channels == 1) stat.peak[1] = stat.peak[0];
        stat.rms = static_cast<float>(std::sqrt(sumSquares / static_cast<double>(frames * channels)));
        for (std::uint32_t channel = 0; channel < 2 && channel < channels; ++channel)
            self->last[channel] = in[(frames - 1) * channels + channel];

        AudioDiagnostics& owner = *self->owner;
        const std::uint64_t head = owner.m_head.load(std::memory_order_relaxed);
        owner.m_ring[head % RING_SIZE] = stat;
        owner.m_head.store(head + 1, std::memory_order_release);
    }
};

AudioDiagnostics::AudioDiagnostics() = default;
AudioDiagnostics::~AudioDiagnostics() = default;

AudioDiagnostics& AudioDiagnostics::get() {
    static AudioDiagnostics instance;
    return instance;
}

double AudioDiagnostics::now() {
    using Clock = std::chrono::steady_clock;
    static const Clock::time_point start = Clock::now();
    return std::chrono::duration<double>(Clock::now() - start).count();
}

void AudioDiagnostics::installTap(ma_engine& engine, ma_sound_group& sfx, ma_sound_group& bgm) {
    if (m_tap) return;
    static ma_node_vtable vtable = {Tap::process, nullptr, 1, 1, 0};

    auto tap = std::make_unique<Tap>();
    tap->owner = this;
    tap->channels = std::max<ma_uint32>(1, ma_engine_get_channels(&engine));
    ma_uint32 channels = tap->channels;
    ma_node_config config = ma_node_config_init();
    config.vtable = &vtable;
    config.pInputChannels = &channels;
    config.pOutputChannels = &channels;
    if (ma_node_init(ma_engine_get_node_graph(&engine), &config, nullptr, &tap->base) != MA_SUCCESS)
        return;

    tap->endpoint = ma_engine_get_endpoint(&engine);
    tap->sfx = &sfx;
    tap->bgm = &bgm;
    if (ma_node_attach_output_bus(&tap->base, 0, tap->endpoint, 0) != MA_SUCCESS ||
        ma_node_attach_output_bus(&sfx, 0, &tap->base, 0) != MA_SUCCESS ||
        ma_node_attach_output_bus(&bgm, 0, &tap->base, 0) != MA_SUCCESS) {
        ma_node_attach_output_bus(&sfx, 0, tap->endpoint, 0);
        ma_node_attach_output_bus(&bgm, 0, tap->endpoint, 0);
        ma_node_uninit(&tap->base, nullptr);
        return;
    }
    m_engine = &engine;
    m_tap = std::move(tap);
}

void AudioDiagnostics::removeTap() {
    if (!m_tap) return;
    // グループをエンジン出力へ戻してからタップを外す。
    ma_node_attach_output_bus(m_tap->sfx, 0, m_tap->endpoint, 0);
    ma_node_attach_output_bus(m_tap->bgm, 0, m_tap->endpoint, 0);
    ma_node_uninit(&m_tap->base, nullptr);
    m_tap.reset();
    m_engine = nullptr;
}

AudioDiag::DeviceInfo AudioDiagnostics::deviceInfo() const {
    AudioDiag::DeviceInfo info;
    if (!m_engine) return info;
    info.engineReady = true;
    info.sampleRate = ma_engine_get_sample_rate(m_engine);
    info.channels = ma_engine_get_channels(m_engine);
    if (ma_device* device = ma_engine_get_device(m_engine)) {
        info.hasDevice = true;
        info.deviceName = device->playback.name;
        info.backend = ma_get_backend_name(device->pContext->backend);
        info.periodFrames = device->playback.internalPeriodSizeInFrames;
        info.periods = device->playback.internalPeriods;
        info.sampleRate = device->sampleRate;
    }
    return info;
}

double AudioDiagnostics::expectedBlockInterval() const {
    const AudioDiag::DeviceInfo info = deviceInfo();
    if (info.sampleRate == 0 || info.periodFrames == 0) return 0.0;
    return static_cast<double>(info.periodFrames) / static_cast<double>(info.sampleRate);
}

void AudioDiagnostics::account(const AudioDiag::BlockStat& block) {
    ++m_stats.blocks;
    if (block.nonFinite > 0) ++m_stats.nonFiniteBlocks;
    if (block.clipped > 0) ++m_stats.clippedBlocks;
    const float peak = std::max(block.peak[0], block.peak[1]);
    if (peak < SILENCE_PEAK) ++m_stats.silentBlocks;
    m_stats.maxPeak = std::max(m_stats.maxPeak, peak);
    m_stats.maxDelta = std::max(m_stats.maxDelta, block.maxDelta);
    if (m_lastBlockTime > 0.0) {
        const double interval = block.time - m_lastBlockTime;
        m_stats.maxInterval = std::max(m_stats.maxInterval, interval);
        const double expected = static_cast<double>(block.frames) / std::max(1u, deviceInfo().sampleRate);
        if (expected > 0.0 && interval > expected * 1.5 + 0.002) ++m_stats.lateCallbacks;
    }
    m_lastBlockTime = block.time;
}

void AudioDiagnostics::drainRing() {
    const std::uint64_t head = m_head.load(std::memory_order_acquire);
    if (head - m_tail > RING_SIZE) m_tail = head - RING_SIZE;  // 読み切れなかった分は捨てる
    for (; m_tail < head; ++m_tail) {
        const AudioDiag::BlockStat block = m_ring[m_tail % RING_SIZE];
        account(block);
        m_blocks.push_back(block);
    }
    if (m_blocks.size() > MAX_UI_BLOCKS)
        m_blocks.erase(m_blocks.begin(), m_blocks.begin() + static_cast<std::ptrdiff_t>(m_blocks.size() - MAX_UI_BLOCKS * 3 / 4));
}

void AudioDiagnostics::pump(const std::vector<std::shared_ptr<Sound>>& sounds) {
    if (!enabled()) return;
    drainRing();
    const double time = now();
    for (const auto& sound : sounds) {
        if (!sound) continue;
        AudioDiag::SoundSnapshot snapshot = sound->debugSnapshot();
        const std::string key = sound->getFullPath();
        snapshot.registered = true;
        AudioDiag::SoundSample sample;
        sample.time = time;
        sample.cursor = snapshot.cursor;
        sample.appliedVolume = snapshot.appliedVolume;
        sample.mixVolume = snapshot.mixVolume;
        sample.distance = snapshot.distance;
        sample.pan = snapshot.pan;
        sample.playing = snapshot.playing;
        sample.atEnd = snapshot.atEnd;
        auto& series = m_series[key];
        series.push_back(sample);
        if (series.size() > MAX_SERIES)
            series.erase(series.begin(), series.begin() + static_cast<std::ptrdiff_t>(series.size() - MAX_SERIES * 3 / 4));
        m_snapshots[key] = std::move(snapshot);
    }
}

void AudioDiagnostics::clear() {
    m_tail = m_head.load(std::memory_order_acquire);
    m_blocks.clear();
    m_stats = {};
    m_events.clear();
    m_series.clear();
    m_snapshots.clear();
    m_lastBlockTime = 0.0;
}

void AudioDiagnostics::recordEvent(AudioDiag::SoundEvent event) {
    if (!enabled()) return;
    m_events.push_back(std::move(event));
    if (m_events.size() > MAX_EVENTS)
        m_events.erase(m_events.begin(), m_events.begin() + static_cast<std::ptrdiff_t>(m_events.size() - MAX_EVENTS * 3 / 4));
}

// ---- 波形解析 ----
AudioDiag::WaveformAnalysis AudioDiagnostics::analyzeWaveform(const std::string& path) {
    AudioDiag::WaveformAnalysis result;
    result.path = path;
    ma_decoder decoder;
    const ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
#ifdef _WIN32
    const ma_result opened = ma_decoder_init_file_w(utf8ToWide(path).c_str(), &config, &decoder);
#else
    const ma_result opened = ma_decoder_init_file(path.c_str(), &config, &decoder);
#endif
    if (opened != MA_SUCCESS) {
        result.error = "failed to open (ma_result " + std::to_string(static_cast<int>(opened)) + ")";
        return result;
    }
    result.sampleRate = decoder.outputSampleRate;
    result.channels = std::max<ma_uint32>(1, decoder.outputChannels);

    std::vector<float> chunk(4096 * result.channels);
    for (;;) {
        ma_uint64 framesRead = 0;
        const ma_result status = ma_decoder_read_pcm_frames(&decoder, chunk.data(), 4096, &framesRead);
        for (ma_uint64 frame = 0; frame < framesRead; ++frame) {
            float sum = 0.0f;
            for (std::uint32_t channel = 0; channel < result.channels; ++channel)
                sum += chunk[frame * result.channels + channel];
            result.mono.push_back(sum / static_cast<float>(result.channels));
        }
        if (status != MA_SUCCESS || framesRead == 0) break;
    }
    ma_decoder_uninit(&decoder);
    if (result.mono.empty()) {
        result.error = "decoded no samples";
        return result;
    }

    double sum = 0.0, sumSquares = 0.0;
    for (const float sample : result.mono) {
        result.peak = std::max(result.peak, std::fabs(sample));
        sum += sample;
        sumSquares += static_cast<double>(sample) * static_cast<double>(sample);
    }
    const double count = static_cast<double>(result.mono.size());
    result.duration = count / static_cast<double>(result.sampleRate);
    result.dcOffset = static_cast<float>(sum / count);
    result.rms = static_cast<float>(std::sqrt(sumSquares / count));
    result.firstSample = result.mono.front();
    result.lastSample = result.mono.back();
    std::size_t silent = 0;
    while (silent < result.mono.size() && std::fabs(result.mono[silent]) < LEADING_SILENCE_LEVEL) ++silent;
    result.leadingSilenceMs = static_cast<float>(1000.0 * static_cast<double>(silent) / result.sampleRate);

    constexpr std::size_t BUCKETS = 600;
    result.envelopeMin.assign(BUCKETS, 0.0f);
    result.envelopeMax.assign(BUCKETS, 0.0f);
    for (std::size_t bucket = 0; bucket < BUCKETS; ++bucket) {
        const std::size_t begin = bucket * result.mono.size() / BUCKETS;
        const std::size_t end = std::max(begin + 1, (bucket + 1) * result.mono.size() / BUCKETS);
        float low = 1.0e9f, high = -1.0e9f;
        for (std::size_t i = begin; i < end && i < result.mono.size(); ++i) {
            low = std::min(low, result.mono[i]);
            high = std::max(high, result.mono[i]);
        }
        result.envelopeMin[bucket] = low;
        result.envelopeMax[bucket] = high;
    }
    result.ok = true;
    return result;
}

std::vector<AudioDiag::CutPoint> AudioDiagnostics::analyzeCuts(const AudioDiag::WaveformAnalysis& analysis,
                                                               double intervalSeconds) {
    std::vector<AudioDiag::CutPoint> cuts;
    if (!analysis.ok || intervalSeconds <= 0.0) return cuts;
    for (double time = intervalSeconds; time < analysis.duration; time += intervalSeconds) {
        const std::size_t index = std::min(analysis.mono.size() - 1,
            static_cast<std::size_t>(time * analysis.sampleRate));
        cuts.push_back({time, analysis.mono[index]});
    }
    return cuts;
}

// ---- レポート ----
std::string AudioDiagnostics::buildReport() const {
    std::ostringstream out;
    const AudioDiag::DeviceInfo device = deviceInfo();
    out << "# Audio debug report\n\n";
    out << "## Engine\n";
    out << "- engine ready: " << (device.engineReady ? "yes" : "NO") << "\n";
    out << "- device: " << (device.hasDevice ? device.deviceName : std::string("(none)"))
        << " / backend " << device.backend << "\n";
    out << "- sample rate " << device.sampleRate << " Hz, channels " << device.channels
        << ", period " << device.periodFrames << " frames x " << device.periods << "\n";
    out << "- expected callback interval: " << formatFixed(expectedBlockInterval() * 1000.0, 2) << " ms\n";
    out << "- spatial bypass: " << (m_settings.bypassSpatial ? "ON" : "off") << "\n\n";

    out << "## Output (tap)\n";
    out << "- blocks " << m_stats.blocks << ", silent " << m_stats.silentBlocks
        << ", late callbacks " << m_stats.lateCallbacks << ", max interval "
        << formatFixed(m_stats.maxInterval * 1000.0, 2) << " ms\n";
    out << "- max peak " << formatFixed(m_stats.maxPeak, 4) << ", clipped blocks " << m_stats.clippedBlocks
        << ", non-finite blocks " << m_stats.nonFiniteBlocks << "\n";
    out << "- max sample-to-sample delta " << formatFixed(m_stats.maxDelta, 4)
        << " (large values mean clicks/discontinuities)\n";
    std::vector<double> intervals, deltas, peaks;
    for (std::size_t i = 0; i < m_blocks.size(); ++i) {
        if (i > 0) intervals.push_back((m_blocks[i].time - m_blocks[i - 1].time) * 1000.0);
        deltas.push_back(m_blocks[i].maxDelta);
        peaks.push_back(std::max(m_blocks[i].peak[0], m_blocks[i].peak[1]));
    }
    out << "- interval ms p50/p99/max: " << formatFixed(percentile(intervals, 0.5), 2) << " / "
        << formatFixed(percentile(intervals, 0.99), 2) << " / " << formatFixed(percentile(intervals, 1.0), 2) << "\n";
    out << "- block peak p50/p99/max: " << formatFixed(percentile(peaks, 0.5), 4) << " / "
        << formatFixed(percentile(peaks, 0.99), 4) << " / " << formatFixed(percentile(peaks, 1.0), 4) << "\n";
    out << "- block max-delta p50/p99/max: " << formatFixed(percentile(deltas, 0.5), 4) << " / "
        << formatFixed(percentile(deltas, 0.99), 4) << " / " << formatFixed(percentile(deltas, 1.0), 4) << "\n\n";

    out << "## Sounds\n";
    for (const auto& [key, snapshot] : m_snapshots) {
        out << "### " << key << "\n";
        out << "- path `" << snapshot.path << "`, group " << snapshot.group << ", loaded "
            << (snapshot.loaded ? "yes" : "NO") << ", length " << formatFixed(snapshot.length, 3) << " s\n";
        out << "- Volume " << formatFixed(snapshot.propertyVolume, 3) << ", applied " << formatFixed(snapshot.appliedVolume, 4)
            << ", mix " << formatFixed(snapshot.mixVolume, 4) << ", distance " << formatFixed(snapshot.distance, 2)
            << ", pan " << formatFixed(snapshot.pan, 3) << ", pitch " << formatFixed(snapshot.pitch, 3)
            << ", speed " << formatFixed(snapshot.speed, 3) << ", looping " << (snapshot.looping ? "yes" : "no")
            << ", preservePitch " << (snapshot.preservePitch ? "yes" : "no") << "\n";
        out << "- world (" << formatFixed(snapshot.worldPosition[0], 1) << ", " << formatFixed(snapshot.worldPosition[1], 1)
            << ", " << formatFixed(snapshot.worldPosition[2], 1) << "), listener (" << formatFixed(snapshot.listenerPosition[0], 1)
            << ", " << formatFixed(snapshot.listenerPosition[1], 1) << ", " << formatFixed(snapshot.listenerPosition[2], 1)
            << "), spatial mix " << (snapshot.spatialMixActive ? "active" : "off") << "\n";
        for (const std::string& ancestor : snapshot.ancestry) out << "  - ancestor " << ancestor << "\n";
        const auto series = m_series.find(key);
        if (series != m_series.end() && !series->second.empty()) {
            float minVolume = 1.0e9f, maxVolume = 0.0f, minDistance = 1.0e9f, maxDistance = 0.0f;
            std::size_t playingFrames = 0;
            for (const auto& sample : series->second) {
                minVolume = std::min(minVolume, sample.appliedVolume);
                maxVolume = std::max(maxVolume, sample.appliedVolume);
                minDistance = std::min(minDistance, sample.distance);
                maxDistance = std::max(maxDistance, sample.distance);
                if (sample.playing) ++playingFrames;
            }
            out << "- over " << series->second.size() << " frames: applied volume " << formatFixed(minVolume, 4)
                << ".." << formatFixed(maxVolume, 4) << ", distance " << formatFixed(minDistance, 2) << ".."
                << formatFixed(maxDistance, 2) << ", playing in " << playingFrames << " frames\n";
        }
    }

    out << "\n## Last events (newest last)\n";
    out << "| t (s) | sound | call | playing before->after | atEnd before | cursor before->after | volume |\n";
    out << "|---|---|---|---|---|---|---|\n";
    const std::size_t first = m_events.size() > 60 ? m_events.size() - 60 : 0;
    for (std::size_t i = first; i < m_events.size(); ++i) {
        const auto& event = m_events[i];
        out << "| " << formatFixed(event.time, 3) << " | " << event.sound << " | " << event.call << " | "
            << event.playingBefore << "->" << event.playingAfter << " | " << event.atEndBefore << " | "
            << formatFixed(event.cursorBefore, 3) << "->" << formatFixed(event.cursorAfter, 3) << " | "
            << formatFixed(event.appliedVolume, 4) << " |\n";
    }
    return out.str();
}

std::string AudioDiagnostics::buildCsv() const {
    std::ostringstream out;
    out << "# output blocks\n";
    out << "time,frames,peakL,peakR,rms,maxDelta,nonFinite,clipped\n";
    for (const auto& block : m_blocks) {
        out << formatFixed(block.time, 6) << "," << block.frames << "," << formatFixed(block.peak[0], 6) << ","
            << formatFixed(block.peak[1], 6) << "," << formatFixed(block.rms, 6) << ","
            << formatFixed(block.maxDelta, 6) << "," << block.nonFinite << "," << block.clipped << "\n";
    }
    for (const auto& [key, series] : m_series) {
        out << "\n# sound " << key << "\n";
        out << "time,cursor,appliedVolume,mixVolume,distance,pan,playing,atEnd\n";
        for (const auto& sample : series) {
            out << formatFixed(sample.time, 6) << "," << formatFixed(sample.cursor, 5) << ","
                << formatFixed(sample.appliedVolume, 5) << "," << formatFixed(sample.mixVolume, 5) << ","
                << formatFixed(sample.distance, 3) << "," << formatFixed(sample.pan, 4) << ","
                << sample.playing << "," << sample.atEnd << "\n";
        }
    }
    out << "\n# events\n";
    out << "time,sound,call,playingBefore,playingAfter,atEndBefore,cursorBefore,cursorAfter,appliedVolume\n";
    for (const auto& event : m_events) {
        out << formatFixed(event.time, 6) << ",\"" << event.sound << "\"," << event.call << ","
            << event.playingBefore << "," << event.playingAfter << "," << event.atEndBefore << ","
            << formatFixed(event.cursorBefore, 5) << "," << formatFixed(event.cursorAfter, 5) << ","
            << formatFixed(event.appliedVolume, 5) << "\n";
    }
    return out.str();
}

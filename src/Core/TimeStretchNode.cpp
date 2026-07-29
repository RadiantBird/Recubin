#include "Core/TimeStretchNode.hpp"

#include <signalsmith-stretch/signalsmith-stretch.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>

namespace {
constexpr float MIN_SPEED = 0.25f;
constexpr float MAX_SPEED = 4.0f;
constexpr std::uint32_t MAX_CALLBACK_FRAMES = 16384;

using Stretch = signalsmith::stretch::SignalsmithStretch<float>;

template<typename Buffer>
void deinterleave(const float* input, std::uint32_t frames, std::uint32_t channels,
                  Buffer& planar, std::uint32_t offset = 0) {
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            planar[channel][offset + frame] = input[frame * channels + channel];
        }
    }
}

template<typename Buffer>
void interleave(const Buffer& planar, std::uint32_t frames, std::uint32_t channels,
                float* output, std::uint32_t offset = 0) {
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            output[frame * channels + channel] = planar[channel][offset + frame];
        }
    }
}
}

struct TimeStretchNode::Impl {
    ma_node_base base{};
    Stretch stretch;
    std::atomic<float> speed{1.0f};
    std::uint32_t channels = 0;
    bool initialized = false;
    bool primed = false;
    bool tailGenerated = false;
    double inputFrameRemainder = 0.0;
    std::uint32_t preRollFrames = 0;
    std::uint32_t tailFrames = 0;
    std::uint32_t tailPosition = 0;
    std::vector<std::vector<float>> inputPlanar;
    std::vector<std::vector<float>> outputPlanar;
    std::vector<std::vector<float>> preRollPlanar;
    std::vector<std::vector<float>> tailPlanar;
    std::vector<float*> inputPointers;
    std::vector<float*> outputPointers;
    std::vector<float*> preRollPointers;
    std::vector<float*> tailPointers;

    static void process(ma_node* rawNode, const float** inputFrames,
                        ma_uint32* inputFrameCount, float** outputFrames,
                        ma_uint32* outputFrameCount) {
        auto* self = static_cast<Impl*>(rawNode);
        const std::uint32_t inputAvailable = *inputFrameCount;
        const std::uint32_t outputCapacity = *outputFrameCount;
        *inputFrameCount = 0;
        *outputFrameCount = 0;

        if (outputCapacity == 0) return;

        if (inputAvailable > 0 && inputFrames && inputFrames[0]) {
            if (self->tailGenerated) {
                self->primed = false;
                self->inputFrameRemainder = 0.0;
                self->preRollFrames = 0;
            }
            self->tailGenerated = false;
            self->tailFrames = 0;
            self->tailPosition = 0;

            if (!self->primed) {
                const std::uint32_t latency = static_cast<std::uint32_t>(
                    std::max(1, self->stretch.inputLatency()));
                const std::uint32_t remaining = latency - self->preRollFrames;
                const std::uint32_t consumed = std::min(inputAvailable, remaining);
                deinterleave(inputFrames[0], consumed, self->channels,
                             self->preRollPlanar, self->preRollFrames);
                self->preRollFrames += consumed;
                *inputFrameCount = consumed;
                if (self->preRollFrames >= latency) {
                    self->stretch.seek(self->preRollPointers, self->preRollFrames,
                                       self->speed.load(std::memory_order_relaxed));
                    self->primed = true;
                }
                return;
            }

            const double currentSpeed = self->speed.load(std::memory_order_relaxed);
            const double availableForOutput =
                (static_cast<double>(inputAvailable) + 1.0 -
                 std::numeric_limits<double>::epsilon() -
                 self->inputFrameRemainder) / currentSpeed;
            std::uint32_t produced = static_cast<std::uint32_t>(
                std::max(0.0, std::floor(availableForOutput)));
            produced = std::min(produced, outputCapacity);
            if (produced == 0) produced = 1;

            std::uint32_t consumed = TimeStretchNode::calculateInputFrameCount(
                produced, static_cast<float>(currentSpeed),
                self->inputFrameRemainder);
            consumed = std::clamp(consumed, 1u, inputAvailable);

            deinterleave(inputFrames[0], consumed, self->channels, self->inputPlanar);
            self->stretch.process(self->inputPointers, static_cast<int>(consumed),
                                  self->outputPointers, static_cast<int>(produced));
            interleave(self->outputPlanar, produced, self->channels, outputFrames[0]);
            *inputFrameCount = consumed;
            *outputFrameCount = produced;
            return;
        }

        if (!self->tailGenerated) {
            if (!self->primed && self->preRollFrames > 0) {
                self->stretch.seek(self->preRollPointers, self->preRollFrames,
                                   self->speed.load(std::memory_order_relaxed));
                self->primed = true;
            }
            if (!self->primed) return;

            const float currentSpeed = self->speed.load(std::memory_order_relaxed);
            const std::uint32_t pendingInput = static_cast<std::uint32_t>(
                std::ceil(self->stretch.inputLatency() / currentSpeed));
            self->tailFrames = std::min<std::uint32_t>(
                pendingInput + static_cast<std::uint32_t>(self->stretch.outputLatency()),
                static_cast<std::uint32_t>(self->tailPlanar[0].size()));
            self->stretch.flush(self->tailPointers, static_cast<int>(self->tailFrames),
                                currentSpeed);
            self->tailGenerated = true;
            self->tailPosition = 0;
        }

        const std::uint32_t remaining = self->tailFrames - self->tailPosition;
        const std::uint32_t produced = std::min(outputCapacity, remaining);
        if (produced > 0) {
            interleave(self->tailPlanar, produced, self->channels,
                       outputFrames[0], self->tailPosition);
            self->tailPosition += produced;
            *outputFrameCount = produced;
        }
    }

    static ma_result requiredInput(ma_node* rawNode, ma_uint32 outputFrameCount,
                                   ma_uint32* inputFrameCount) {
        const auto* self = static_cast<const Impl*>(rawNode);
        const double required = std::ceil(
            outputFrameCount * self->speed.load(std::memory_order_relaxed));
        *inputFrameCount = static_cast<ma_uint32>(std::max(1.0, required));
        return MA_SUCCESS;
    }
};

TimeStretchNode::TimeStretchNode() : m_impl(std::make_unique<Impl>()) {}

TimeStretchNode::~TimeStretchNode() {
    uninitialize();
}

bool TimeStretchNode::initialize(ma_node_graph* graph, std::uint32_t channels,
                                 std::uint32_t sampleRate, float initialSpeed) {
    if (!graph || channels == 0 || sampleRate == 0) return false;
    uninitialize();

    m_impl->channels = channels;
    m_impl->speed.store(clampSpeed(initialSpeed), std::memory_order_relaxed);
    m_impl->stretch.presetDefault(static_cast<int>(channels), sampleRate, true);

    const std::uint32_t preRollCapacity =
        static_cast<std::uint32_t>(m_impl->stretch.inputLatency());
    const std::uint32_t tailCapacity =
        static_cast<std::uint32_t>(std::ceil(preRollCapacity / MIN_SPEED)) +
        static_cast<std::uint32_t>(m_impl->stretch.outputLatency()) +
        MAX_CALLBACK_FRAMES;
    m_impl->inputPlanar.assign(channels, std::vector<float>(MAX_CALLBACK_FRAMES));
    m_impl->outputPlanar.assign(channels, std::vector<float>(MAX_CALLBACK_FRAMES));
    m_impl->preRollPlanar.assign(channels, std::vector<float>(preRollCapacity));
    m_impl->tailPlanar.assign(channels, std::vector<float>(tailCapacity));
    m_impl->inputPointers.resize(channels);
    m_impl->outputPointers.resize(channels);
    m_impl->preRollPointers.resize(channels);
    m_impl->tailPointers.resize(channels);
    for (std::uint32_t channel = 0; channel < channels; ++channel) {
        m_impl->inputPointers[channel] = m_impl->inputPlanar[channel].data();
        m_impl->outputPointers[channel] = m_impl->outputPlanar[channel].data();
        m_impl->preRollPointers[channel] = m_impl->preRollPlanar[channel].data();
        m_impl->tailPointers[channel] = m_impl->tailPlanar[channel].data();
    }

    // Signalsmith grows an internal scratch vector on first use. Exercise the
    // largest callback and tail paths before attachment so the audio callback
    // never allocates or reconfigures the processor.
    m_impl->stretch.process(m_impl->inputPointers,
                            static_cast<int>(MAX_CALLBACK_FRAMES),
                            m_impl->outputPointers,
                            static_cast<int>(MAX_CALLBACK_FRAMES));
    m_impl->stretch.flush(m_impl->tailPointers, static_cast<int>(tailCapacity),
                          MAX_SPEED);
    m_impl->stretch.reset();

    static const ma_node_vtable vtable{
        Impl::process,
        Impl::requiredInput,
        1,
        1,
        MA_NODE_FLAG_DIFFERENT_PROCESSING_RATES
    };
    ma_node_config config = ma_node_config_init();
    config.vtable = &vtable;
    config.pInputChannels = &m_impl->channels;
    config.pOutputChannels = &m_impl->channels;
    if (ma_node_init(graph, &config, nullptr, &m_impl->base) != MA_SUCCESS) {
        return false;
    }
    m_impl->initialized = true;
    reset();
    return true;
}

void TimeStretchNode::uninitialize() {
    if (m_impl && m_impl->initialized) {
        ma_node_uninit(&m_impl->base, nullptr);
        m_impl->initialized = false;
    }
}

void TimeStretchNode::reset() {
    if (!m_impl) return;
    m_impl->stretch.reset();
    m_impl->primed = false;
    m_impl->tailGenerated = false;
    m_impl->inputFrameRemainder = 0.0;
    m_impl->preRollFrames = 0;
    m_impl->tailFrames = 0;
    m_impl->tailPosition = 0;
}

void TimeStretchNode::setSpeed(float newSpeed) {
    m_impl->speed.store(clampSpeed(newSpeed), std::memory_order_relaxed);
}

ma_node* TimeStretchNode::node() {
    return m_impl && m_impl->initialized ? &m_impl->base : nullptr;
}

float TimeStretchNode::clampSpeed(float speed) {
    if (!std::isfinite(speed)) return 1.0f;
    return std::clamp(speed, MIN_SPEED, MAX_SPEED);
}

std::uint32_t TimeStretchNode::calculateInputFrameCount(
        std::uint32_t outputFrames, float speed, double& remainder) {
    const double exactInput =
        outputFrames * static_cast<double>(clampSpeed(speed)) + remainder;
    const auto inputFrames = static_cast<std::uint32_t>(std::floor(exactInput));
    remainder = exactInput - inputFrames;
    return inputFrames;
}

bool TimeStretchNode::processOffline(const std::vector<float>& input,
                                     std::uint32_t channels,
                                     std::uint32_t sampleRate,
                                     float requestedSpeed,
                                     std::vector<float>& output) {
    if (channels == 0 || sampleRate == 0 || input.empty() ||
        input.size() % channels != 0) {
        output.clear();
        return false;
    }

    const float speed = clampSpeed(requestedSpeed);
    const std::size_t inputFrames = input.size() / channels;
    const std::size_t outputFrames = static_cast<std::size_t>(
        std::llround(static_cast<double>(inputFrames) / speed));
    std::vector<std::vector<float>> planarInput(channels,
                                                std::vector<float>(inputFrames));
    std::vector<std::vector<float>> planarOutput(channels,
                                                 std::vector<float>(outputFrames));
    deinterleave(input.data(), static_cast<std::uint32_t>(inputFrames),
                 channels, planarInput);

    std::vector<float*> inputPointers(channels);
    std::vector<float*> outputPointers(channels);
    for (std::uint32_t channel = 0; channel < channels; ++channel) {
        inputPointers[channel] = planarInput[channel].data();
        outputPointers[channel] = planarOutput[channel].data();
    }

    Stretch stretch;
    stretch.presetDefault(static_cast<int>(channels), sampleRate, true);
    stretch.process(inputPointers, static_cast<int>(inputFrames),
                    outputPointers, static_cast<int>(outputFrames));

    output.resize(outputFrames * channels);
    interleave(planarOutput, static_cast<std::uint32_t>(outputFrames),
               channels, output.data());
    return true;
}

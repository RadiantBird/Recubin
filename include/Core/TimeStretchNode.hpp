#pragma once

#include "miniaudio.h"

#include <cstdint>
#include <memory>
#include <vector>

class TimeStretchNode final {
public:
    TimeStretchNode();
    ~TimeStretchNode();

    TimeStretchNode(const TimeStretchNode&) = delete;
    TimeStretchNode& operator=(const TimeStretchNode&) = delete;

    bool initialize(ma_node_graph* graph, std::uint32_t channels,
                    std::uint32_t sampleRate, float speed);
    void uninitialize();
    void reset();
    void setSpeed(float speed);
    ma_node* node();

    static float clampSpeed(float speed);
    static std::uint32_t calculateInputFrameCount(std::uint32_t outputFrames,
                                                  float speed,
                                                  double& remainder);
    static bool processOffline(const std::vector<float>& input,
                               std::uint32_t channels,
                               std::uint32_t sampleRate,
                               float speed,
                               std::vector<float>& output);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

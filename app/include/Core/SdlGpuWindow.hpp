#pragma once

#include <Core/SdlGpuDriver.hpp>

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>
#include <thread>

struct SdlGpuWindowConfig {
    const char* title = "Recubin";
    int width = 1280;
    int height = 720;
    bool present = true;
    bool createWindowOffscreen = false;
    bool visible = true;
    bool highPixelDensity = true;
    bool resizable = true;
    bool debug = false;
    SdlGpuDriver driver = SdlGpuDriver::Auto;
    SDL_GPUShaderFormat shaderFormats = SDL_GPU_SHADERFORMAT_SPIRV |
                                         SDL_GPU_SHADERFORMAT_DXIL |
                                         SDL_GPU_SHADERFORMAT_MSL;
};

// Owns the SDL video subsystem, window, and GPU device. All methods and the
// destructor must be used from the thread which initialized the host. GPU
// resources created from the device must be released before this host is
// destroyed or shut down.
class SdlGpuWindow final {
public:
    struct Frame {
        SDL_GPUCommandBuffer* command = nullptr;
        SDL_GPUTexture* swapchain = nullptr;
        Uint32 width = 0;
        Uint32 height = 0;
        bool valid = false;
        std::uint64_t token = 0;

        explicit operator bool() const { return valid; }
    };

    SdlGpuWindow() = default;
    ~SdlGpuWindow();

    SdlGpuWindow(const SdlGpuWindow&) = delete;
    SdlGpuWindow& operator=(const SdlGpuWindow&) = delete;
    SdlGpuWindow(SdlGpuWindow&&) = delete;
    SdlGpuWindow& operator=(SdlGpuWindow&&) = delete;

    bool initialize(const SdlGpuWindowConfig& config, std::string* error = nullptr);
    void shutdown();

    SDL_Window* window() const { return m_window; }
    SDL_GPUDevice* device() const { return m_device; }
    const std::string& driverName() const { return m_driverName; }

    Frame beginFrame(std::string* error = nullptr);
    bool submit(Frame& frame, SDL_GPUFence** fenceOut = nullptr, std::string* error = nullptr);
    bool cancel(Frame& frame, std::string* error = nullptr);

private:
    bool checkOwner(std::string* error) const;
    bool fail(const char* operation, std::string* error) const;
    void resetState();

    SDL_Window* m_window = nullptr;
    SDL_GPUDevice* m_device = nullptr;
    bool m_claimed = false;
    bool m_videoInitialized = false;
    std::string m_requestedDriver;
    std::string m_driverName;
    std::thread::id m_ownerThread;
    SDL_GPUCommandBuffer* m_outstandingCommand = nullptr;
    bool m_outstandingSwapchain = false;
    std::uint64_t m_nextToken = 0;
    std::uint64_t m_outstandingToken = 0;
};

#include <Core/SdlGpuWindow.hpp>

#include <cstdio>
#include <utility>

namespace {
const char* driverOption(SdlGpuDriver driver) {
    switch (driver) {
    case SdlGpuDriver::Auto: return nullptr;
    case SdlGpuDriver::Vulkan: return "vulkan";
    case SdlGpuDriver::Direct3D12: return "direct3d12";
    case SdlGpuDriver::Metal: return "metal";
    }
    return nullptr;
}

std::string sdlError() {
    const char* value = SDL_GetError();
    return value != nullptr && *value != '\0' ? value : "unknown SDL error";
}
}

SdlGpuWindow::~SdlGpuWindow() {
    shutdown();
}

bool SdlGpuWindow::fail(const char* operation, std::string* error) const {
    std::string message = operation;
    message += " (driver=";
    message += m_driverName.empty() ?
                   (m_requestedDriver.empty() ? "unknown" : m_requestedDriver) : m_driverName;
    message += "): ";
    message += sdlError();
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

bool SdlGpuWindow::checkOwner(std::string* error) const {
    if (m_ownerThread == std::thread::id{} || m_ownerThread == std::this_thread::get_id()) {
        return true;
    }
    if (error != nullptr) {
        *error = "SDL GPU host used from a thread other than its owner";
    }
    return false;
}

void SdlGpuWindow::resetState() {
    m_window = nullptr;
    m_device = nullptr;
    m_claimed = false;
    m_videoInitialized = false;
    m_requestedDriver.clear();
    m_driverName.clear();
    m_ownerThread = {};
    m_outstandingCommand = nullptr;
    m_outstandingSwapchain = false;
    m_outstandingToken = 0;
}

void SdlGpuWindow::shutdown() {
    if (!checkOwner(nullptr)) {
        std::fprintf(stderr, "SdlGpuWindow::shutdown called from a non-owner thread\n");
        return;
    }
    if (m_outstandingCommand != nullptr) {
        if (m_outstandingSwapchain) {
            if (!SDL_SubmitGPUCommandBuffer(m_outstandingCommand)) {
                std::fprintf(stderr, "SdlGpuWindow shutdown failed to submit acquired frame: %s\n",
                             sdlError().c_str());
            }
        } else {
            if (!SDL_CancelGPUCommandBuffer(m_outstandingCommand)) {
                std::fprintf(stderr, "SdlGpuWindow shutdown failed to cancel frame: %s\n",
                             sdlError().c_str());
            }
        }
        m_outstandingCommand = nullptr;
        m_outstandingSwapchain = false;
        m_outstandingToken = 0;
    }
    if (m_device != nullptr) {
        SDL_WaitForGPUIdle(m_device);
        if (m_claimed && m_window != nullptr) {
            SDL_ReleaseWindowFromGPUDevice(m_device, m_window);
        }
        SDL_DestroyGPUDevice(m_device);
    }
    if (m_window != nullptr) {
        SDL_DestroyWindow(m_window);
    }
    if (m_videoInitialized) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
    resetState();
}

bool SdlGpuWindow::initialize(const SdlGpuWindowConfig& config, std::string* error) {
    if (!checkOwner(error)) {
        return false;
    }
    shutdown();
    m_ownerThread = std::this_thread::get_id();
    const char* requestedDriver = driverOption(config.driver);
    m_requestedDriver = requestedDriver != nullptr ? requestedDriver : "auto";
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        m_ownerThread = {};
        return fail("initialize SDL video", error);
    }
    m_videoInitialized = true;

    m_device = SDL_CreateGPUDevice(config.shaderFormats, config.debug, requestedDriver);
    if (m_device == nullptr) {
        const bool result = fail("create GPU device", error);
        shutdown();
        return result;
    }
    const char* actualDriver = SDL_GetGPUDeviceDriver(m_device);
    if (actualDriver == nullptr || *actualDriver == '\0') {
        const bool result = fail("query GPU device driver", error);
        shutdown();
        return result;
    }
    m_driverName = actualDriver;

    if (config.present) {
        SDL_WindowFlags flags = 0;
        if (config.resizable) {
            flags |= SDL_WINDOW_RESIZABLE;
        }
        if (config.highPixelDensity) {
            flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
        }
        if (!config.visible) {
            flags |= SDL_WINDOW_HIDDEN;
        }
        m_window = SDL_CreateWindow(config.title, config.width, config.height, flags);
        if (m_window == nullptr) {
            const bool result = fail("create SDL window", error);
            shutdown();
            return result;
        }
        if (!SDL_ClaimWindowForGPUDevice(m_device, m_window)) {
            const bool result = fail("claim SDL window for GPU device", error);
            shutdown();
            return result;
        }
        m_claimed = true;
    }
    return true;
}

SdlGpuWindow::Frame SdlGpuWindow::beginFrame(std::string* error) {
    Frame frame;
    if (!checkOwner(error)) {
        return frame;
    }
    if (m_outstandingCommand != nullptr) {
        if (error != nullptr) {
            *error = "begin frame called while another SDL GPU frame is outstanding";
        }
        return frame;
    }
    if (m_device == nullptr) {
        fail("acquire GPU command buffer without a device", error);
        return frame;
    }
    frame.command = SDL_AcquireGPUCommandBuffer(m_device);
    if (frame.command == nullptr) {
        fail("acquire GPU command buffer", error);
        return frame;
    }
    frame.valid = true;
    if (m_claimed && !SDL_WaitAndAcquireGPUSwapchainTexture(
                         frame.command, m_window, &frame.swapchain, &frame.width, &frame.height)) {
        const std::string message = sdlError();
        SDL_CancelGPUCommandBuffer(frame.command);
        frame = {};
        if (error != nullptr) {
            *error = "acquire swapchain texture (driver=" + m_driverName + "): " + message;
        }
    } else {
        m_outstandingCommand = frame.command;
        m_outstandingSwapchain = frame.swapchain != nullptr;
        ++m_nextToken;
        if (m_nextToken == 0) {
            ++m_nextToken;
        }
        frame.token = m_nextToken;
        m_outstandingToken = frame.token;
    }
    return frame;
}

bool SdlGpuWindow::submit(Frame& frame, SDL_GPUFence** fenceOut, std::string* error) {
    if (!checkOwner(error)) {
        return false;
    }
    if (!frame.valid || frame.command == nullptr) {
        if (error != nullptr) {
            *error = "submit invalid SDL GPU frame";
        }
        return false;
    }
    if (frame.command != m_outstandingCommand || frame.token == 0 ||
        frame.token != m_outstandingToken) {
        if (error != nullptr) {
            *error = "submit SDL GPU frame not owned by this host";
        }
        return false;
    }
    if (fenceOut != nullptr) {
        *fenceOut = SDL_SubmitGPUCommandBufferAndAcquireFence(frame.command);
        if (*fenceOut == nullptr) {
            m_outstandingCommand = nullptr;
            m_outstandingSwapchain = false;
            m_outstandingToken = 0;
            frame = {};
            return fail("submit GPU command buffer and acquire fence", error);
        }
    } else {
        if (!SDL_SubmitGPUCommandBuffer(frame.command)) {
            m_outstandingCommand = nullptr;
            m_outstandingSwapchain = false;
            m_outstandingToken = 0;
            frame = {};
            return fail("submit GPU command buffer", error);
        }
    }
    m_outstandingCommand = nullptr;
    m_outstandingSwapchain = false;
    m_outstandingToken = 0;
    frame = {};
    return true;
}

bool SdlGpuWindow::cancel(Frame& frame, std::string* error) {
    if (!checkOwner(error)) {
        return false;
    }
    if (!frame.valid || frame.command == nullptr) {
        if (error != nullptr) {
            *error = "cancel invalid SDL GPU frame";
        }
        return false;
    }
    if (frame.command != m_outstandingCommand || frame.token == 0 ||
        frame.token != m_outstandingToken) {
        if (error != nullptr) {
            *error = "cancel SDL GPU frame not owned by this host";
        }
        return false;
    }
    if (m_outstandingSwapchain) {
        if (error != nullptr) {
            *error = "cannot cancel SDL GPU frame after acquiring a swapchain texture";
        }
        return false;
    }
    if (!SDL_CancelGPUCommandBuffer(frame.command)) {
        return fail("cancel GPU command buffer", error);
    }
    m_outstandingCommand = nullptr;
    m_outstandingSwapchain = false;
    m_outstandingToken = 0;
    frame = {};
    return true;
}

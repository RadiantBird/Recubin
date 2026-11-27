#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include <recubin_process.h>

// Loads and drives one local C++ Process DLL.  All methods must be called
// from the engine's frame thread.  A Process can yield only through the host
// API supplied to bindHost(); arbitrary native code cannot be interrupted.
class ProcessHandler {
public:
    struct InstanceCallbacks {
        void* userdata = nullptr;
        RCBNProcessFindChildFn findChild = nullptr;
        RCBNProcessFindChildWithClassFn findChildWithClass = nullptr;
        RCBNProcessGetSystemFn getSystem = nullptr;
        RCBNProcessIsInstanceValidFn isInstanceValid = nullptr;
        RCBNProcessIsInstanceOfFn isInstanceOf = nullptr;
        RCBNProcessGetNameFn getName = nullptr;
        RCBNProcessSetNameFn setName = nullptr;
        RCBNProcessCreateInstanceFn createInstance = nullptr;
        RCBNProcessSetParentFn setParent = nullptr;
    };
    ProcessHandler();
    ~ProcessHandler();
    ProcessHandler(const ProcessHandler&) = delete;
    ProcessHandler& operator=(const ProcessHandler&) = delete;

    bool load(const std::filesystem::path& dllPath);
    void setInstanceCallbacks(const InstanceCallbacks& callbacks);
    // Rebinds the host table immediately before resuming a DLL. Required when
    // multiple handlers share a loaded module on the same thread.
    void rebindHost() noexcept;
    bool start();
    bool pump(double deltaSeconds = -1.0);
    void requestStop();
    bool unload();

    bool isLoaded() const noexcept;
    bool isRunning() const noexcept;
    bool isStopping() const noexcept;
    const std::string& lastError() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};


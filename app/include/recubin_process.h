#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#if defined(_WIN32)
#  define RCBN_PROCESS_EXPORT __declspec(dllexport)
#else
#  define RCBN_PROCESS_EXPORT
#endif

#define RCBN_PROCESS_START extern "C" RCBN_PROCESS_EXPORT void start()
#define RCBN_PROCESS_STOP extern "C" RCBN_PROCESS_EXPORT void stop()
#define RCBN_PROCESS_BIND_HOST extern "C" RCBN_PROCESS_EXPORT void bindHost(const RCBNProcessHostApi* api)

// Versioned ABI shared by a Process DLL and the host. The required DLL
// entry point is `extern "C" void start()`. `stop()` and `bindHost()` are
// optional exports discovered by ProcessHandler. The host table remains
// valid until the DLL is unloaded; DLL code must not retain it afterwards.
inline constexpr std::uint32_t RCBN_PROCESS_HOST_ABI_VERSION = 6;

struct RCBNProcessInstanceHandle {
    std::uint64_t id = 0;
    std::uint64_t generation = 0;
};

extern "C" {
// Returns false when the host requested that the Process stop. The callback
// must not throw: this function is an ABI boundary between the host and DLL.
using RCBNProcessWaitFn = bool (*)(void* userdata, double seconds);
using RCBNProcessShouldStopFn = bool (*)(void* userdata);
using RCBNProcessFindChildFn = bool (*)(void* userdata,
    RCBNProcessInstanceHandle parent, const char* name,
    RCBNProcessInstanceHandle* result);
using RCBNProcessFindChildWithClassFn = bool (*)(void* userdata,
    RCBNProcessInstanceHandle parent, const char* name, const char* className,
    RCBNProcessInstanceHandle* result);
using RCBNProcessGetSystemFn = bool (*)(void* userdata,
    RCBNProcessInstanceHandle* result);
using RCBNProcessIsInstanceValidFn = bool (*)(void* userdata,
    RCBNProcessInstanceHandle handle);
using RCBNProcessIsInstanceOfFn = bool (*)(void* userdata,
    RCBNProcessInstanceHandle handle, const char* className);
using RCBNProcessGetNameFn = bool (*)(void* userdata,
    RCBNProcessInstanceHandle handle, char* buffer, std::size_t capacity,
    std::size_t* required);
using RCBNProcessSetNameFn = bool (*)(void* userdata,
    RCBNProcessInstanceHandle handle, const char* name);
using RCBNProcessCreateInstanceFn = bool (*)(void* userdata,
    const char* className, RCBNProcessInstanceHandle* result);
using RCBNProcessSetParentFn = bool (*)(void* userdata,
    RCBNProcessInstanceHandle child, RCBNProcessInstanceHandle parent);
using RCBNProcessHeartbeatCallback = void (*)(void* userdata, double deltaSeconds);
using RCBNProcessConnectHeartbeatFn = bool (*)(void* userdata,
    RCBNProcessHeartbeatCallback callback, void* callbackUserdata,
    std::uint64_t* connectionId);
using RCBNProcessDisconnectHeartbeatFn = bool (*)(void* userdata,
    std::uint64_t connectionId);

struct RCBNProcessHostApi {
    std::uint32_t version;
    void* userdata;
    RCBNProcessWaitFn wait;
    RCBNProcessShouldStopFn shouldStop;
    RCBNProcessFindChildFn findChild;
    RCBNProcessFindChildWithClassFn findChildWithClass;
    RCBNProcessGetSystemFn getSystem;
    RCBNProcessIsInstanceValidFn isInstanceValid;
    RCBNProcessIsInstanceOfFn isInstanceOf;
    RCBNProcessGetNameFn getName;
    RCBNProcessSetNameFn setName;
    RCBNProcessCreateInstanceFn createInstance;
    RCBNProcessSetParentFn setParent;
    RCBNProcessConnectHeartbeatFn connectHeartbeat;
    RCBNProcessDisconnectHeartbeatFn disconnectHeartbeat;
};

using RCBNProcessStartFn = void (*)();
using RCBNProcessStopFn = void (*)();
using RCBNProcessBindHostFn = void (*)(const RCBNProcessHostApi* api);
}

// Forward declarations let a user write ordinary `void start() {}` after
// including this header; the prior C-linkage/export declaration is inherited.
extern "C" RCBN_PROCESS_EXPORT void start();
extern "C" RCBN_PROCESS_EXPORT void stop();
extern "C" RCBN_PROCESS_EXPORT void bindHost(const RCBNProcessHostApi* api);


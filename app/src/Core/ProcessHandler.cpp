#include <Core/ProcessHandler.hpp>

#include <chrono>
#include <exception>
#include <thread>
#include <unordered_map>
#include <vector>

#include <Util/Logger.hpp>

#if defined(_WIN32)
#include <Windows.h>
#endif

struct ProcessHandler::Impl {
#if defined(_WIN32)
    HMODULE module = nullptr;
    LPVOID schedulerFiber = nullptr;
    LPVOID processFiber = nullptr;
#endif
    RCBNProcessStartFn startFn = nullptr;
    RCBNProcessStopFn stopFn = nullptr;
    RCBNProcessStartFn entryFn = nullptr;
    RCBNProcessStopFn stopEntryFn = nullptr;
    RCBNProcessBindHostFn bindHostFn = nullptr;
    RCBNProcessBindHostFn engineBindHostFn = nullptr;
    RCBNProcessUnbindHostFn unbindHostFn = nullptr;
    std::filesystem::path path;
    std::string error;
    bool running = false;
    bool started = false;
    bool stopping = false;
    bool returned = false;
    std::chrono::steady_clock::time_point lastYield{};
    std::chrono::steady_clock::time_point wakeTime{};
    RCBNProcessHostApi hostApi{};
    ProcessHandler::InstanceCallbacks instanceCallbacks{};
    RCBNProcessInstanceHandle selfHandle{};
    static bool getSelfProcess(void* userdata, RCBNProcessInstanceHandle* result) {
        auto* self = static_cast<Impl*>(userdata);
        if (!self || !result || !self->selfHandle.id || !self->instanceCallbacks.isInstanceValid ||
            !self->instanceCallbacks.isInstanceValid(self->instanceCallbacks.userdata, self->selfHandle)) return false;
        *result = self->selfHandle;
        return true;
    }
    static bool getParentBridge(void* userdata, RCBNProcessInstanceHandle child, RCBNProcessInstanceHandle* result) {
        auto* self = static_cast<Impl*>(userdata);
        return self && self->instanceCallbacks.getParent &&
            self->instanceCallbacks.getParent(self->instanceCallbacks.userdata, child, result);
    }
    static bool getPropertyBridge(void* userdata, RCBNProcessInstanceHandle handle, const char* name,
        std::uint32_t* kind, double* values, char* buffer, std::size_t capacity, std::size_t* required) {
        auto* self = static_cast<Impl*>(userdata);
        return self && self->instanceCallbacks.getProperty &&
            self->instanceCallbacks.getProperty(self->instanceCallbacks.userdata, handle, name,
                kind, values, buffer, capacity, required);
    }
    static bool setPropertyBridge(void* userdata, RCBNProcessInstanceHandle handle, const char* name,
        std::uint32_t kind, const double* values, const char* stringValue) {
        auto* self = static_cast<Impl*>(userdata);
        return self && self->instanceCallbacks.setProperty &&
            self->instanceCallbacks.setProperty(self->instanceCallbacks.userdata, handle, name,
                kind, values, stringValue);
    }
    struct HeartbeatConnection {
        RCBNProcessHeartbeatCallback callback = nullptr;
        void* userdata = nullptr;
        RCBNProcessHeartbeatCleanupFn cleanup = nullptr;
    };
    std::unordered_map<std::uint64_t, HeartbeatConnection> heartbeatConnections;
    struct EventCallbackState : std::enable_shared_from_this<EventCallbackState> {
        Impl* owner = nullptr;
        RCBNProcessInstanceEventCallback callback = nullptr;
        void* callbackUserdata = nullptr;
        RCBNProcessHeartbeatCleanupFn cleanup = nullptr;
        ~EventCallbackState() {
            if (cleanup) cleanup(callbackUserdata);
        }
    };
    struct InstanceEventConnection {
        RCBNProcessInstanceHandle instance{};
        std::uint64_t subscriptionId = 0;
        std::shared_ptr<EventCallbackState> callback;
    };
    std::unordered_map<std::uint64_t, InstanceEventConnection> instanceEventConnections;
    std::uint64_t nextHeartbeatConnection = 1;
    bool inHeartbeatCallback = false;

    static bool wait(void* userdata, double seconds) {
        auto* self = static_cast<Impl*>(userdata);
        if (!self) return false;
#if defined(_WIN32)
        if (self->inHeartbeatCallback) {
            self->error = "task.wait() cannot be called from a Process Heartbeat callback";
            return false;
        }
        if (self->stopping) return false;
        self->wakeTime = std::chrono::steady_clock::now() +
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(seconds > 0.0 ? seconds : 0.0));
        self->lastYield = std::chrono::steady_clock::now();
        if (self->schedulerFiber && self->processFiber) {
            SwitchToFiber(self->schedulerFiber);
        }
        return !self->stopping;
#else
        (void)seconds;
        return !self->stopping;
#endif
    }

    static bool shouldStop(void* userdata) {
        const auto* self = static_cast<const Impl*>(userdata);
        return !self || self->stopping;
    }

    static bool findChildBridge(void* userdata, RCBNProcessInstanceHandle parent,
                                const char* name, RCBNProcessInstanceHandle* result) {
        auto* self = static_cast<Impl*>(userdata);
        return self && self->instanceCallbacks.findChild &&
            self->instanceCallbacks.findChild(self->instanceCallbacks.userdata, parent, name, result);
    }

    static bool findChildWithClassBridge(void* userdata, RCBNProcessInstanceHandle parent,
                                         const char* name, const char* className,
                                         RCBNProcessInstanceHandle* result) {
        auto* self = static_cast<Impl*>(userdata);
        return self && self->instanceCallbacks.findChildWithClass &&
            self->instanceCallbacks.findChildWithClass(self->instanceCallbacks.userdata,
                                                        parent, name, className, result);
    }

    static bool getSystemBridge(void* userdata, RCBNProcessInstanceHandle* result) {
        auto* self = static_cast<Impl*>(userdata);
        return self && self->instanceCallbacks.getSystem &&
            self->instanceCallbacks.getSystem(self->instanceCallbacks.userdata, result);
    }

    static bool isInstanceValidBridge(void* userdata, RCBNProcessInstanceHandle handle) {
        auto* self = static_cast<Impl*>(userdata);
        return self && self->instanceCallbacks.isInstanceValid &&
            self->instanceCallbacks.isInstanceValid(self->instanceCallbacks.userdata, handle);
    }
    static bool isInstanceOfBridge(void* userdata, RCBNProcessInstanceHandle handle,
                                   const char* className) {
        auto* self = static_cast<Impl*>(userdata);
        return self && self->instanceCallbacks.isInstanceOf &&
            self->instanceCallbacks.isInstanceOf(self->instanceCallbacks.userdata, handle, className);
    }
    static bool getNameBridge(void* userdata, RCBNProcessInstanceHandle handle,
                              char* buffer, std::size_t capacity, std::size_t* required) {
        auto* self = static_cast<Impl*>(userdata);
        return self && self->instanceCallbacks.getName &&
            self->instanceCallbacks.getName(self->instanceCallbacks.userdata, handle, buffer, capacity, required);
    }
    static bool setNameBridge(void* userdata, RCBNProcessInstanceHandle handle, const char* name) {
        auto* self = static_cast<Impl*>(userdata);
        return self && self->instanceCallbacks.setName &&
            self->instanceCallbacks.setName(self->instanceCallbacks.userdata, handle, name);
    }
    static bool createInstanceBridge(void* userdata, const char* className,
                                     RCBNProcessInstanceHandle* result) {
        auto* self = static_cast<Impl*>(userdata);
        return self && self->instanceCallbacks.createInstance &&
            self->instanceCallbacks.createInstance(self->instanceCallbacks.userdata, className, result);
    }
    static bool setParentBridge(void* userdata, RCBNProcessInstanceHandle child,
                                RCBNProcessInstanceHandle parent) {
        auto* self = static_cast<Impl*>(userdata);
        return self && self->instanceCallbacks.setParent &&
            self->instanceCallbacks.setParent(self->instanceCallbacks.userdata, child, parent);
    }

    static bool connectHeartbeat(void* userdata, RCBNProcessHeartbeatCallback callback,
                                void* callbackUserdata, std::uint64_t* connectionId) {
        auto* self = static_cast<Impl*>(userdata);
        if (!self || !callback || !connectionId || self->stopping) return false;
        const auto id = self->nextHeartbeatConnection++;
        self->heartbeatConnections.emplace(id, HeartbeatConnection{callback, callbackUserdata, nullptr});
        *connectionId = id;
        return true;
    }

    static bool connectManagedHeartbeat(void* userdata, RCBNProcessHeartbeatCallback callback,
        void* callbackUserdata, RCBNProcessHeartbeatCleanupFn cleanup, std::uint64_t* connectionId) {
        auto* self = static_cast<Impl*>(userdata);
        if (!self || !callback || !cleanup || !connectionId || self->stopping) return false;
        const std::uint64_t id = self->nextHeartbeatConnection++;
        self->heartbeatConnections.emplace(id, HeartbeatConnection{callback, callbackUserdata, cleanup});
        *connectionId = id;
        return true;
    }

    static bool disconnectHeartbeat(void* userdata, std::uint64_t connectionId) {
        auto* self = static_cast<Impl*>(userdata);
        if (!self || connectionId == 0) return false;
        const auto found = self->heartbeatConnections.find(connectionId);
        if (found != self->heartbeatConnections.end()) {
            if (found->second.cleanup) found->second.cleanup(found->second.userdata);
            self->heartbeatConnections.erase(found);
            return true;
        }
        const auto event = self->instanceEventConnections.find(connectionId);
        if (event == self->instanceEventConnections.end()) return false;
        bool disconnected = false;
        if (self->instanceCallbacks.disconnectInstanceEvent) {
            disconnected = self->instanceCallbacks.disconnectInstanceEvent(
                self->instanceCallbacks.userdata, event->second.instance,
                event->second.subscriptionId);
        }
        self->instanceEventConnections.erase(event);
        return disconnected;
    }

    static void dispatchInstanceEvent(void* userdata, RCBNProcessInstanceHandle eventInstance) {
        auto* raw = static_cast<EventCallbackState*>(userdata);
        if (!raw || !raw->callback) return;
        std::shared_ptr<EventCallbackState> keepAlive = raw->shared_from_this();
        try {
            keepAlive->callback(keepAlive->callbackUserdata, eventInstance);
        } catch (...) {
            keepAlive->owner->error = "Unhandled C++ exception escaped Process Instance event callback";
            keepAlive->owner->stopping = true;
        }
    }

    static bool connectInstanceEvent(void* userdata, RCBNProcessInstanceHandle instance,
        std::uint32_t eventKind, RCBNProcessInstanceEventCallback callback,
        void* callbackUserdata, RCBNProcessHeartbeatCleanupFn cleanup,
        std::uint64_t* connectionId) {
        auto* self = static_cast<Impl*>(userdata);
        if (!self || self->stopping || !callback || !cleanup || !connectionId ||
            !self->instanceCallbacks.connectInstanceEvent ||
            !self->instanceCallbacks.disconnectInstanceEvent) return false;
        std::shared_ptr<EventCallbackState> state = std::make_shared<EventCallbackState>();
        state->owner = self;
        state->callback = callback;
        state->callbackUserdata = callbackUserdata;
        state->cleanup = cleanup;
        std::uint64_t subscriptionId = 0;
        if (!self->instanceCallbacks.connectInstanceEvent(self->instanceCallbacks.userdata,
                instance, eventKind, &dispatchInstanceEvent, state.get(), &subscriptionId) ||
            subscriptionId == 0) return false;
        const std::uint64_t id = self->nextHeartbeatConnection++;
        self->instanceEventConnections.emplace(id,
            InstanceEventConnection{instance, subscriptionId, std::move(state)});
        *connectionId = id;
        return true;
    }

    static void reportError(void* userdata, const char* message) {
        auto* self = static_cast<Impl*>(userdata);
        if (!self) return;
        self->error = message && *message
            ? message
            : "Unhandled C++ exception escaped Process DLL";
        self->stopping = true;
    }

    void clearHeartbeatConnections() noexcept {
        for (auto& [id, connection] : heartbeatConnections) {
            (void)id;
            if (connection.cleanup) connection.cleanup(connection.userdata);
        }
        heartbeatConnections.clear();
        while (!instanceEventConnections.empty()) {
            const auto current = instanceEventConnections.begin();
            if (instanceCallbacks.disconnectInstanceEvent) {
                instanceCallbacks.disconnectInstanceEvent(instanceCallbacks.userdata,
                    current->second.instance, current->second.subscriptionId);
            }
            instanceEventConnections.erase(current);
        }
    }

    void fireHeartbeat(double deltaSeconds) {
        std::vector<std::uint64_t> ids;
        ids.reserve(heartbeatConnections.size());
        for (const auto& [id, connection] : heartbeatConnections) {
            (void)connection;
            ids.push_back(id);
        }
        for (const auto id : ids) {
            const auto current = heartbeatConnections.find(id);
            if (current == heartbeatConnections.end()) continue;
            const auto connection = current->second;
            if (connection.callback) {
                inHeartbeatCallback = true;
                try {
                    connection.callback(connection.userdata, deltaSeconds);
                } catch (...) {
                    error = "Unhandled C++ exception escaped Process Heartbeat callback";
                    stopping = true;
                }
                inHeartbeatCallback = false;
            }
        }
    }

#if defined(_WIN32)
    static VOID CALLBACK processEntry(void* parameter) {
        auto* self = static_cast<Impl*>(parameter);
        if (!self || !self->startFn) return;
        try {
            (self->entryFn ? self->entryFn : self->startFn)();
        } catch (...) {
            self->error = "Unhandled C++ exception escaped Process start()";
        }
        self->returned = true;
        // A Process that returns from start() may still own Heartbeat
        // connections. Keep its module alive until those connections are
        // explicitly disconnected or the host begins teardown.
        self->running = !self->heartbeatConnections.empty() ||
            !self->instanceEventConnections.empty();
        if (self->schedulerFiber) SwitchToFiber(self->schedulerFiber);
    }
#endif
};

ProcessHandler::ProcessHandler() : m_impl(std::make_unique<Impl>()) {}

void ProcessHandler::setInstanceCallbacks(const InstanceCallbacks& callbacks) {
    m_impl->instanceCallbacks = callbacks;
}

void ProcessHandler::setSelfProcess(const std::shared_ptr<Instance>& instance) {
    m_impl->selfHandle = m_impl->instanceCallbacks.registerInstance
        ? m_impl->instanceCallbacks.registerInstance(m_impl->instanceCallbacks.userdata, instance)
        : RCBNProcessInstanceHandle{};
}

void ProcessHandler::rebindHost() noexcept {
    try {
        if (m_impl->engineBindHostFn && m_impl->module)
            m_impl->engineBindHostFn(&m_impl->hostApi);
        if (m_impl->bindHostFn && m_impl->module)
            m_impl->bindHostFn(&m_impl->hostApi);
    } catch (const std::exception& error) {
        m_impl->error = std::string("Process bindHost() failed: ") + error.what();
        m_impl->stopping = true;
    } catch (...) {
        m_impl->error = "Process bindHost() failed with an unknown exception";
        m_impl->stopping = true;
    }
}

ProcessHandler::~ProcessHandler() {
    requestStop();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (m_impl->running && std::chrono::steady_clock::now() < deadline) {
        pump();
    }
    if (!unload()) {
        RCBN_WARN("ProcessHandler: DLL remained loaded because Process did not return from start()");
        (void)m_impl.release();
    }
}

bool ProcessHandler::load(const std::filesystem::path& dllPath) {
    if (!unload()) {
        m_impl->error = "Cannot load a new Process while the current DLL is still running";
        return false;
    }
    m_impl->error.clear();
    m_impl->path = dllPath;
#if !defined(_WIN32)
    m_impl->error = "Process DLLs are supported only on Windows";
    return false;
#else
    m_impl->module = LoadLibraryW(dllPath.wstring().c_str());
    if (!m_impl->module) {
        m_impl->error = "LoadLibraryW failed for " + dllPath.string();
        return false;
    }
    m_impl->startFn = reinterpret_cast<RCBNProcessStartFn>(GetProcAddress(m_impl->module, "start"));
    m_impl->stopFn = reinterpret_cast<RCBNProcessStopFn>(GetProcAddress(m_impl->module, "stop"));
    m_impl->entryFn = reinterpret_cast<RCBNProcessStartFn>(GetProcAddress(m_impl->module, "rcbn_process_entry"));
    m_impl->stopEntryFn = reinterpret_cast<RCBNProcessStopFn>(GetProcAddress(m_impl->module, "rcbn_process_stop_entry"));
    m_impl->bindHostFn = reinterpret_cast<RCBNProcessBindHostFn>(GetProcAddress(m_impl->module, "bindHost"));
    m_impl->engineBindHostFn = reinterpret_cast<RCBNProcessBindHostFn>(GetProcAddress(m_impl->module, "rcbn_process_bind_host"));
    m_impl->unbindHostFn = reinterpret_cast<RCBNProcessUnbindHostFn>(GetProcAddress(m_impl->module, "rcbn_process_unbind_host"));
    if (!m_impl->startFn) {
        m_impl->error = "Process DLL does not export start()";
        FreeLibrary(m_impl->module);
        m_impl->module = nullptr;
        return false;
    }
    return true;
#endif
}

bool ProcessHandler::start() {
    if (!isLoaded()) {
        m_impl->error = "ProcessHandler::start called before load";
        return false;
    }
    if (m_impl->running) return true;
#if !defined(_WIN32)
    m_impl->error = "Process DLLs are supported only on Windows";
    return false;
#else
    if (!IsThreadAFiber()) {
        m_impl->schedulerFiber = ConvertThreadToFiber(nullptr);
    } else {
        m_impl->schedulerFiber = GetCurrentFiber();
    }
    if (!m_impl->schedulerFiber) {
        m_impl->error = "Failed to initialize the scheduler fiber";
        return false;
    }
    m_impl->hostApi = {
        RCBN_PROCESS_HOST_ABI_VERSION,
        m_impl.get(),
        &Impl::wait,
        &Impl::shouldStop,
        &Impl::findChildBridge,
        &Impl::findChildWithClassBridge,
        &Impl::getSystemBridge,
        &Impl::isInstanceValidBridge,
        &Impl::isInstanceOfBridge,
        &Impl::getNameBridge,
        &Impl::setNameBridge,
        &Impl::createInstanceBridge,
        &Impl::setParentBridge,
        &Impl::connectHeartbeat,
        &Impl::disconnectHeartbeat,
        &Impl::reportError,
        &Impl::getSelfProcess,
        &Impl::getParentBridge,
        &Impl::getPropertyBridge,
        &Impl::setPropertyBridge,
        &Impl::connectManagedHeartbeat,
        &Impl::connectInstanceEvent};
    m_impl->hostApi.userdata = m_impl.get();
    m_impl->error.clear();
    m_impl->stopping = false;
    m_impl->returned = false;
    rebindHost();
    if (!m_impl->error.empty()) return false;
    m_impl->running = true;
    m_impl->lastYield = std::chrono::steady_clock::now();
    m_impl->processFiber = CreateFiber(0, &Impl::processEntry, m_impl.get());
    if (!m_impl->processFiber) {
        m_impl->running = false;
        m_impl->error = "CreateFiber failed";
        return false;
    }
    m_impl->started = true;
    SwitchToFiber(m_impl->processFiber);
    return true;
#endif
}

bool ProcessHandler::pump(double deltaSeconds) {
    if (!m_impl->running) return !m_impl->stopping;
#if defined(_WIN32)
    if (!m_impl->processFiber) return false;
    const auto now = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(now - m_impl->lastYield).count();
    const double delta = deltaSeconds >= 0.0 ? deltaSeconds : elapsed;
    rebindHost();
    m_impl->fireHeartbeat(delta > 0.0 ? delta : 0.0);
    if (m_impl->returned && m_impl->heartbeatConnections.empty() &&
        m_impl->instanceEventConnections.empty()) {
        m_impl->running = false;
    }
    if (!m_impl->running) return !m_impl->stopping;
    if (m_impl->returned) return true;
    if (!m_impl->stopping && now < m_impl->wakeTime) {
        return true;
    }
    SwitchToFiber(m_impl->processFiber);
    return m_impl->running;
#else
    return false;
#endif
}

void ProcessHandler::requestStop() {
    m_impl->stopping = true;
    m_impl->clearHeartbeatConnections();
}

bool ProcessHandler::unload() {
#if !defined(_WIN32)
    return !isLoaded();
#else
    if (!m_impl->module) return true;
    if (m_impl->running || (m_impl->started && !m_impl->returned)) {
        m_impl->error = "Process DLL cannot be unloaded while start() is still running";
        return false;
    }
    rebindHost();
    try {
        if (m_impl->stopEntryFn) {
            m_impl->stopEntryFn();
        } else if (m_impl->stopFn) {
            m_impl->stopFn();
        }
    } catch (const std::exception& error) {
        m_impl->error = std::string("Unhandled C++ exception escaped Process stop(): ") + error.what();
    } catch (...) {
        m_impl->error = "Unhandled C++ exception escaped Process stop()";
    }
    if (m_impl->unbindHostFn) m_impl->unbindHostFn(&m_impl->hostApi);
    m_impl->clearHeartbeatConnections();
    if (m_impl->processFiber) {
        DeleteFiber(m_impl->processFiber);
        m_impl->processFiber = nullptr;
    }
    const bool ok = FreeLibrary(m_impl->module) != FALSE;
    if (ok) m_impl->module = nullptr;
    return ok;
#endif
}

bool ProcessHandler::isLoaded() const noexcept {
#if defined(_WIN32)
    return m_impl->module != nullptr;
#else
    return false;
#endif
}

bool ProcessHandler::isRunning() const noexcept { return m_impl->running; }
bool ProcessHandler::isStopping() const noexcept { return m_impl->stopping; }
const std::string& ProcessHandler::lastError() const noexcept { return m_impl->error; }


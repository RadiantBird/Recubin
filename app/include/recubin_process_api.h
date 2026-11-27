#pragma once

#include <optional>
#include <exception>
#include <string>
#include <string_view>
#include <stdexcept>
#include <vector>

#include <recubin_process.h>

namespace recubin::process {

extern "C" const RCBNProcessHostApi* recubin_process_host_api();

inline thread_local const RCBNProcessHostApi* g_hostApi = nullptr;

inline void InstallHostApi(const RCBNProcessHostApi* api) noexcept {
    g_hostApi = api;
}

class InstanceRef {
public:
    InstanceRef() = default;

    bool valid() const noexcept {
        if (!m_api || m_handle.id == 0) return false;
        if (!m_api->isInstanceValid) return true;
        return m_api->isInstanceValid(m_api->userdata, m_handle);
    }
    bool isA(std::string_view className) const {
        if (!valid() || !m_api->isInstanceOf) return false;
        std::string copy(className);
        return m_api->isInstanceOf(m_api->userdata, m_handle, copy.c_str());
    }
    std::string Name() const {
        if (!valid() || !m_api->getName) return {};
        std::size_t required = 0;
        if (!m_api->getName(m_api->userdata, m_handle, nullptr, 0, &required)) return {};
        std::string result(required, '\0');
        if (!m_api->getName(m_api->userdata, m_handle, result.data(), result.size() + 1, &required)) return {};
        result.resize(required);
        return result;
    }
    bool SetName(std::string_view name) const {
        if (!valid() || !m_api->setName) return false;
        std::string copy(name);
        return m_api->setName(m_api->userdata, m_handle, copy.c_str());
    }
    bool SetParent(const InstanceRef& parent) const {
        if (!valid() || !parent.valid() || !m_api->setParent) return false;
        return m_api->setParent(m_api->userdata, m_handle, parent.m_handle);
    }
    template <typename U>
    std::optional<U> As() const {
        if (!isA(U::ClassName())) return std::nullopt;
        return U(m_api, m_handle);
    }
    RCBNProcessInstanceHandle handle() const noexcept { return m_handle; }

    std::optional<InstanceRef> FindChild(std::string_view name) const {
        if (!valid() || !m_api->findChild) return std::nullopt;
        RCBNProcessInstanceHandle result{};
        std::string copy(name);
        if (!m_api->findChild(m_api->userdata, m_handle, copy.c_str(), &result)) {
            return std::nullopt;
        }
        return InstanceRef(m_api, result);
    }

    template <typename U>
    std::optional<U> FindChildWithClass(std::string_view name) const {
        if (!valid() || !m_api->findChildWithClass) return std::nullopt;
        RCBNProcessInstanceHandle result{};
        std::string copy(name);
        const auto className = U::ClassName();
        if (!m_api->findChildWithClass(m_api->userdata, m_handle, copy.c_str(),
                                       className.data(), &result)) {
            return std::nullopt;
        }
        return U(m_api, result);
    }

protected:
    InstanceRef(const RCBNProcessHostApi* api, RCBNProcessInstanceHandle handle)
        : m_api(api), m_handle(handle) {}
    const RCBNProcessHostApi* m_api = nullptr;
    RCBNProcessInstanceHandle m_handle{};
};

template <typename Derived>
class TypedInstanceRef : public InstanceRef {
public:
    static constexpr std::string_view ClassName() { return Derived::kClassName; }
    TypedInstanceRef() = default;

protected:
    TypedInstanceRef(const RCBNProcessHostApi* api, RCBNProcessInstanceHandle handle)
        : InstanceRef(api, handle) {}
    friend class InstanceRef;
};

template <typename U>
std::optional<U> New() {
    if (!g_hostApi || !g_hostApi->createInstance) return std::nullopt;
    RCBNProcessInstanceHandle result{};
    const auto className = U::ClassName();
    std::string copy(className);
    if (!g_hostApi->createInstance(g_hostApi->userdata, copy.c_str(), &result)) return std::nullopt;
    return U(g_hostApi, result);
}

class HeartbeatConnection {
public:
    HeartbeatConnection() = default;
    HeartbeatConnection(const RCBNProcessHostApi* api, std::uint64_t id)
        : m_api(api), m_id(id) {}
    HeartbeatConnection(const HeartbeatConnection&) = delete;
    HeartbeatConnection& operator=(const HeartbeatConnection&) = delete;
    HeartbeatConnection(HeartbeatConnection&& other) noexcept
        : m_api(other.m_api), m_id(other.m_id) { other.m_api = nullptr; other.m_id = 0; }
    HeartbeatConnection& operator=(HeartbeatConnection&& other) noexcept {
        if (this != &other) {
            Disconnect();
            m_api = other.m_api;
            m_id = other.m_id;
            other.m_api = nullptr;
            other.m_id = 0;
        }
        return *this;
    }
    ~HeartbeatConnection() { Disconnect(); }
    bool Disconnect() noexcept {
        if (!m_api || m_id == 0 || !m_api->disconnectHeartbeat) return false;
        const bool disconnected = m_api->disconnectHeartbeat(m_api->userdata, m_id);
        m_api = nullptr;
        m_id = 0;
        return disconnected;
    }
    bool valid() const noexcept { return m_api != nullptr && m_id != 0; }

private:
    const RCBNProcessHostApi* m_api = nullptr;
    std::uint64_t m_id = 0;
};

using HeartbeatCallback = RCBNProcessHeartbeatCallback;

class System : public TypedInstanceRef<System> {
public:
    static constexpr std::string_view kClassName = "System";
    System() = default;
    System(const RCBNProcessHostApi* api, RCBNProcessInstanceHandle handle)
        : TypedInstanceRef(api, handle) {}
    std::optional<HeartbeatConnection> ConnectHeartbeat(HeartbeatCallback callback,
                                                        void* userdata = nullptr) const {
        if (!valid() || !m_api->connectHeartbeat || !callback) return std::nullopt;
        std::uint64_t connectionId = 0;
        if (!m_api->connectHeartbeat(m_api->userdata, callback, userdata, &connectionId) ||
            connectionId == 0) return std::nullopt;
        return HeartbeatConnection(m_api, connectionId);
    }
private:
    friend class InstanceRef;
};

class Workspace : public TypedInstanceRef<Workspace> {
public:
    static constexpr std::string_view kClassName = "Workspace";
    Workspace() = default;
    Workspace(const RCBNProcessHostApi* api, RCBNProcessInstanceHandle handle)
        : TypedInstanceRef(api, handle) {}
private:
    friend class InstanceRef;
};

class Cube : public TypedInstanceRef<Cube> {
public:
    static constexpr std::string_view kClassName = "Cube";
    Cube() = default;
    Cube(const RCBNProcessHostApi* api, RCBNProcessInstanceHandle handle)
        : TypedInstanceRef(api, handle) {}
private:
    friend class InstanceRef;
};

inline std::optional<System> GetSystem() {
    const auto* api = recubin_process_host_api();
    if (!api || !api->getSystem) return std::nullopt;
    RCBNProcessInstanceHandle result{};
    if (!api->getSystem(api->userdata, &result)) return std::nullopt;
    return System(api, result);
}

namespace task {
class StopRequested final : public std::exception {
public:
    const char* what() const noexcept override {
        return "Recubin Process stop requested";
    }
};

inline void wait(double seconds) {
    const auto* api = recubin_process_host_api();
    if (!api || !api->wait) {
        throw std::runtime_error("Recubin Process host API is not installed");
    }
    if (!api->wait(api->userdata, seconds)) {
        // Keep the stop exception inside the DLL. The host callback only
        // returns an ABI-safe status and therefore never throws across the
        // module boundary.
        throw StopRequested{};
    }
}
inline bool shouldStop() noexcept {
    const auto* api = recubin_process_host_api();
    return api && api->shouldStop
        ? api->shouldStop(api->userdata)
        : false;
}
} // namespace task

} // namespace recubin::process


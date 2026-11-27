#pragma once

#include <array>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <recubin_process.h>
#include <Math/Vector3.hpp>
#include <Math/CFrame.hpp>
#include <Util/Color4.hpp>

namespace recubin::process {

extern "C" const RCBNProcessHostApi* recubin_process_host_api();
inline thread_local const RCBNProcessHostApi* g_hostApi = nullptr;

template<class T> class InstanceRef;
struct Instance { template<class T> static InstanceRef<T> New(); };
struct Process {};
struct System {};
struct Workspace {};
struct BaseCube {};
struct Cube {};

namespace detail {
struct HostContext {
    const RCBNProcessHostApi* api = nullptr;
};
inline std::mutex& contextMutex() {
    static std::mutex value;
    return value;
}
inline std::unordered_map<const RCBNProcessHostApi*, std::weak_ptr<HostContext>>& contexts() {
    static std::unordered_map<const RCBNProcessHostApi*, std::weak_ptr<HostContext>> value;
    return value;
}
inline thread_local std::shared_ptr<HostContext> activeContext;
inline std::shared_ptr<HostContext> contextFor(const RCBNProcessHostApi* api) {
    if (!api) return {};
    std::scoped_lock lock(contextMutex());
    std::weak_ptr<HostContext>& entry = contexts()[api];
    if (std::shared_ptr<HostContext> existing = entry.lock()) return existing;
    std::shared_ptr<HostContext> created = std::make_shared<HostContext>();
    created->api = api;
    entry = created;
    return created;
}
inline const RCBNProcessHostApi& api(const std::shared_ptr<HostContext>& context) {
    if (!context || !context->api) throw std::runtime_error("Recubin Process host API is unavailable");
    return *context->api;
}
}

inline void InstallHostApi(const RCBNProcessHostApi* api) noexcept {
    g_hostApi = api;
    detail::activeContext = detail::contextFor(api);
}
inline void UninstallHostApi(const RCBNProcessHostApi* api) noexcept {
    if (!api) return;
    std::scoped_lock lock(detail::contextMutex());
    std::unordered_map<const RCBNProcessHostApi*, std::weak_ptr<detail::HostContext>>::iterator found =
        detail::contexts().find(api);
    if (found != detail::contexts().end()) {
        if (std::shared_ptr<detail::HostContext> context = found->second.lock()) context->api = nullptr;
        detail::contexts().erase(found);
    }
    if (detail::activeContext && detail::activeContext->api == api) detail::activeContext.reset();
    if (g_hostApi == api) g_hostApi = nullptr;
}

namespace detail {
inline const RCBNProcessHostApi* activeApi() noexcept {
    return activeContext ? activeContext->api : nullptr;
}

template<class T> inline constexpr const char* className = "Instance";
template<> inline constexpr const char* className<System> = "System";
template<> inline constexpr const char* className<Process> = "Process";
template<> inline constexpr const char* className<Workspace> = "Workspace";
template<> inline constexpr const char* className<BaseCube> = "BaseCube";
template<> inline constexpr const char* className<Cube> = "Cube";

template<class T> struct ValueKind;
template<> struct ValueKind<bool> { static constexpr std::uint32_t value = RCBN_PROCESS_VALUE_BOOL; };
template<> struct ValueKind<int> { static constexpr std::uint32_t value = RCBN_PROCESS_VALUE_INT; };
template<> struct ValueKind<float> { static constexpr std::uint32_t value = RCBN_PROCESS_VALUE_FLOAT; };
template<> struct ValueKind<std::string> { static constexpr std::uint32_t value = RCBN_PROCESS_VALUE_STRING; };
template<> struct ValueKind<Vector3> { static constexpr std::uint32_t value = RCBN_PROCESS_VALUE_VECTOR3; };
template<> struct ValueKind<Color4> { static constexpr std::uint32_t value = RCBN_PROCESS_VALUE_COLOR4; };
template<> struct ValueKind<CFrame> { static constexpr std::uint32_t value = RCBN_PROCESS_VALUE_CFRAME; };

template<class T> void encode(const T& value, std::array<double, 16>& numbers, const char*& string) {
    if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, int> || std::is_same_v<T, float>) {
        numbers[0] = value;
    } else if constexpr (std::is_same_v<T, std::string>) {
        string = value.c_str();
    } else if constexpr (std::is_same_v<T, Vector3>) {
        numbers[0] = value.x;
        numbers[1] = value.y;
        numbers[2] = value.z;
    } else if constexpr (std::is_same_v<T, Color4>) {
        numbers[0] = value.r;
        numbers[1] = value.g;
        numbers[2] = value.b;
        numbers[3] = value.a;
    } else if constexpr (std::is_same_v<T, CFrame>) {
        numbers[0] = value.Position.x;
        numbers[1] = value.Position.y;
        numbers[2] = value.Position.z;
        numbers[3] = value.Rotation.x;
        numbers[4] = value.Rotation.y;
        numbers[5] = value.Rotation.z;
        numbers[6] = value.Rotation.w;
    }
}

template<class T> T decode(const std::array<double, 16>& numbers, const std::string& string) {
    if constexpr (std::is_same_v<T, bool>) return numbers[0] != 0.0;
    else if constexpr (std::is_same_v<T, int>) return static_cast<int>(numbers[0]);
    else if constexpr (std::is_same_v<T, float>) return static_cast<float>(numbers[0]);
    else if constexpr (std::is_same_v<T, std::string>) return string;
    else if constexpr (std::is_same_v<T, Vector3>) return Vector3(static_cast<float>(numbers[0]), static_cast<float>(numbers[1]), static_cast<float>(numbers[2]));
    else if constexpr (std::is_same_v<T, Color4>) return Color4(static_cast<float>(numbers[0]), static_cast<float>(numbers[1]), static_cast<float>(numbers[2]), static_cast<float>(numbers[3]));
    else if constexpr (std::is_same_v<T, CFrame>) {
        Quaternion rotation;
        if (!Quaternion::tryFromComponents(static_cast<float>(numbers[6]),
                static_cast<float>(numbers[3]), static_cast<float>(numbers[4]),
                static_cast<float>(numbers[5]), rotation)) {
            throw std::runtime_error("Process host returned an invalid CFrame rotation");
        }
        return CFrame(Vector3(static_cast<float>(numbers[0]), static_cast<float>(numbers[1]), static_cast<float>(numbers[2])), rotation);
    }
}
}

template<class T>
class Property {
public:
    Property() = default;
    Property(std::shared_ptr<detail::HostContext> context, RCBNProcessInstanceHandle handle, const char* name)
        : m_context(std::move(context)), m_handle(handle), m_name(name) {}
    T Get() const {
        std::array<double, 16> numbers{};
        std::size_t required = 0;
        std::uint32_t kind = 0;
        const RCBNProcessHostApi& host = detail::api(m_context);
        if (!host.getProperty || !host.getProperty(host.userdata, m_handle, m_name,
                &kind, numbers.data(), nullptr, 0, &required)) {
            throw std::runtime_error(std::string("Failed to read Process property: ") + m_name);
        }
        if (kind != detail::ValueKind<T>::value) {
            throw std::runtime_error(std::string("Process property type mismatch: ") + m_name);
        }
        std::string string;
        if constexpr (std::is_same_v<T, std::string>) {
            std::vector<char> buffer(required + 1, '\0');
            if (!host.getProperty(host.userdata, m_handle, m_name, &kind, numbers.data(),
                    buffer.data(), buffer.size(), &required)) {
                throw std::runtime_error(std::string("Failed to read Process string property: ") + m_name);
            }
            string.assign(buffer.data(), required);
        }
        return detail::decode<T>(numbers, string);
    }
    void Set(const T& value) const {
        std::array<double, 16> numbers{};
        const char* string = nullptr;
        detail::encode(value, numbers, string);
        const RCBNProcessHostApi& host = detail::api(m_context);
        if (!host.setProperty || !host.setProperty(host.userdata, m_handle, m_name,
                detail::ValueKind<T>::value, numbers.data(), string)) {
            throw std::runtime_error(std::string("Failed to write Process property: ") + m_name);
        }
    }
private:
    std::shared_ptr<detail::HostContext> m_context;
    RCBNProcessInstanceHandle m_handle{};
    const char* m_name = nullptr;
};

class ParentProperty {
public:
    ParentProperty() = default;
    ParentProperty(std::shared_ptr<detail::HostContext> context, RCBNProcessInstanceHandle handle)
        : m_context(std::move(context)), m_handle(handle) {}
    template<class T = Instance> std::optional<InstanceRef<T>> Get() const;
    template<class T> std::optional<InstanceRef<T>> GetAs() const;
    template<class T> void Set(const InstanceRef<T>& parent) const;
    void Clear() const;
private:
    std::shared_ptr<detail::HostContext> m_context;
    RCBNProcessInstanceHandle m_handle{};
};

template<class T>
class ReadOnlyProperty {
public:
    ReadOnlyProperty() = default;
    ReadOnlyProperty(std::shared_ptr<detail::HostContext> context,
        RCBNProcessInstanceHandle handle, const char* name)
        : m_property(std::move(context), handle, name) {}
    T Get() const { return m_property.Get(); }
private:
    Property<T> m_property;
};

class Connection {
public:
    Connection() = default;
    Connection(std::shared_ptr<detail::HostContext> context, std::uint64_t id)
        : m_context(std::move(context)), m_id(id) {}
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    Connection(Connection&& other) noexcept
        : m_context(std::move(other.m_context)), m_id(std::exchange(other.m_id, 0)) {}
    Connection& operator=(Connection&&) = delete;
    bool valid() const noexcept { return m_id != 0 && m_context && m_context->api; }
    bool Disconnect() noexcept {
        if (!valid() || !m_context->api->disconnectHeartbeat) return false;
        const bool result = m_context->api->disconnectHeartbeat(m_context->api->userdata, m_id);
        m_id = 0;
        return result;
    }
private:
    std::shared_ptr<detail::HostContext> m_context;
    std::uint64_t m_id = 0;
};

template<class T>
class InstanceEventSignal {
public:
    InstanceEventSignal() = default;
    InstanceEventSignal(std::shared_ptr<detail::HostContext> context,
        RCBNProcessInstanceHandle instance, std::uint32_t eventKind)
        : m_context(std::move(context)), m_instance(instance), m_eventKind(eventKind) {}
    template<class Callback> Connection Connect(Callback&& callback) const {
        using Function = std::function<void(InstanceRef<T>)>;
        static_assert(std::is_convertible_v<Callback, Function>,
            "This Process signal callback must accept an InstanceRef event argument");
        CallbackState* state = new CallbackState{
            m_context, Function(std::forward<Callback>(callback))};
        const RCBNProcessHostApi* host = m_context ? m_context->api : nullptr;
        std::uint64_t id = 0;
        if (!host || !host->connectInstanceEvent ||
            !host->connectInstanceEvent(host->userdata, m_instance, m_eventKind,
                &invoke, state, &destroy, &id) || id == 0) {
            delete state;
            throw std::runtime_error("Failed to connect Process Instance event listener");
        }
        return Connection(m_context, id);
    }
private:
    struct CallbackState {
        std::shared_ptr<detail::HostContext> context;
        std::function<void(InstanceRef<T>)> function;
    };
    static void invoke(void* userdata, RCBNProcessInstanceHandle eventInstance) {
        const CallbackState* state = static_cast<const CallbackState*>(userdata);
        std::function<void(InstanceRef<T>)> callback = state->function;
        callback(InstanceRef<T>(state->context, eventInstance));
    }
    static void destroy(void* userdata) { delete static_cast<CallbackState*>(userdata); }
    std::shared_ptr<detail::HostContext> m_context;
    RCBNProcessInstanceHandle m_instance{};
    std::uint32_t m_eventKind = 0;
};

template<class... Args>
class Signal {
public:
    Signal() = default;
    explicit Signal(std::shared_ptr<detail::HostContext> context) : m_context(std::move(context)) {}
    template<class Callback> Connection Connect(Callback&& callback) const {
        static_assert(sizeof...(Args) == 1 && (std::is_convertible_v<Callback, std::function<void(Args...)>>),
            "This Process signal callback must accept exactly one event argument");
        using Function = std::function<void(Args...)>;
        Function* stored = new Function(std::forward<Callback>(callback));
        const RCBNProcessHostApi* host = m_context ? m_context->api : nullptr;
        std::uint64_t id = 0;
        if (!host || !host->connectManagedHeartbeat ||
            !host->connectManagedHeartbeat(host->userdata, &invoke, stored, &destroy, &id) || id == 0) {
            delete stored;
            throw std::runtime_error("Failed to connect Process signal listener");
        }
        return Connection(m_context, id);
    }
private:
    static void invoke(void* userdata, double deltaSeconds) {
        const std::function<void(Args...)>* stored =
            static_cast<const std::function<void(Args...)>*>(userdata);
        FunctionCopy(*stored, deltaSeconds);
    }
    static void FunctionCopy(const std::function<void(Args...)>& stored, double deltaSeconds) {
        std::function<void(Args...)> callback = stored;
        callback(static_cast<Args>(deltaSeconds)...);
    }
    static void destroy(void* userdata) { delete static_cast<std::function<void(Args...)>*>(userdata); }
    std::shared_ptr<detail::HostContext> m_context;
};

template<class T>
class InstanceRef {
public:
    InstanceRef() = default;
    bool valid() const noexcept {
        if (!m_context || !m_context->api || m_handle.id == 0) return false;
        const RCBNProcessHostApi* host = m_context->api;
        return !host->isInstanceValid || host->isInstanceValid(host->userdata, m_handle);
    }
    bool isA(std::string_view className) const {
        if (!valid() || !m_context->api->isInstanceOf) return false;
        const std::string copy(className);
        return m_context->api->isInstanceOf(m_context->api->userdata, m_handle, copy.c_str());
    }
    template<class U> std::optional<InstanceRef<U>> As() const {
        if (!isA(detail::className<U>)) {
            return std::nullopt;
        }
        return InstanceRef<U>(m_context, m_handle);
    }
    template<class U = Instance> std::optional<InstanceRef<U>> FindChild(std::string_view childName) const {
        RCBNProcessInstanceHandle result{};
        const std::string copy(childName);
        if (!valid() || !m_context->api->findChild ||
            !m_context->api->findChild(m_context->api->userdata, m_handle,
                copy.c_str(), &result)) {
            return std::nullopt;
        }
        return InstanceRef<U>(m_context, result);
    }
    template<class U> std::optional<InstanceRef<U>> FindChildWithClass(std::string_view childName) const {
        RCBNProcessInstanceHandle result{};
        const std::string copy(childName);
        if (!valid() || !m_context->api->findChildWithClass ||
            !m_context->api->findChildWithClass(m_context->api->userdata, m_handle,
                copy.c_str(), detail::className<U>, &result)) {
            return std::nullopt;
        }
        return InstanceRef<U>(m_context, result);
    }
    static constexpr const char* ClassName() { return detail::className<T>; }
    RCBNProcessInstanceHandle handle() const noexcept { return m_handle; }
protected:
    const std::shared_ptr<detail::HostContext>& context() const noexcept {
        return m_context;
    }
private:
    InstanceRef(std::shared_ptr<detail::HostContext> context, RCBNProcessInstanceHandle handle)
        : m_context(std::move(context)), m_handle(handle), Name(m_context, handle, "Name"), Parent(m_context, handle) {}
public:
    void requireValid() const {
        if (!valid()) throw std::runtime_error("Process Instance reference is no longer valid");
    }
private:
    std::shared_ptr<detail::HostContext> m_context;
    RCBNProcessInstanceHandle m_handle{};
public:
    Property<std::string> Name;
    ParentProperty Parent;
    template<class U> friend class InstanceRef;
    friend class ParentProperty;
    template<class U> friend class InstanceEventSignal;
    template<class U> friend InstanceRef<U> Instance::New();
    friend InstanceRef<Process> GetSelfProcess();
    friend std::optional<InstanceRef<System>> GetSystem();
};

template<class T> std::optional<InstanceRef<T>> ParentProperty::Get() const {
    RCBNProcessInstanceHandle result{};
    const RCBNProcessHostApi& host = detail::api(m_context);
    if (!host.getParent || !host.getParent(host.userdata, m_handle, &result)) {
        return std::nullopt;
    }
    if (!host.isInstanceOf || !host.isInstanceOf(host.userdata, result, detail::className<T>)) {
        return std::nullopt;
    }
    return InstanceRef<T>(m_context, result);
}

template<class T> std::optional<InstanceRef<T>> ParentProperty::GetAs() const {
    std::optional<InstanceRef<Instance>> parent = Get<Instance>();
    if (!parent) return std::nullopt;
    return parent->template As<T>();
}

template<class T> void ParentProperty::Set(const InstanceRef<T>& parent) const {
    if (!parent.valid()) throw std::runtime_error("Cannot set Parent to an invalid Instance reference");
    const RCBNProcessHostApi& host = detail::api(m_context);
    if (!host.setParent || !host.setParent(host.userdata, m_handle, parent.handle())) {
        throw std::runtime_error("Failed to set Process Instance parent");
    }
}

inline void ParentProperty::Clear() const {
    const RCBNProcessHostApi& host = detail::api(m_context);
    if (!host.setParent || !host.setParent(host.userdata, m_handle, {})) {
        throw std::runtime_error("Failed to clear Process Instance parent");
    }
}

template<> class InstanceRef<System> : public InstanceRef<Instance> {
public:
    InstanceRef() = default;
    Signal<double> Heartbeat{context()};
    ReadOnlyProperty<bool> EnableExecutingProcess{context(), handle(), "EnableExecutingProcess"};
    ReadOnlyProperty<bool> UseNetwork{context(), handle(), "UseNetwork"};
    ReadOnlyProperty<float> ProcessWatchdogTimeoutSeconds{context(), handle(), "ProcessWatchdogTimeoutSeconds"};
private:
    InstanceRef(std::shared_ptr<detail::HostContext> context, RCBNProcessInstanceHandle handle)
        : InstanceRef<Instance>(std::move(context), handle) {}
    template<class U> friend InstanceRef<U> Instance::New();
    template<class U> friend class InstanceRef;
    template<class U> friend class InstanceEventSignal;
    friend class ParentProperty;
    friend std::optional<InstanceRef<System>> GetSystem();
};
template<> class InstanceRef<Process> : public InstanceRef<Instance> {
public:
    InstanceRef() = default;
    Property<bool> Enabled{context(), handle(), "Enabled"};
    Property<std::string> Path{context(), handle(), "Path"};
    Property<std::string> DllPath{context(), handle(), "DllPath"};
private:
    InstanceRef(std::shared_ptr<detail::HostContext> context, RCBNProcessInstanceHandle handle)
        : InstanceRef<Instance>(std::move(context), handle) {}
    template<class U> friend InstanceRef<U> Instance::New();
    template<class U> friend class InstanceRef;
    template<class U> friend class InstanceEventSignal;
    friend class ParentProperty;
    friend InstanceRef<Process> GetSelfProcess();
};
template<> class InstanceRef<Workspace> : public InstanceRef<Instance> {
public:
    InstanceRef() = default;
private:
    InstanceRef(std::shared_ptr<detail::HostContext> context, RCBNProcessInstanceHandle handle)
        : InstanceRef<Instance>(std::move(context), handle) {}
    template<class U> friend InstanceRef<U> Instance::New();
    template<class U> friend class InstanceRef;
    template<class U> friend class InstanceEventSignal;
    friend class ParentProperty;
};
template<> class InstanceRef<BaseCube> : public InstanceRef<Instance> {
public:
    InstanceRef() = default;
    InstanceEventSignal<BaseCube> Touched{context(), handle(), RCBN_PROCESS_EVENT_TOUCHED};
    InstanceEventSignal<BaseCube> TouchEnded{context(), handle(), RCBN_PROCESS_EVENT_TOUCH_ENDED};
    Property<CFrame> CFrame{context(), handle(), "CFrame"};
    Property<Vector3> Position{context(), handle(), "Position"};
    Property<Vector3> Size{context(), handle(), "Size"};
    Property<Color4> Color{context(), handle(), "Color"};
    Property<bool> Anchored{context(), handle(), "Anchored"};
    Property<bool> CanCollide{context(), handle(), "CanCollide"};
    Property<bool> CanTouch{context(), handle(), "CanTouch"};
private:
    InstanceRef(std::shared_ptr<detail::HostContext> context, RCBNProcessInstanceHandle handle)
        : InstanceRef<Instance>(std::move(context), handle) {}
    template<class U> friend InstanceRef<U> Instance::New();
    template<class U> friend class InstanceRef;
    template<class U> friend class InstanceEventSignal;
    friend class ParentProperty;
};
template<> class InstanceRef<Cube> : public InstanceRef<BaseCube> {
public:
    InstanceRef() = default;
private:
    InstanceRef(std::shared_ptr<detail::HostContext> context, RCBNProcessInstanceHandle handle)
        : InstanceRef<BaseCube>(std::move(context), handle) {}
    template<class U> friend InstanceRef<U> Instance::New();
    template<class U> friend class InstanceRef;
    template<class U> friend class InstanceEventSignal;
    friend class ParentProperty;
};

template<class T> InstanceRef<T> Instance::New() {
    const RCBNProcessHostApi& host = detail::api(detail::activeContext);
    RCBNProcessInstanceHandle result{};
    if (!host.createInstance || !host.createInstance(host.userdata, detail::className<T>, &result)) {
        throw std::runtime_error(std::string("Failed to create Process Instance of class ") + detail::className<T>);
    }
    return InstanceRef<T>(detail::activeContext, result);
}

inline std::optional<InstanceRef<System>> GetSystem() {
    RCBNProcessInstanceHandle result{};
    const RCBNProcessHostApi& host = detail::api(detail::activeContext);
    if (!host.getSystem || !host.getSystem(host.userdata, &result)) {
        return std::nullopt;
    }
    return InstanceRef<System>(detail::activeContext, result);
}
inline InstanceRef<Process> GetSelfProcess() {
    RCBNProcessInstanceHandle result{};
    const RCBNProcessHostApi& host = detail::api(detail::activeContext);
    if (!host.getSelfProcess || !host.getSelfProcess(host.userdata, &result))
        throw std::runtime_error("The current Process Instance could not be resolved");
    return InstanceRef<Process>(detail::activeContext, result);
}

namespace task {
inline void wait(double seconds) {
    const RCBNProcessHostApi* api = g_hostApi;
    if (!api || !api->wait) throw std::runtime_error("Recubin Process host API is not installed");
    (void)api->wait(api->userdata, seconds);
}
inline bool shouldStop() noexcept { return g_hostApi && g_hostApi->shouldStop && g_hostApi->shouldStop(g_hostApi->userdata); }
}

}

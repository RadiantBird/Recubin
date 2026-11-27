#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

#include <Core/ProcessHandler.hpp>
#include <Instances/System.hpp>

// Adapts the engine Instance tree to the Process host ABI. Handles are
// generation checked and resolve through weak references, so a DLL cannot
// retain an Instance after scene replacement or destruction.
class ProcessInstanceBridge {
public:
    explicit ProcessInstanceBridge(std::shared_ptr<System> system);

    ProcessHandler::InstanceCallbacks callbacks() noexcept;
    void bind(ProcessHandler& handler) noexcept;

private:
    struct Entry {
        std::weak_ptr<Instance> instance;
        std::uint64_t generation = 0;
    };

    std::shared_ptr<System> m_system;
    mutable std::mutex m_mutex;
    std::unordered_map<std::uint64_t, Entry> m_entries;
    // Newly created instances remain owned until SetParent attaches them.
    std::unordered_map<std::uint64_t, std::shared_ptr<Instance>> m_pendingOwned;
    std::unordered_map<Instance*, std::uint64_t> m_ids;
    std::uint64_t m_nextId = 1;
    std::uint64_t m_nextGeneration = 1;

    RCBNProcessInstanceHandle handleFor(const std::shared_ptr<Instance>& instance);
    std::shared_ptr<Instance> resolve(RCBNProcessInstanceHandle handle) const;
    bool findChild(RCBNProcessInstanceHandle parent, const char* name,
                   RCBNProcessInstanceHandle* result);
    bool findChildWithClass(RCBNProcessInstanceHandle parent, const char* name,
                            const char* className, RCBNProcessInstanceHandle* result);
    bool getSystem(RCBNProcessInstanceHandle* result);
    bool isValid(RCBNProcessInstanceHandle handle) const;
    bool isInstanceOf(RCBNProcessInstanceHandle handle, const char* className) const;
    bool getName(RCBNProcessInstanceHandle handle, char* buffer,
                 std::size_t capacity, std::size_t* required) const;
    bool setName(RCBNProcessInstanceHandle handle, const char* name);
    bool createInstance(const char* className, RCBNProcessInstanceHandle* result);
    bool setParent(RCBNProcessInstanceHandle child, RCBNProcessInstanceHandle parent);
    bool getParent(RCBNProcessInstanceHandle child, RCBNProcessInstanceHandle* result);
    bool getProperty(RCBNProcessInstanceHandle handle, const char* name, std::uint32_t* kind,
                     double* values, char* buffer, std::size_t capacity, std::size_t* required);
    bool setProperty(RCBNProcessInstanceHandle handle, const char* name, std::uint32_t kind,
                     const double* values, const char* stringValue);
    bool connectInstanceEvent(RCBNProcessInstanceHandle instance, std::uint32_t eventKind,
                              RCBNProcessInstanceEventCallback callback, void* callbackUserdata,
                              std::uint64_t* subscriptionId);
    bool disconnectInstanceEvent(RCBNProcessInstanceHandle instance, std::uint64_t subscriptionId);
    static bool getParentThunk(void*, RCBNProcessInstanceHandle, RCBNProcessInstanceHandle*);
    static bool getPropertyThunk(void*, RCBNProcessInstanceHandle, const char*, std::uint32_t*, double*, char*, std::size_t, std::size_t*);
    static bool setPropertyThunk(void*, RCBNProcessInstanceHandle, const char*, std::uint32_t, const double*, const char*);
    static bool connectInstanceEventThunk(void*, RCBNProcessInstanceHandle, std::uint32_t,
        RCBNProcessInstanceEventCallback, void*, std::uint64_t*);
    static bool disconnectInstanceEventThunk(void*, RCBNProcessInstanceHandle, std::uint64_t);
    static RCBNProcessInstanceHandle registerInstanceThunk(void*, const std::shared_ptr<Instance>&);

    static bool findChildThunk(void*, RCBNProcessInstanceHandle, const char*, RCBNProcessInstanceHandle*);
    static bool findChildWithClassThunk(void*, RCBNProcessInstanceHandle, const char*, const char*, RCBNProcessInstanceHandle*);
    static bool getSystemThunk(void*, RCBNProcessInstanceHandle*);
    static bool isValidThunk(void*, RCBNProcessInstanceHandle);
    static bool isInstanceOfThunk(void*, RCBNProcessInstanceHandle, const char*);
    static bool getNameThunk(void*, RCBNProcessInstanceHandle, char*, std::size_t, std::size_t*);
    static bool setNameThunk(void*, RCBNProcessInstanceHandle, const char*);
    static bool createInstanceThunk(void*, const char*, RCBNProcessInstanceHandle*);
    static bool setParentThunk(void*, RCBNProcessInstanceHandle, RCBNProcessInstanceHandle);
};


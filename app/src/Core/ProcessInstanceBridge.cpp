#include <Core/ProcessInstanceBridge.hpp>

#include <utility>
#include <algorithm>
#include <Core/SceneLoader.hpp>

ProcessInstanceBridge::ProcessInstanceBridge(std::shared_ptr<System> system)
    : m_system(std::move(system)) {}

ProcessHandler::InstanceCallbacks ProcessInstanceBridge::callbacks() noexcept {
    return ProcessHandler::InstanceCallbacks{
        this,
        &ProcessInstanceBridge::findChildThunk,
        &ProcessInstanceBridge::findChildWithClassThunk,
        &ProcessInstanceBridge::getSystemThunk,
        &ProcessInstanceBridge::isValidThunk,
        &ProcessInstanceBridge::isInstanceOfThunk,
        &ProcessInstanceBridge::getNameThunk,
        &ProcessInstanceBridge::setNameThunk,
        &ProcessInstanceBridge::createInstanceThunk,
        &ProcessInstanceBridge::setParentThunk};
}

void ProcessInstanceBridge::bind(ProcessHandler& handler) noexcept {
    handler.setInstanceCallbacks(callbacks());
}

RCBNProcessInstanceHandle ProcessInstanceBridge::handleFor(
    const std::shared_ptr<Instance>& instance) {
    if (!instance) return {};
    std::scoped_lock lock(m_mutex);
    auto idIt = m_ids.find(instance.get());
    if (idIt != m_ids.end()) {
        const auto entryIt = m_entries.find(idIt->second);
        if (entryIt != m_entries.end()) {
            if (auto current = entryIt->second.instance.lock(); current.get() == instance.get()) {
                return {idIt->second, entryIt->second.generation};
            }
        }
    }
    const std::uint64_t id = m_nextId++;
    const std::uint64_t generation = m_nextGeneration++;
    m_ids[instance.get()] = id;
    m_entries[id] = Entry{instance, generation};
    return {id, generation};
}

std::shared_ptr<Instance> ProcessInstanceBridge::resolve(
    RCBNProcessInstanceHandle handle) const {
    if (handle.id == 0 || handle.generation == 0) return {};
    std::scoped_lock lock(m_mutex);
    const auto it = m_entries.find(handle.id);
    if (it == m_entries.end() || it->second.generation != handle.generation) return {};
    return it->second.instance.lock();
}

bool ProcessInstanceBridge::findChild(RCBNProcessInstanceHandle parent, const char* name,
                                      RCBNProcessInstanceHandle* result) {
    if (!name || !result) return false;
    auto parentInstance = resolve(parent);
    if (!parentInstance) return false;
    const auto it = parentInstance->children.find(name);
    if (it == parentInstance->children.end()) return false;
    *result = handleFor(it->second);
    return true;
}

bool ProcessInstanceBridge::findChildWithClass(RCBNProcessInstanceHandle parent,
                                               const char* name, const char* className,
                                               RCBNProcessInstanceHandle* result) {
    if (!name || !className || !result) return false;
    auto parentInstance = resolve(parent);
    if (!parentInstance) return false;
    const auto it = parentInstance->children.find(name);
    if (it == parentInstance->children.end() || !it->second) return false;
    if (it->second->getClassName() != className) return false;
    *result = handleFor(it->second);
    return true;
}

bool ProcessInstanceBridge::getSystem(RCBNProcessInstanceHandle* result) {
    if (!result || !m_system) return false;
    *result = handleFor(m_system);
    return true;
}

bool ProcessInstanceBridge::isValid(RCBNProcessInstanceHandle handle) const {
    return static_cast<bool>(resolve(handle));
}

bool ProcessInstanceBridge::isInstanceOf(RCBNProcessInstanceHandle handle,
                                          const char* className) const {
    if (!className) return false;
    auto instance = resolve(handle);
    return instance && instance->IsA(className);
}

bool ProcessInstanceBridge::getName(RCBNProcessInstanceHandle handle, char* buffer,
                                    std::size_t capacity, std::size_t* required) const {
    auto instance = resolve(handle);
    if (!instance || !required) return false;
    const std::string& name = instance->Name;
    *required = name.size();
    if (!buffer || capacity == 0) return true;
    if (capacity <= name.size()) return false;
    std::copy(name.begin(), name.end(), buffer);
    buffer[name.size()] = '\0';
    return true;
}

bool ProcessInstanceBridge::setName(RCBNProcessInstanceHandle handle, const char* name) {
    if (!name) return false;
    auto instance = resolve(handle);
    if (!instance) return false;
    instance->renameTo(name);
    return true;
}

bool ProcessInstanceBridge::createInstance(const char* className,
                                           RCBNProcessInstanceHandle* result) {
    if (!className || !result) return false;
    auto instance = SceneLoader::createInstance(className);
    if (!instance) return false;
    // Instance constructors use their base class name by default (for example
    // Cube starts as BaseCube).  The public factory follows Luau's
    // Instance.new convention and gives a newly created object its requested
    // class name before it is attached to a parent.
    instance->renameTo(className);
    *result = handleFor(instance);
    {
        std::scoped_lock lock(m_mutex);
        m_pendingOwned[result->id] = instance;
    }
    return true;
}

bool ProcessInstanceBridge::setParent(RCBNProcessInstanceHandle child,
                                      RCBNProcessInstanceHandle parent) {
    auto childInstance = resolve(child);
    auto parentInstance = resolve(parent);
    if (!childInstance || !parentInstance) return false;
    childInstance->setParent(parentInstance);
    {
        std::scoped_lock lock(m_mutex);
        m_pendingOwned.erase(child.id);
    }
    return true;
}

bool ProcessInstanceBridge::findChildThunk(void* userdata, RCBNProcessInstanceHandle parent,
                                           const char* name, RCBNProcessInstanceHandle* result) {
    auto* self = static_cast<ProcessInstanceBridge*>(userdata);
    return self && self->findChild(parent, name, result);
}

bool ProcessInstanceBridge::findChildWithClassThunk(void* userdata,
                                                    RCBNProcessInstanceHandle parent,
                                                    const char* name, const char* className,
                                                    RCBNProcessInstanceHandle* result) {
    auto* self = static_cast<ProcessInstanceBridge*>(userdata);
    return self && self->findChildWithClass(parent, name, className, result);
}

bool ProcessInstanceBridge::getSystemThunk(void* userdata, RCBNProcessInstanceHandle* result) {
    auto* self = static_cast<ProcessInstanceBridge*>(userdata);
    return self && self->getSystem(result);
}

bool ProcessInstanceBridge::isValidThunk(void* userdata, RCBNProcessInstanceHandle handle) {
    auto* self = static_cast<ProcessInstanceBridge*>(userdata);
    return self && self->isValid(handle);
}

bool ProcessInstanceBridge::isInstanceOfThunk(void* userdata,
                                              RCBNProcessInstanceHandle handle,
                                              const char* className) {
    auto* self = static_cast<ProcessInstanceBridge*>(userdata);
    return self && self->isInstanceOf(handle, className);
}

bool ProcessInstanceBridge::getNameThunk(void* userdata, RCBNProcessInstanceHandle handle,
                                         char* buffer, std::size_t capacity,
                                         std::size_t* required) {
    auto* self = static_cast<ProcessInstanceBridge*>(userdata);
    return self && self->getName(handle, buffer, capacity, required);
}

bool ProcessInstanceBridge::setNameThunk(void* userdata, RCBNProcessInstanceHandle handle,
                                         const char* name) {
    auto* self = static_cast<ProcessInstanceBridge*>(userdata);
    return self && self->setName(handle, name);
}

bool ProcessInstanceBridge::createInstanceThunk(void* userdata, const char* className,
                                                RCBNProcessInstanceHandle* result) {
    auto* self = static_cast<ProcessInstanceBridge*>(userdata);
    return self && self->createInstance(className, result);
}

bool ProcessInstanceBridge::setParentThunk(void* userdata, RCBNProcessInstanceHandle child,
                                           RCBNProcessInstanceHandle parent) {
    auto* self = static_cast<ProcessInstanceBridge*>(userdata);
    return self && self->setParent(child, parent);
}


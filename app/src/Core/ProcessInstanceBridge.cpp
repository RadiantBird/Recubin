#include <Core/ProcessInstanceBridge.hpp>

#include <utility>
#include <algorithm>
#include <cmath>
#include <limits>
#include <Core/SceneLoader.hpp>
#include <Core/PropertyRegistry.hpp>
#include <Instances/BaseCube.hpp>
#include <Util/Logger.hpp>

namespace {
template<class Function>
bool invokeProcessBridge(void* userdata, std::string_view operation, Function&& function) noexcept {
    ProcessInstanceBridge* bridge = static_cast<ProcessInstanceBridge*>(userdata);
    if (!bridge) {
        RCBN_WARN("Process " << operation << " callback has no Instance bridge context");
        return false;
    }
    try {
        return function(*bridge);
    } catch (const std::exception& error) {
        RCBN_WARN("Process " << operation << " callback failed: " << error.what());
    } catch (...) {
        RCBN_WARN("Process " << operation << " callback failed with an unknown exception");
    }
    return false;
}
}

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
        &ProcessInstanceBridge::setParentThunk,
        &ProcessInstanceBridge::getParentThunk,
        &ProcessInstanceBridge::getPropertyThunk,
        &ProcessInstanceBridge::setPropertyThunk,
        &ProcessInstanceBridge::connectInstanceEventThunk,
        &ProcessInstanceBridge::disconnectInstanceEventThunk,
        &ProcessInstanceBridge::registerInstanceThunk};
}

bool ProcessInstanceBridge::connectInstanceEvent(RCBNProcessInstanceHandle handle,
    std::uint32_t eventKind, RCBNProcessInstanceEventCallback callback,
    void* callbackUserdata, std::uint64_t* subscriptionId) {
    if (!callback || !subscriptionId) return false;
    if (eventKind != RCBN_PROCESS_EVENT_TOUCHED && eventKind != RCBN_PROCESS_EVENT_TOUCH_ENDED) return false;
    std::shared_ptr<Instance> instance = resolve(handle);
    std::shared_ptr<BaseCube> cube = std::dynamic_pointer_cast<BaseCube>(instance);
    if (!cube) return false;
    const bool ended = eventKind == RCBN_PROCESS_EVENT_TOUCH_ENDED;
    *subscriptionId = cube->connectNativeTouch(ended,
        [this, callback, callbackUserdata](const std::shared_ptr<BaseCube>& other) {
            callback(callbackUserdata, handleFor(other));
        });
    return *subscriptionId != 0;
}

bool ProcessInstanceBridge::disconnectInstanceEvent(RCBNProcessInstanceHandle handle,
    std::uint64_t subscriptionId) {
    std::shared_ptr<BaseCube> cube = std::dynamic_pointer_cast<BaseCube>(resolve(handle));
    return cube && cube->disconnectNativeTouch(subscriptionId);
}

bool ProcessInstanceBridge::getProperty(RCBNProcessInstanceHandle handle, const char* name,
    std::uint32_t* kind, double* values, char* buffer, std::size_t capacity,
    std::size_t* required) {
    if (!name || !kind || !values || !required) return false;
    auto instance = resolve(handle);
    if (!instance) return false;
    if (std::string_view(name) == "Name") {
        *kind = RCBN_PROCESS_VALUE_STRING;
        std::string currentName = instance->Name;
        *required = currentName.size();
        if (!buffer || capacity == 0) return true;
        if (capacity <= currentName.size()) return false;
        std::copy(currentName.begin(), currentName.end(), buffer);
        buffer[currentName.size()] = '\0';
        return true;
    }
    for (const PropertyDesc* desc : PropertyRegistry::collectApplicableSchema(instance.get())) {
        if (desc->name != name || desc->kind != PropKind::Field || desc->noLuaRead || !desc->get) continue;
        const PropValue value = PropertyRegistry::readValue(instance.get(), *desc);
        *values = {};
        *required = 0;
        if (const auto* item = std::get_if<bool>(&value)) { *kind = RCBN_PROCESS_VALUE_BOOL; values[0] = *item; }
        else if (const auto* item = std::get_if<int>(&value)) { *kind = RCBN_PROCESS_VALUE_INT; values[0] = *item; }
        else if (const auto* item = std::get_if<float>(&value)) { *kind = RCBN_PROCESS_VALUE_FLOAT; values[0] = *item; }
        else if (const auto* item = std::get_if<std::string>(&value)) {
            *kind = RCBN_PROCESS_VALUE_STRING; *required = item->size();
            if (!buffer || capacity == 0) return true;
            if (capacity <= item->size()) return false;
            std::copy(item->begin(), item->end(), buffer);
            buffer[item->size()] = '\0';
        } else if (const auto* item = std::get_if<Vector3>(&value)) {
            *kind = RCBN_PROCESS_VALUE_VECTOR3; values[0] = item->x; values[1] = item->y; values[2] = item->z;
        } else if (const auto* item = std::get_if<Color4>(&value)) {
            *kind = RCBN_PROCESS_VALUE_COLOR4; values[0] = item->r; values[1] = item->g; values[2] = item->b; values[3] = item->a;
        } else if (const auto* item = std::get_if<CFrame>(&value)) {
            *kind = RCBN_PROCESS_VALUE_CFRAME; values[0] = item->Position.x; values[1] = item->Position.y; values[2] = item->Position.z;
            values[3] = item->Rotation.x; values[4] = item->Rotation.y; values[5] = item->Rotation.z; values[6] = item->Rotation.w;
        } else return false;
        return true;
    }
    return false;
}

bool ProcessInstanceBridge::setProperty(RCBNProcessInstanceHandle handle, const char* name,
    std::uint32_t kind, const double* values, const char* stringValue) {
    if (!name || !values) return false;
    auto instance = resolve(handle);
    if (!instance) return false;
    if (std::string_view(name) == "Name") {
        if (kind != RCBN_PROCESS_VALUE_STRING || !stringValue) return false;
        instance->renameTo(stringValue);
        return true;
    }
    for (const PropertyDesc* desc : PropertyRegistry::collectApplicableSchema(instance.get())) {
        if (desc->name != name || desc->kind != PropKind::Field || desc->noLuaWrite ||
            (!desc->set && !desc->luaSet)) continue;
        PropValue value;
        switch (kind) {
        case RCBN_PROCESS_VALUE_BOOL:
            if (values[0] != 0.0 && values[0] != 1.0) return false;
            value = values[0] != 0.0;
            break;
        case RCBN_PROCESS_VALUE_INT:
            if (!std::isfinite(values[0]) || values[0] < std::numeric_limits<int>::min() ||
                values[0] > std::numeric_limits<int>::max()) return false;
            value = static_cast<int>(values[0]);
            break;
        case RCBN_PROCESS_VALUE_FLOAT:
            if (!std::isfinite(values[0]) || std::abs(values[0]) > std::numeric_limits<float>::max()) return false;
            value = static_cast<float>(values[0]);
            break;
        case RCBN_PROCESS_VALUE_STRING: if (!stringValue) return false; value = std::string(stringValue); break;
        case RCBN_PROCESS_VALUE_VECTOR3:
        case RCBN_PROCESS_VALUE_COLOR4:
            for (int index = 0; index < (kind == RCBN_PROCESS_VALUE_VECTOR3 ? 3 : 4); ++index) {
                if (!std::isfinite(values[index]) || std::abs(values[index]) > std::numeric_limits<float>::max()) return false;
            }
            if (kind == RCBN_PROCESS_VALUE_VECTOR3) {
                value = Vector3(static_cast<float>(values[0]), static_cast<float>(values[1]), static_cast<float>(values[2]));
            } else {
                value = Color4(static_cast<float>(values[0]), static_cast<float>(values[1]), static_cast<float>(values[2]), static_cast<float>(values[3]));
            }
            break;
        case RCBN_PROCESS_VALUE_CFRAME: {
            for (int index = 0; index < 7; ++index) {
                if (!std::isfinite(values[index]) || std::abs(values[index]) > std::numeric_limits<float>::max()) return false;
            }
            Quaternion rotation;
            if (!Quaternion::tryFromComponents(static_cast<float>(values[6]),
                    static_cast<float>(values[3]), static_cast<float>(values[4]),
                    static_cast<float>(values[5]), rotation)) return false;
            value = CFrame(Vector3(static_cast<float>(values[0]), static_cast<float>(values[1]), static_cast<float>(values[2])), rotation);
            break;
        }
        default: return false;
        }
        if ((kind == RCBN_PROCESS_VALUE_VECTOR3 && desc->type != PropType::Vec3) ||
            (kind == RCBN_PROCESS_VALUE_COLOR4 && desc->type != PropType::Color4) ||
            (kind == RCBN_PROCESS_VALUE_CFRAME && desc->type != PropType::CFrame) ||
            (kind == RCBN_PROCESS_VALUE_STRING && desc->type != PropType::String) ||
            (kind == RCBN_PROCESS_VALUE_BOOL && desc->type != PropType::Bool) ||
            (kind == RCBN_PROCESS_VALUE_INT && desc->type != PropType::Int && desc->type != PropType::Enum) ||
            (kind == RCBN_PROCESS_VALUE_FLOAT && desc->type != PropType::Float)) return false;
        if (desc->luaSet) desc->luaSet(instance.get(), value);
        else desc->set(instance.get(), value);
        return true;
    }
    return false;
}

bool ProcessInstanceBridge::getParent(RCBNProcessInstanceHandle child, RCBNProcessInstanceHandle* result) {
    if (!result) return false;
    auto instance = resolve(child);
    if (!instance) return false;
    auto parent = instance->Parent.lock();
    if (!parent) return false;
    *result = handleFor(parent);
    return true;
}

bool ProcessInstanceBridge::getParentThunk(void* userdata, RCBNProcessInstanceHandle child, RCBNProcessInstanceHandle* result) {
    return invokeProcessBridge(userdata, "GetParent", [&](ProcessInstanceBridge& bridge) {
        return bridge.getParent(child, result);
    });
}

bool ProcessInstanceBridge::getPropertyThunk(void* userdata, RCBNProcessInstanceHandle handle, const char* name, std::uint32_t* kind, double* values, char* buffer, std::size_t capacity, std::size_t* required) {
    auto* self = static_cast<ProcessInstanceBridge*>(userdata);
    try {
        return self && self->getProperty(handle, name, kind, values, buffer, capacity, required);
    } catch (const std::exception& error) {
        RCBN_WARN("Process property read failed for '" << (name ? name : "<null>")
            << "' (Instance id " << handle.id << "): " << error.what());
        return false;
    } catch (...) {
        RCBN_WARN("Process property read failed with an unknown exception");
        return false;
    }
}

bool ProcessInstanceBridge::setPropertyThunk(void* userdata, RCBNProcessInstanceHandle handle, const char* name, std::uint32_t kind, const double* values, const char* stringValue) {
    auto* self = static_cast<ProcessInstanceBridge*>(userdata);
    try {
        return self && self->setProperty(handle, name, kind, values, stringValue);
    } catch (const std::exception& error) {
        RCBN_WARN("Process property write failed for '" << (name ? name : "<null>")
            << "' (Instance id " << handle.id << "): " << error.what());
        return false;
    } catch (...) {
        RCBN_WARN("Process property write failed with an unknown exception");
        return false;
    }
}

bool ProcessInstanceBridge::connectInstanceEventThunk(void* userdata,
    RCBNProcessInstanceHandle handle, std::uint32_t eventKind,
    RCBNProcessInstanceEventCallback callback, void* callbackUserdata,
    std::uint64_t* subscriptionId) {
    return invokeProcessBridge(userdata, "ConnectInstanceEvent", [&](ProcessInstanceBridge& bridge) {
        return bridge.connectInstanceEvent(handle, eventKind, callback,
            callbackUserdata, subscriptionId);
    });
}

bool ProcessInstanceBridge::disconnectInstanceEventThunk(void* userdata,
    RCBNProcessInstanceHandle handle, std::uint64_t subscriptionId) {
    return invokeProcessBridge(userdata, "DisconnectInstanceEvent", [&](ProcessInstanceBridge& bridge) {
        return bridge.disconnectInstanceEvent(handle, subscriptionId);
    });
}

RCBNProcessInstanceHandle ProcessInstanceBridge::registerInstanceThunk(void* userdata, const std::shared_ptr<Instance>& instance) {
    ProcessInstanceBridge* bridge = static_cast<ProcessInstanceBridge*>(userdata);
    if (!bridge) {
        RCBN_WARN("Process RegisterInstance callback has no Instance bridge context");
        return {};
    }
    try {
        return bridge->handleFor(instance);
    } catch (const std::exception& error) {
        RCBN_WARN("Process RegisterInstance callback failed: " << error.what());
    } catch (...) {
        RCBN_WARN("Process RegisterInstance callback failed with an unknown exception");
    }
    return {};
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
    if (!childInstance) return false;
    if (parent.id == 0 && parent.generation == 0) {
        childInstance->setParent(nullptr);
        return childInstance->Parent.expired();
    }
    auto parentInstance = resolve(parent);
    if (!parentInstance) return false;
    childInstance->setParent(parentInstance);
    if (childInstance->Parent.lock() != parentInstance) {
        return false;
    }
    {
        std::scoped_lock lock(m_mutex);
        m_pendingOwned.erase(child.id);
    }
    return true;
}

bool ProcessInstanceBridge::findChildThunk(void* userdata, RCBNProcessInstanceHandle parent,
                                           const char* name, RCBNProcessInstanceHandle* result) {
    return invokeProcessBridge(userdata, "FindChild", [&](ProcessInstanceBridge& bridge) {
        return bridge.findChild(parent, name, result);
    });
}

bool ProcessInstanceBridge::findChildWithClassThunk(void* userdata,
                                                    RCBNProcessInstanceHandle parent,
                                                    const char* name, const char* className,
                                                    RCBNProcessInstanceHandle* result) {
    return invokeProcessBridge(userdata, "FindChildWithClass", [&](ProcessInstanceBridge& bridge) {
        return bridge.findChildWithClass(parent, name, className, result);
    });
}

bool ProcessInstanceBridge::getSystemThunk(void* userdata, RCBNProcessInstanceHandle* result) {
    return invokeProcessBridge(userdata, "GetSystem", [&](ProcessInstanceBridge& bridge) {
        return bridge.getSystem(result);
    });
}

bool ProcessInstanceBridge::isValidThunk(void* userdata, RCBNProcessInstanceHandle handle) {
    return invokeProcessBridge(userdata, "IsInstanceValid", [&](ProcessInstanceBridge& bridge) {
        return bridge.isValid(handle);
    });
}

bool ProcessInstanceBridge::isInstanceOfThunk(void* userdata,
                                              RCBNProcessInstanceHandle handle,
                                              const char* className) {
    return invokeProcessBridge(userdata, "IsInstanceOf", [&](ProcessInstanceBridge& bridge) {
        return bridge.isInstanceOf(handle, className);
    });
}

bool ProcessInstanceBridge::getNameThunk(void* userdata, RCBNProcessInstanceHandle handle,
                                         char* buffer, std::size_t capacity,
                                         std::size_t* required) {
    return invokeProcessBridge(userdata, "GetName", [&](ProcessInstanceBridge& bridge) {
        return bridge.getName(handle, buffer, capacity, required);
    });
}

bool ProcessInstanceBridge::setNameThunk(void* userdata, RCBNProcessInstanceHandle handle,
                                         const char* name) {
    return invokeProcessBridge(userdata, "SetName", [&](ProcessInstanceBridge& bridge) {
        return bridge.setName(handle, name);
    });
}

bool ProcessInstanceBridge::createInstanceThunk(void* userdata, const char* className,
                                                RCBNProcessInstanceHandle* result) {
    return invokeProcessBridge(userdata, "CreateInstance", [&](ProcessInstanceBridge& bridge) {
        return bridge.createInstance(className, result);
    });
}

bool ProcessInstanceBridge::setParentThunk(void* userdata, RCBNProcessInstanceHandle child,
                                           RCBNProcessInstanceHandle parent) {
    return invokeProcessBridge(userdata, "SetParent", [&](ProcessInstanceBridge& bridge) {
        return bridge.setParent(child, parent);
    });
}


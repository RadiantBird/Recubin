#include <Instances/Instance.hpp>
#include <Instances/System.hpp>
#include <Instances/Workspace.hpp>
#include <Core/PropertyRegistry.hpp>
#include <algorithm>
#include <cmath>
#include <Util/Logger.hpp>

static const bool s_systemRegistered = []{
    using namespace PropertyRegistry;
    auto watchdogTimeout = field<&System::ProcessWatchdogTimeoutSeconds>(
        "ProcessWatchdogTimeoutSeconds");
    watchdogTimeout.set = [](Instance* object, const PropValue& value) {
        const float candidate = std::get<float>(value);
        if (!std::isfinite(candidate) || candidate <= 0.0f) {
            RCBN_WARN("System.ProcessWatchdogTimeoutSeconds must be finite and positive; value rejected");
            return;
        }
        static_cast<System*>(object)->ProcessWatchdogTimeoutSeconds = candidate;
    };
    watchdogTimeout.luaReadOnly().group("General");
    registerClass("System", {
        field<&System::BaseResolution>("BaseResolution", 1.f, 16384.f, 1.f).luaReadOnly(),
        field<&System::UseNetwork>("UseNetwork"),
        field<&System::NetworkInterpolationDelay>("NetworkInterpolationDelay", 0.0f, 1.0f, 0.01f)
            .luaReadOnly(),
        field<&System::EnableIOAPI>("EnableIOAPI").luaReadOnly(),
        field<&System::EnableIPCAPI>("EnableIPCAPI").luaReadOnly(),
        field<&System::EnableExternalFileAccess>("EnableExternalFileAccess").luaReadOnly(),
        field<&System::EnableExecutingProcess>("EnableExecutingProcess")
            .luaReadOnly().group("General"),
        std::move(watchdogTimeout),
        field<&System::AutoSpawnPlayerCharacter>("AutoSpawnPlayerCharacter")
            .luaReadOnly().group("Runtime"),
        field<&System::MaxClonesPerFrame>("MaxClonesPerFrame", 0.0f, 1000000.0f, 1.0f).luaReadOnly(),
        field<&System::MaxRestartsPerFrame>("MaxRestartsPerFrame", 0.0f, 1000000.0f, 1.0f).luaReadOnly(),
        field<&System::MaxTasksPerFrame>("MaxTasksPerFrame", 0.0f, 1000000.0f, 1.0f).luaReadOnly(),
        field<&System::ScriptLoopTimeoutSeconds>("ScriptLoopTimeoutSeconds", 0.0f, 60.0f, 0.05f).luaReadOnly(),
        enumProp<&System::DefaultCameraMode>("DefaultCameraMode", {
            {"Character", static_cast<int>(System::CameraMode::Character)},
            {"Free", static_cast<int>(System::CameraMode::Free)},
            {"Program", static_cast<int>(System::CameraMode::Program)},
        }, true).luaReadOnly().group("Runtime"),
        field<&System::LockAspectRatio>("LockAspectRatio")
            .luaReadOnly()
            .serializeIf([](const Instance* o) { return static_cast<const System*>(o)->LockAspectRatio; }),
        field<&System::AllowControlModeSwitch>("AllowControlModeSwitch")
            .luaReadOnly()
            .serializeIf([](const Instance* o) { return static_cast<const System*>(o)->AllowControlModeSwitch; }),
        field<&System::AllowWorkspaceSwitch>("AllowWorkspaceSwitch")
            .luaReadOnly()
            .serializeIf([](const Instance* o) { return static_cast<const System*>(o)->AllowWorkspaceSwitch; }),
        field<&System::UseFixedResolution>("UseFixedResolution")
            .luaHidden().group("Editor")
            .serializeIf([](const Instance* o) { return !static_cast<const System*>(o)->UseFixedResolution; }),
        []() {
            PropertyDesc collisionGroups = custom(
                "CollisionGroups",
                PropType::String,
                [](Instance*) { return PropValue(std::string()); },
                [](Instance*, const PropValue&) {});
            collisionGroups.noEditor().luaHidden();
            collisionGroups.serializeIf([](const Instance* object) {
                const auto* system = static_cast<const System*>(object);
                const auto& groups = system->getCollisionGroups();
                return groups.size() > 1 ||
                    (groups.size() == 1 && groups.front() != "Default") ||
                    !system->areCollisionGroupsCollidable("Default", "Default");
            });
            collisionGroups.yamlReadWith([](Instance* object, const YAML::Node& value) {
                auto* system = static_cast<System*>(object);
                if (!value.IsMap()) {
                    RCBN_WARN("System.CollisionGroups must be a YAML map");
                    return true;
                }
                if (const YAML::Node groupsNode = value["Groups"]; groupsNode && groupsNode.IsSequence()) {
                    for (const YAML::Node& item : groupsNode) {
                        if (!item.IsScalar()) {
                            RCBN_WARN("Ignoring non-scalar CollisionGroups entry");
                            continue;
                        }
                        const std::string groupName = item.as<std::string>();
                        if (!groupName.empty()) system->registerCollisionGroup(groupName);
                    }
                }
                if (const YAML::Node pairsNode = value["NonCollidablePairs"]; pairsNode && pairsNode.IsSequence()) {
                    for (const YAML::Node& pairNode : pairsNode) {
                        if (!pairNode.IsSequence() || pairNode.size() != 2 ||
                            !pairNode[0].IsScalar() || !pairNode[1].IsScalar()) {
                            RCBN_WARN("Ignoring invalid CollisionGroups NonCollidablePairs entry");
                            continue;
                        }
                        system->setCollisionGroupsCollidable(
                            pairNode[0].as<std::string>(),
                            pairNode[1].as<std::string>(),
                            false);
                    }
                }
                return true;
            });
            collisionGroups.yamlWriteWith([](YAML::Emitter& out, const Instance* object, std::string_view yamlKey) {
                const auto* system = static_cast<const System*>(object);
                const auto snapshot = system->buildCollisionGroupSnapshot();
                out << YAML::Key << std::string(yamlKey) << YAML::Value << YAML::BeginMap;
                out << YAML::Key << "Groups" << YAML::Value << YAML::Flow << YAML::BeginSeq;
                for (const std::string& group : system->getCollisionGroups()) {
                    out << group;
                }
                out << YAML::EndSeq;
                if (snapshot && !snapshot->nonCollidablePairs.empty()) {
                    out << YAML::Key << "NonCollidablePairs" << YAML::Value << YAML::BeginSeq;
                    for (const auto& [first, second] : snapshot->nonCollidablePairs) {
                        out << YAML::Flow << YAML::BeginSeq << first << second << YAML::EndSeq;
                    }
                    out << YAML::EndSeq;
                }
                out << YAML::EndMap;
            });
            return collisionGroups;
        }(),
    });
    return true;
}();

System::System(std::string name) : Instance(name) {
    Heartbeat = std::make_shared<RCBNScriptSignal>();
    NetworkRoleChanged = std::make_shared<RCBNScriptSignal>();
}

void System::registerScript(const std::shared_ptr<Instance>& s) {
    scripts.push_back(s);
}

void System::unregisterScript(const std::shared_ptr<Instance>& s) {
    scripts.erase(std::remove(scripts.begin(), scripts.end(), s), scripts.end());
}
std::string System::getClassName() {
    return "System";
}
bool System::IsA(std::string className) {
    return className == "System" || Instance::IsA(className);
}

void System::addChild(std::shared_ptr<Instance> child) {
    if (child && child->IsA("Workspace")) {
        std::string baseName = child->Name.empty() ? "Workspace" : child->Name;
        std::string uniqueName = baseName;
        int suffix = 1;
        while (children.count(uniqueName) > 0 && children[uniqueName] != child) {
            uniqueName = baseName + std::to_string(suffix++);
        }
        child->Name = uniqueName;
    }
    Instance::addChild(std::move(child));
}

void System::setProperty(const std::string& name, const YAML::Node& value) {
    if (name == "DefaultCameraMode" && value.IsScalar()) {
        const std::string mode = value.as<std::string>();
        if (mode != "Character" && mode != "Free" && mode != "Program") {
            RCBN_WARN("Unknown DefaultCameraMode '" << mode
                      << "'; falling back to Character");
            DefaultCameraMode = CameraMode::Character;
            return;
        }
    }
    if (name == "ProcessWatchdogTimeoutSeconds") {
        const float candidate = value.as<float>();
        if (!std::isfinite(candidate) || candidate <= 0.0f) {
            RCBN_WARN("System.ProcessWatchdogTimeoutSeconds must be finite and positive; value rejected");
            return;
        }
        ProcessWatchdogTimeoutSeconds = candidate;
        return;
    }
    if (PropertyRegistry::loadProperty(this, "System", name, value)) return;
    if (name == "MaxClonesPerFrame")        { MaxClonesPerFrame        = value.as<int>();   return; }
    if (name == "MaxRestartsPerFrame")      { MaxRestartsPerFrame      = value.as<int>();   return; }
    if (name == "MaxTasksPerFrame")         { MaxTasksPerFrame         = value.as<int>();   return; }
    if (name == "ScriptLoopTimeoutSeconds") { ScriptLoopTimeoutSeconds = value.as<float>(); return; }
    Instance::setProperty(name, value);
}

namespace {
std::string normalizeCollisionGroupName(const std::string& name) {
    return name.empty() ? std::string("Default") : name;
}

std::pair<std::string, std::string> normalizeCollisionGroupPair(
    const std::string& first,
    const std::string& second) {
    const std::string a = normalizeCollisionGroupName(first);
    const std::string b = normalizeCollisionGroupName(second);
    return a <= b ? std::pair{a, b} : std::pair{b, a};
}
}

bool System::registerCollisionGroup(const std::string& name) {
    if (name.empty()) {
        RCBN_WARN("System::registerCollisionGroup rejected empty group name");
        return false;
    }
    if (std::find(m_collisionGroups.begin(), m_collisionGroups.end(), name) !=
        m_collisionGroups.end()) {
        return false;
    }
    m_collisionGroups.push_back(name);
    ++m_collisionGroupRevision;
    return true;
}

bool System::unregisterCollisionGroup(const std::string& name) {
    if (name.empty() || name == "Default") {
        if (name == "Default") {
            RCBN_WARN("System::unregisterCollisionGroup cannot remove Default group");
        }
        return false;
    }
    const auto it = std::find(m_collisionGroups.begin(), m_collisionGroups.end(), name);
    if (it == m_collisionGroups.end()) {
        return false;
    }
    m_collisionGroups.erase(it);
    for (auto pairIt = m_nonCollidableGroupPairs.begin();
         pairIt != m_nonCollidableGroupPairs.end();) {
        if (pairIt->first == name || pairIt->second == name) {
            pairIt = m_nonCollidableGroupPairs.erase(pairIt);
        } else {
            ++pairIt;
        }
    }
    ++m_collisionGroupRevision;
    return true;
}

bool System::setCollisionGroupsCollidable(
    const std::string& groupA,
    const std::string& groupB,
    bool collidable) {
    const std::string a = normalizeCollisionGroupName(groupA);
    const std::string b = normalizeCollisionGroupName(groupB);
    registerCollisionGroup(a);
    registerCollisionGroup(b);
    const auto pair = normalizeCollisionGroupPair(a, b);
    bool changed = false;
    if (collidable) {
        changed = m_nonCollidableGroupPairs.erase(pair) > 0;
    } else {
        changed = m_nonCollidableGroupPairs.insert(pair).second;
    }
    if (changed) {
        ++m_collisionGroupRevision;
    }
    return changed;
}

bool System::areCollisionGroupsCollidable(
    const std::string& groupA,
    const std::string& groupB) const {
    const auto pair = normalizeCollisionGroupPair(groupA, groupB);
    return !m_nonCollidableGroupPairs.contains(pair);
}

std::shared_ptr<const CollisionGroupSnapshot> System::buildCollisionGroupSnapshot() const {
    auto snapshot = std::make_shared<CollisionGroupSnapshot>();
    snapshot->nonCollidablePairs = m_nonCollidableGroupPairs;
    return snapshot;
}

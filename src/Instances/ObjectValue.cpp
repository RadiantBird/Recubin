#include <include/Instances/ObjectValue.hpp>
#include <include/Core/LuauEngine.hpp>

ObjectValue::ObjectValue() : Named<ObjectValue, ValueBase>("ObjectValue") {}

bool ObjectValue::IsA(std::string className) {
    if (className == "ObjectValue") return true;
    return ValueBase::IsA(className);
}

void ObjectValue::setProperty(const std::string& name, const YAML::Node& value) {
    if (name == "Value") {
        m_targetPathName = value.as<std::string>();
        // ベストエフォート即時解決（NoCollision::setProperty と同様、実体解決は
        // SceneLoader の全ツリー走査フェーズが別途行う）
        Instance* top = this;
        for (auto p = Parent.lock(); p; p = p->Parent.lock()) top = p.get();
        if (Instance* found = top->getChildByPath(m_targetPathName))
            m_target = found->shared_from_this();
    } else {
        ValueBase::setProperty(name, value);
    }
}

std::shared_ptr<Instance> ObjectValue::clone() const {
    auto copy = std::make_shared<ObjectValue>();
    copy->Name = Name;
    copy->m_targetPathName = m_targetPathName;
    copy->m_target = m_target;   // 一旦は元の参照を指す（remapClonedInstances が張り替える）
    for (auto const& [n, child] : children)
        copy->addChild(child->clone());
    return copy;
}

void ObjectValue::remapClonedInstances(const CloneRemap& map) {
    if (auto t = m_target.lock()) {
        auto it = map.find(t.get());
        if (it != map.end()) m_target = it->second;
    }
}

std::shared_ptr<Instance> ObjectValue::getTarget() const {
    return m_target.lock();
}

void ObjectValue::setTarget(std::shared_ptr<Instance> target) {
    m_target = target;
    if (target) {
        // getFullPath()はルート自身の名前を含む絶対パスを返すため、getChildByPath()の
        // 起点(ルート自身)から辿る形式（ルート名を含まない）に合わせて自前で計算する。
        Instance* top = target.get();
        for (auto p = target->Parent.lock(); p; p = p->Parent.lock()) top = p.get();
        m_targetPathName = target->getPathUpTo(top);
    } else {
        m_targetPathName = "";
    }
    if (Changed) Changed->fire([target](lua_State* L) {
        if (target) LuauEngine::pushInstance(L, target);
        else        lua_pushnil(L);
        return 1;
    });
}

void ObjectValue::resolveTarget(std::shared_ptr<Instance> target) {
    m_target = target;
}

void ObjectValue::refreshRefName() {
    if (auto t = m_target.lock(); t && !m_targetPathName.empty()) {
        Instance* top = t.get();
        for (auto p = t->Parent.lock(); p; p = p->Parent.lock()) top = p.get();
        m_targetPathName = t->getPathUpTo(top);
    }
}

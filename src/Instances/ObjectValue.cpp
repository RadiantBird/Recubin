#include <include/Instances/ObjectValue.hpp>
#include <include/Core/LuauEngine.hpp>
#include <include/Core/PropertyRegistry.hpp>

// Value: 対象Instanceへのパス（最上位祖先からの相対）。保存はパス文字列が正で、解決済みの対象は
// キャッシュ。クローンは解決済みの対象を引き継ぎ、内部参照はremapClonedInstancesが張り替える。
static const bool s_objectValueRegistered = [] {
    using namespace PropertyRegistry;
    PropertyDesc value = custom("Value", PropType::String,
        [](Instance* object) -> PropValue {
            auto* objectValue = static_cast<ObjectValue*>(object);
            objectValue->refreshRefName();
            return objectValue->m_targetPathName;
        },
        [](Instance* object, const PropValue& v) {
            static_cast<ObjectValue*>(object)->setTargetPathName(std::get<std::string>(v));
        });
    value.noEditor();  // 専用の参照ピッカー(drawObjectValueRef)が担当する
    value.copyStateWith([](const Instance* source, Instance* destination) {
        static_cast<ObjectValue*>(destination)->resolveTarget(
            static_cast<const ObjectValue*>(source)->getTarget());
    });
    registerClass("ObjectValue", "ValueBase", {value});
    return true;
}();

ObjectValue::ObjectValue() : Named<ObjectValue, ValueBase>("ObjectValue") {}

bool ObjectValue::IsA(std::string className) {
    if (className == "ObjectValue") return true;
    return ValueBase::IsA(className);
}

void ObjectValue::setProperty(const std::string& name, const YAML::Node& value) {
    if (PropertyRegistry::loadProperty(this, "ObjectValue", name, value)) return;
    ValueBase::setProperty(name, value);
}

void ObjectValue::setTargetPathName(const std::string& path) {
    m_targetPathName = path;
    // ベストエフォート即時解決（NoCollision::setProperty と同様、実体解決は
    // SceneLoader の全ツリー走査フェーズが別途行う）
    Instance* top = this;
    for (auto p = Parent.lock(); p; p = p->Parent.lock()) top = p.get();
    if (Instance* found = top->getChildByPath(m_targetPathName))
        m_target = found->shared_from_this();
}

std::shared_ptr<Instance> ObjectValue::clone() const {
    auto copy = std::make_shared<ObjectValue>();
    copy->Name = Name;
    // 一旦は元の対象を指す（remapClonedInstances が張り替える）
    PropertyRegistry::cloneFields(this, copy.get(), "ObjectValue");
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

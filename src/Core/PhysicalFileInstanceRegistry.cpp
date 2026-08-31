#include <Core/PhysicalFileInstanceRegistry.hpp>

#include <Util/AssetPath.hpp>

#include <algorithm>

namespace {

std::vector<PhysicalFileInstanceType>& mutableTypes() {
    static std::vector<PhysicalFileInstanceType> values;
    return values;
}

void ensureBaseSchemaRegistered() {
    static const bool registered = [] {
        using namespace PropertyRegistry;
        registerClass("PhysicalFileInstance", {
            custom(
                "Path", PropType::String,
                [](Instance* instance) -> PropValue {
                    return static_cast<PhysicalFileInstance*>(instance)->Path;
                },
                [](Instance* instance, const PropValue& value) {
                    static_cast<PhysicalFileInstance*>(instance)->Path =
                        AssetPath::normalize(std::get<std::string>(value));
                })
                .yaml("ContentPath")
                .omitEmpty()
                .luaReadOnly()
                .filePath({}, {}),
        });
        return true;
    }();
    (void)registered;
}

void ensureBuiltinsRegistered() {
    static const bool registered = [] {
        ensureBaseSchemaRegistered();
        PhysicalFileInstanceRegistry::registerTextFileType();
#define RCBN_FILE_INSTANCE(ClassName, Kind, Category, DialogLabel, Filter)       \
        PhysicalFileInstanceRegistry::registerType(                             \
            PhysicalFileInstanceType{                                           \
                #ClassName, PhysicalFileKind::Kind,                              \
                PhysicalFileInsertCategory::Category, DialogLabel, Filter,       \
                [] { return std::make_shared<ClassName>(); }});
#include <Instances/PhysicalFileInstances.def>
#undef RCBN_FILE_INSTANCE
        return true;
    }();
    (void)registered;
}

} // namespace

namespace PhysicalFileInstanceRegistry {

const PhysicalFileInstanceType* find(std::string_view className) {
    ensureBuiltinsRegistered();
    const auto& values = mutableTypes();
    const auto it = std::find_if(values.begin(), values.end(),
        [className](const PhysicalFileInstanceType& type) {
            return type.className == className;
        });
    return it == values.end() ? nullptr : &*it;
}

std::shared_ptr<PhysicalFileInstance> create(std::string_view className) {
    const PhysicalFileInstanceType* type = find(className);
    return type && type->factory ? type->factory() : nullptr;
}

const std::vector<PhysicalFileInstanceType>& types() {
    ensureBuiltinsRegistered();
    return mutableTypes();
}

bool registerType(PhysicalFileInstanceType type,
                  std::vector<PropertyDesc> additionalProperties) {
    ensureBaseSchemaRegistered();
    if (type.className.empty() || !type.factory) return false;
    auto& values = mutableTypes();
    if (std::any_of(values.begin(), values.end(),
            [&type](const PhysicalFileInstanceType& value) {
                return value.className == type.className;
            })) return false;
    PropertyRegistry::registerClass(
        type.className, "PhysicalFileInstance", std::move(additionalProperties));
    values.push_back(std::move(type));
    return true;
}

} // namespace PhysicalFileInstanceRegistry

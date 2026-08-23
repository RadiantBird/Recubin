#include <Instances/TextFile.hpp>
#include <Core/PhysicalFileInstanceRegistry.hpp>
#include <Core/PropertyRegistry.hpp>
#include <Util/UUID.hpp>

TextFile::TextFile() : PhysicalFileInstance("TextFile"), StorageId(RecubinUUID::generate()) {
    Name = "TextFile";
}

bool TextFile::IsA(std::string className) {
    return className == "TextFile" || PhysicalFileInstance::IsA(std::move(className));
}

void TextFile::setProperty(const std::string& name, const YAML::Node& value) {
    if (name == "StorageId") {
        if (value.IsScalar()) {
            const auto candidate = value.as<std::string>();
            if (RecubinUUID::isValid(candidate)) StorageId = candidate;
        }
        return;
    }
    PhysicalFileInstance::setProperty(name, value);
}

std::shared_ptr<Instance> TextFile::clone() const {
    auto copy = std::make_shared<TextFile>();
    copy->Path = Path;
    copy->Name = Name;
    copy->StorageId = RecubinUUID::generate();
    return copy;
}

void PhysicalFileInstanceRegistry::registerTextFileType() {
    static const bool registered = [] {
    using namespace PropertyRegistry;
    PhysicalFileInstanceRegistry::registerType(
        PhysicalFileInstanceType{"TextFile", PhysicalFileKind::Text,
            PhysicalFileInsertCategory::Other, "Text file (*.txt)", "*.txt",
            [] { return std::make_shared<TextFile>(); }, false},
        {custom("StorageId", PropType::String,
            [](Instance* i) -> PropValue { return static_cast<TextFile*>(i)->StorageId; },
            [](Instance* i, const PropValue& v) { static_cast<TextFile*>(i)->StorageId = std::get<std::string>(v); })
             .noEditor().luaReadOnly(),
         });
    return true;
    }();
    (void)registered;
}

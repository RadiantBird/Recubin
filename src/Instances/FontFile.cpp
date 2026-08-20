#include <include/Instances/FontFile.hpp>
#include <include/Core/PropertyRegistry.hpp>

static const bool s_fontFileRegistered = []{
    using namespace PropertyRegistry;
    registerClass("FontFile", {
        field<&FontFile::Path>("Path").yaml("ContentPath").omitEmpty().luaReadOnly(),
    });
    return true;
}();

FontFile::FontFile() : Instance("FontFile") {}

bool FontFile::IsA(std::string className) {
    if (className == "FontFile") {
        return true;
    }
    return Instance::IsA(className);
}

void FontFile::setProperty(const std::string& name, const YAML::Node& value) {
    if (PropertyRegistry::loadProperty(this, "FontFile", name, value)) return;
    Instance::setProperty(name, value);
}

std::shared_ptr<Instance> FontFile::clone() const {
    auto copy = std::make_shared<FontFile>();
    PropertyRegistry::cloneFields(this, copy.get(), "FontFile");
    return copy;
}

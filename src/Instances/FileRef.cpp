#include <Instances/FileRef.hpp>
#include <include/Core/PropertyRegistry.hpp>

static const bool s_fileRefRegistered = []{
    using namespace PropertyRegistry;
    registerClass("FileRef", {
        // YAML 上は ContentPath（Packager が追跡するキー）。Lua からは読取専用。
        field<&FileRef::Path>("Path").yaml("ContentPath").omitEmpty().luaReadOnly(),
    });
    return true;
}();

FileRef::FileRef() : Instance("FileRef") {}

bool FileRef::IsA(std::string name) {
    if (name == "FileRef") return true;
    return Instance::IsA(name);
}

void FileRef::setProperty(const std::string& name, const YAML::Node& value) {
    if (PropertyRegistry::loadProperty(this, "FileRef", name, value)) return;
    Instance::setProperty(name, value);
}

std::shared_ptr<Instance> FileRef::clone() const {
    auto copy = std::make_shared<FileRef>();
    PropertyRegistry::cloneFields(this, copy.get(), "FileRef");
    return copy;
}

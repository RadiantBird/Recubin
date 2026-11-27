#include <Instances/Process.hpp>

#include <Core/PhysicalFileInstanceRegistry.hpp>
#include <Core/PropertyRegistry.hpp>
#include <Util/AssetPath.hpp>

Process::Process() : PhysicalFileInstance("Process") {
    Name = "Process";
}

bool Process::IsA(std::string className) {
    return className == "Process" || PhysicalFileInstance::IsA(std::move(className));
}

std::shared_ptr<Instance> Process::clone() const {
    auto copy = std::make_shared<Process>();
    clonePhysicalFileFieldsTo(*copy, "Process");
    return copy;
}

void PhysicalFileInstanceRegistry::registerProcessType() {
    static const bool registered = [] {
        using namespace PropertyRegistry;
        registerType(
            PhysicalFileInstanceType{
                "Process", PhysicalFileKind::Generic, PhysicalFileInsertCategory::Other,
                "C++ Process source (*.cpp;*.cc;*.cxx)", "*.cpp;*.cc;*.cxx",
                [] { return std::make_shared<Process>(); }, false},
            {
                field<&Process::Enabled>("Enabled"),
                custom(
                    "DllPath", PropType::String,
                    [](Instance* instance) -> PropValue {
                        return static_cast<Process*>(instance)->DllPath;
                    },
                    [](Instance* instance, const PropValue& value) {
                        static_cast<Process*>(instance)->DllPath =
                            AssetPath::normalize(std::get<std::string>(value));
                    })
                    .yaml("DllPath")
                    .omitEmpty()
                    .filePath("Dynamic library (*.dll)", "*.dll"),
            });
        return true;
    }();
    (void)registered;
}

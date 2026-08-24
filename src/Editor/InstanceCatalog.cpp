#include <Editor/InstanceCatalog.hpp>
#include <algorithm>
#include <cctype>

namespace {
using C = InstanceCategory;
const std::vector<InstanceCatalogEntry> kEntries = {
    {"Cube", C::Cubes}, {"Cylinder", C::Cubes}, {"TriangularPrism", C::Cubes},
    {"Truss", C::Cubes}, {"Seat", C::Cubes}, {"Sphere", C::Cubes},
    {"MeshCube", C::Cubes}, {"LiquidCube", C::Cubes}, {"SpawnLocation", C::Cubes},
    {"Sound", C::Effects}, {"Decal", C::Effects}, {"Texture", C::Effects},
    {"SurfaceMark", C::Effects}, {"PostEffect", C::Effects}, {"ParticleEmitter", C::Effects},
    {"Highlight", C::Effects}, {"Workspace", C::Environment}, {"Weather", C::Environment}, {"Skybox", C::Environment},
    {"Lighting", C::Environment}, {"PointLight", C::Environment}, {"SpotLight", C::Environment},
    {"Sun", C::Environment}, {"Moon", C::Environment}, {"Terrain", C::Environment},
    {"TextLabel", C::Gui}, {"TextButton", C::Gui}, {"SurfaceGui", C::Gui},
    {"Canvas", C::Gui}, {"BillboardGui", C::Gui}, {"ProximityPrompt", C::Gui},
    {"ImageLabel", C::Gui}, {"ImageButton", C::Gui},
    {"Weld", C::Physics}, {"Motor", C::Physics}, {"Rod", C::Physics},
    {"BallSocket", C::Physics}, {"NoCollision", C::Physics}, {"Rope", C::Physics},
    {"Attachment", C::Physics}, {"Force", C::Physics},
    {"IntValue", C::Values}, {"BoolValue", C::Values}, {"NumberValue", C::Values},
    {"Vector3Value", C::Values}, {"Color4Value", C::Values}, {"CFrameValue", C::Values},
    {"QuaternionValue", C::Values}, {"ObjectValue", C::Values},
    {"Folder", C::Container}, {"Model", C::Container}, {"Tool", C::Container},
    {"StarterCharacter", C::Container}, {"TextFile", C::File}, {"FileRef", C::File},
    {"FontFile", C::File}, {"Script", C::Script}, {"LocalScript", C::Script},
    {"ModuleScript", C::Script}, {"AppImage", C::Other}, {"Animation", C::Other},
    {"Humanoid", C::Other}, {"SignalEvent", C::Other}
};
std::string lower(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}
}

namespace InstanceCatalog {
const std::vector<InstanceCatalogEntry>& entries() { return kEntries; }

std::vector<InstanceCatalogEntry> search(std::string_view query) {
    const std::string needle = lower(query);
    std::vector<InstanceCatalogEntry> result;
    for (const auto& entry : kEntries)
        if (needle.empty() || lower(entry.className).find(needle) != std::string::npos)
            result.push_back(entry);
    return result;
}

std::shared_ptr<Instance> create(std::string_view className) {
    return SceneLoader::createInstance(std::string(className));
}

const char* categoryLabel(InstanceCategory category) {
    switch (category) {
    case C::Cubes: return "Cubes"; case C::Effects: return "Effects";
    case C::Environment: return "Environment"; case C::Gui: return "GUI";
    case C::Physics: return "Physics"; case C::Values: return "Values";
    case C::Container: return "Container"; case C::File: return "File";
    case C::Script: return "Script"; default: return "Other";
    }
}
}

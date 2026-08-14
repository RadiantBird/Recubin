#include <Instances/SpawnLocation.hpp>
#include <Core/PropertyRegistry.hpp>

static const bool s_spawnLocationRegistered = [] {
    using namespace PropertyRegistry;
    registerClass("SpawnLocation", {
        field<&SpawnLocation::Enabled>("Enabled").group("Spawn"),
    });
    return true;
}();

SpawnLocation::SpawnLocation(Vector3 position)
    : Named<SpawnLocation, Cube>(
          position, Vector3(8, 1, 8), Cube::defaultTextureID) {
    Name = "SpawnLocation";
    Color = Color4(1, 1, 1, 1);
    Anchored = true;
    CanCollide = true;
}

void SpawnLocation::setProperty(
    const std::string& name, const YAML::Node& value) {
    if (PropertyRegistry::loadProperty(this, "SpawnLocation", name, value)) return;
    Cube::setProperty(name, value);
}

std::shared_ptr<Instance> SpawnLocation::clone() const {
    auto copy = std::make_shared<SpawnLocation>(Position);
    PropertyRegistry::cloneFields(this, copy.get(), "SpawnLocation");
    cloneBaseCubeStateAndChildrenTo(copy);
    return copy;
}

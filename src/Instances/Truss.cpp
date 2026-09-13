#include <Instances/Truss.hpp>
#include <include/Core/PropertyRegistry.hpp>

static const bool s_trussRegistered = [] {
    PropertyRegistry::registerClass("Truss", "Cube", {});
    return true;
}();

std::shared_ptr<Instance> Truss::clone() const {
    auto copy = std::make_shared<Truss>(this->getPosition(), this->Size, Cube::defaultTextureID);
    cloneBaseCubeStateAndChildrenTo(copy);
    return copy;
}

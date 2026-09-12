#include <Instances/Truss.hpp>

std::shared_ptr<Instance> Truss::clone() const {
    auto copy = std::make_shared<Truss>(this->getPosition(), this->Size, Cube::defaultTextureID);
    cloneBaseCubeStateAndChildrenTo(copy);
    return copy;
}

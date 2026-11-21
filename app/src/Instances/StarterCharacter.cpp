#include <Instances/StarterCharacter.hpp>

std::shared_ptr<Instance> StarterCharacter::clone() const {
    auto copy = std::make_shared<StarterCharacter>();
    cloneInto(*copy);
    return copy;
}

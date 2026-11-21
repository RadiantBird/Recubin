#include <Instances/Users.hpp>

std::shared_ptr<Instance> Users::clone() const {
    auto copy = std::make_shared<Users>();
    copy->Name = Name;
    for (const auto& [name, child] : children) {
        (void)name;
        copy->addChild(child->clone());
    }
    return copy;
}

#include <Instances/Folder.hpp>

std::shared_ptr<Instance> Folder::clone() const {
    auto copy = std::make_shared<Folder>();
    copy->Name = Name;
    for (const auto& [name, child] : children) {
        (void)name;
        copy->addChild(child->clone());
    }
    return copy;
}

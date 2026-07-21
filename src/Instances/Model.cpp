#include <include/Instances/Model.hpp>

std::shared_ptr<Instance> Model::clone() const {
    auto copy = std::make_shared<Model>(Position, Size);
    copy->Name = Name;
    copy->cframe = cframe;

    for (auto const& [name, child] : children) {
        copy->addChild(child->clone());
    }

    return copy;
}

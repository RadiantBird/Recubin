#include <Instances/Truss.hpp>

bool Truss::IsA(std::string className) {
    if (className == "Truss") return true;
    return Cube::IsA(className);
}

std::shared_ptr<Instance> Truss::clone() const {
    auto copy = std::make_shared<Truss>(this->Position, this->Size, Cube::defaultTextureID);
    copy->Name       = this->Name;
    copy->Color      = this->Color;
    copy->Anchored   = this->Anchored;
    copy->CanCollide = this->CanCollide;
    copy->cframe     = this->cframe;
    for (auto const& [name, child] : children) {
        copy->addChild(child->clone());
    }
    return copy;
}

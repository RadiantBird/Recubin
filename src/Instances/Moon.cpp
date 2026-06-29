#include <Instances/Moon.hpp>
#include <include/Core/PropertyRegistry.hpp>

static const bool s_moonRegistered = []{
    using namespace PropertyRegistry;
    registerClass("Moon", "Sphere", {});
    return true;
}();

Moon::Moon() : Named<Moon, Sphere>(Vector3(0, 0, 0), Vector3(150.0f, 150.0f, 150.0f)) {
    Name = "Moon";
    Anchored   = true;
    CanCollide = false;
    CastShadow = false;
    Unlit      = true;
    Color      = Color4(0.9f, 0.9f, 1.0f, 1.0f);
}

bool Moon::IsA(std::string className) {
    if (className == "Moon") return true;
    return Sphere::IsA(className);
}

std::shared_ptr<Instance> Moon::clone() const {
    auto copy = std::make_shared<Moon>();
    copy->Name      = this->Name;
    copy->Color     = this->Color;
    copy->Anchored  = this->Anchored;
    copy->CanCollide = this->CanCollide;
    copy->cframe    = this->cframe;
    for (auto const& [n, child] : children) {
        copy->addChild(child->clone());
    }
    return copy;
}

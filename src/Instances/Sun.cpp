#include <Instances/Sun.hpp>
#include <include/Core/PropertyRegistry.hpp>

static const bool s_sunRegistered = []{
    using namespace PropertyRegistry;
    registerClass("Sun", "Sphere", {
        field<&Sun::Angle>("Angle", 0.0f, 360.0f, 1.0f),
    });
    return true;
}();

Sun::Sun() : Named<Sun, Sphere>(Vector3(0, 0, 0), Vector3(200.0f, 200.0f, 200.0f)) {
    Name = "Sun";
    Anchored   = true;
    CanCollide = false;
    CastShadow = false;
    Unlit      = true;
    Color      = Color4(1.0f, 0.95f, 0.8f, 1.0f);
}

bool Sun::IsA(std::string className) {
    if (className == "Sun") return true;
    return Sphere::IsA(className);
}

void Sun::setProperty(const std::string& name, const YAML::Node& value) {
    if (PropertyRegistry::loadProperty(this, "Sun", name, value)) return;
    Sphere::setProperty(name, value);
}

std::shared_ptr<Instance> Sun::clone() const {
    auto copy = std::make_shared<Sun>();
    copy->Name      = this->Name;
    copy->Angle     = this->Angle;
    copy->Color     = this->Color;
    copy->Anchored  = this->Anchored;
    copy->CanCollide = this->CanCollide;
    copy->Locked    = this->Locked;
    copy->cframe    = this->cframe;
    for (auto const& [n, child] : children) {
        copy->addChild(child->clone());
    }
    return copy;
}

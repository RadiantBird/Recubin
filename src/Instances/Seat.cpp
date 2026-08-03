#include <Instances/Seat.hpp>
#include <include/Core/PropertyRegistry.hpp>

// Steer/ThrottleはSeat.Occupantが毎フレーム書き込むライブ入力値であり、
// System.BaseResolutionのような設計値ではないためYAMLには保存しない(noYaml)
static const bool s_seatRegistered = []{
    using namespace PropertyRegistry;
    registerClass("Seat", {
        field<&Seat::Steer>   ("Steer",    -1, 1).luaReadOnly().noYaml(),
        field<&Seat::Throttle>("Throttle", -1, 1).luaReadOnly().noYaml(),
    });
    return true;
}();

bool Seat::IsA(std::string className) {
    if (className == "Seat") return true;
    return Cube::IsA(className);
}

std::shared_ptr<Instance> Seat::clone() const {
    auto copy = std::make_shared<Seat>(this->Position, this->Size, Cube::defaultTextureID);
    copy->Name       = this->Name;
    copy->Color      = this->Color;
    copy->Anchored   = this->Anchored;
    copy->CanCollide = this->CanCollide;
    copy->cframe     = this->cframe;
    copy->material     = this->material;
    copy->MassDensity  = this->MassDensity;
    copy->LockFlags = this->LockFlags;
    copy->CollisionDetection = this->CollisionDetection;
    copy->CastShadow   = this->CastShadow;
    copy->Unlit        = this->Unlit;
    copy->UseTriplanar = this->UseTriplanar;
    copy->TextureScale = this->TextureScale;
    // m_occupantは複製しない(新規シートは空席から始まる)
    for (auto const& [name, child] : children) {
        copy->addChild(child->clone());
    }
    return copy;
}

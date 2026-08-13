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

std::shared_ptr<Instance> Seat::clone() const {
    auto copy = std::make_shared<Seat>(this->Position, this->Size, Cube::defaultTextureID);
    // m_occupantは複製しない(新規シートは空席から始まる)
    cloneBaseCubeStateAndChildrenTo(copy);
    return copy;
}

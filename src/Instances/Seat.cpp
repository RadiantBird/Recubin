#include <Instances/Seat.hpp>
#include <Instances/Humanoid.hpp>
#include <Instances/Workspace.hpp>
#include <include/Core/Physics.hpp>
#include <include/Core/PropertyRegistry.hpp>
#include <Util/Logger.hpp>

// Steer/ThrottleはSeat.Occupantが毎フレーム書き込むライブ入力値であり、
// System.BaseResolutionのような設計値ではないためYAMLには保存しない(noYaml)
static const bool s_seatRegistered = []{
    using namespace PropertyRegistry;
    registerClass("Seat", "Cube", {
        field<&Seat::Steer>   ("Steer",    -1, 1).luaReadOnly().noYaml().noClone().noEditor(),
        field<&Seat::Throttle>("Throttle", -1, 1).luaReadOnly().noYaml().noClone().noEditor(),
    });
    return true;
}();

Seat::Seat(Vector3 Pos, Vector3 Sz, unsigned int textureID)
    : Named<Seat, Cube>(Pos, Sz, textureID) {
    setNativeTouchObserved(true);
}

void Seat::sit(std::shared_ptr<Humanoid> humanoid) {
    Instance* wsRaw = findFirstAncestorWorkspace();
    Physics* physics = wsRaw ? static_cast<Workspace*>(wsRaw)->getPhysicsEngine() : nullptr;
    if (!physics) {
        RCBN_ERROR("Seat::sit: \"" << Name << "\" is not in a Workspace with physics");
        return;
    }

    if (!humanoid) {
        if (auto occupant = m_occupant.lock()) occupant->standUp(physics);
        clearOccupant();
        return;
    }

    if (auto occupant = m_occupant.lock()) {
        if (occupant != humanoid)
            RCBN_WARN("Seat::sit: \"" << Name << "\" is already occupied");
        return;
    }

    if (humanoid->isSeated()) humanoid->standUp(physics);
    humanoid->sitOn(std::static_pointer_cast<Seat>(shared_from_this()), physics);
}

void Seat::onNativeTouched(BaseCube& other) {
    if (isOccupied()) return;
    if (other.Name != "LeftLeg" && other.Name != "RightLeg") return;

    auto parent = other.Parent.lock();
    if (!parent) return;
    for (const auto& [name, child] : parent->getChildren()) {
        auto humanoid = std::dynamic_pointer_cast<Humanoid>(child);
        if (!humanoid) continue;
        if (!humanoid->isSeated()) sit(humanoid);
        return;
    }
}

std::shared_ptr<Instance> Seat::clone() const {
    auto copy = std::make_shared<Seat>(this->getPosition(), this->Size, Cube::defaultTextureID);
    // m_occupantは複製しない(新規シートは空席から始まる)
    PropertyRegistry::cloneFields(this, copy.get(), "Seat");
    cloneBaseCubeStateAndChildrenTo(copy);
    return copy;
}

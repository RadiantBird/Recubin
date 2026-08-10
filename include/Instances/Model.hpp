#pragma once
#include <Instances/Spatial.hpp>
#include <Instances/BaseCube.hpp>
#include <cstdint>

class Model : public Spatial {
    friend class BaseCube;

private:
    std::uint32_t m_characterCollisionGroup = 0;
    void refreshCharacterCollisionGroup();

public:
    Model(Vector3 Pos = {0,0,0}, Vector3 Sz = {1,1,1}) : Spatial(Pos, Sz, "Model") {}
    std::string getClassName() { return "Model"; }
    bool IsA(std::string name) override {
        if (name == "Model") return true;
        return Spatial::IsA(name);
    }
    void onChildrenChanged() override;
    std::shared_ptr<Instance> clone() const override;
};

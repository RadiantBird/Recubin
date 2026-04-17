#include <Instances/Spatial.hpp>
#include <Instances/BaseCube.hpp>

class Model : public Spatial {
public:
    Model(Vector3 Pos, Vector3 Sz) : Spatial(Pos, Sz, "Model") {}
    std::string GetClassName() override { return "Model"; }
    bool IsA(std::string name) override {
        if (name == "Model") return true;
        return Spatial::IsA(name);
    }
};
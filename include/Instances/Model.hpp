#pragma once
#include <Instances/Spatial.hpp>
#include <Instances/BaseCube.hpp>

class Model : public Spatial {
public:
    Model(Vector3 Pos = {0,0,0}, Vector3 Sz = {1,1,1}) : Spatial(Pos, Sz, "Model") {}
    std::string getClassName() { return "Model"; }
    bool IsA(std::string name) override {
        if (name == "Model") return true;
        return Spatial::IsA(name);
    }
};
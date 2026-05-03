#pragma once
#include <Instances/Instance.hpp>

class System : public Instance {
    public:
        System(string name = "System");
        string GetClassName() override;
        bool IsA(std::string className) override;
};
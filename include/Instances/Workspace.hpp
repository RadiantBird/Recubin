#pragma once

#include <include/Math/Vector3.hpp>

#include <include/Instances/Instance.hpp>

class Workspace : public Instance {
    public:
        Vector3 Gravity = Vector3(0, -9.8f, 0);
        Workspace () : Instance("Workspace") {
            this->ClassName = "Workspace";
        }
};
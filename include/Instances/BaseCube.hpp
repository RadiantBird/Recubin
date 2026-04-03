#pragma once

#include <include/Util/Color4.hpp>
#include <include/Math/Vector3.hpp>

#include <include/Instances/Instance.hpp>

class BaseCube : public Instance {
    public:
        bool Anchored = false;
        bool CanCollide = true;
        bool CanTouch = true;

        Vector3 Position;
        Vector3 Size;
        Color4 Color;

        BaseCube (Vector3 Pos, Vector3 Sz):
            Instance("BaseCube"),
            Position(Pos),
            Size(Sz),
            Color(1, 1, 1, 1) {
            this->ClassName = "BaseCube";
        }
};
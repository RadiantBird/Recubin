#include "Instance.hpp"
#include "Math/Vector3.hpp"
#include "Math/Quaternion.hpp"

class Spatial : public Instance {
public:
    Vector3 Position;
    Vector3 Size;
    Quaternion Rotation;

    Spatial(Vector3 Pos, Vector3 Sz, std::string name) : Instance(name), Position(Pos), Size(Sz), Rotation(Quaternion()) {}
    std::string GetClassName() override { return "Spatial"; }
    virtual bool IsA(std::string name) override;

};
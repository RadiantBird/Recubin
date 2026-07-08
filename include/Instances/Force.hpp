#pragma once
#include <Instances/Instance.hpp>
#include <Math/Vector3.hpp>

// 親(直上)の BaseCube に毎フレーム力を加えるインスタンス。BaseCube の子として置く。
// - Torque=false: Value をベクトル(力)として扱う / true: 角力(トルク)として扱う
// - MaintainVelocity=false: 毎ステップ addForce/addTorque で加算する(重力など他の力と合成)
//   MaintainVelocity=true:  Value を目標速度(線速度/角速度)として毎ステップ維持する
// - Value はワールド座標系
class Force : public Instance {
public:
    bool    Enabled          = true;
    bool    Torque           = false;
    bool    MaintainVelocity = false;
    Vector3 Value            = {0.0f, 0.0f, 0.0f};

    Force() : Instance("Force") {}
    std::string getClassName() override { return "Force"; }
    bool IsA(std::string name) override {
        if (name == "Force") return true;
        return Instance::IsA(name);
    }
    std::shared_ptr<Instance> clone() const override;
    void setProperty(const std::string& name, const YAML::Node& value) override;
};

#include "include/Instances/Spatial.hpp"

CFrame Spatial::getWorldCFrame() const {
    auto par = Parent.lock();
    if (par && par->IsA("Spatial")) {
        // 親もワールド CFrame を持つ → 親ワールド * 自ローカル で合成
        return static_cast<Spatial*>(par.get())->getWorldCFrame() * cframe;
    }
    return cframe; // Workspace 直下 or 親なし → ローカル = ワールド
}

bool Spatial::IsA(std::string className) {
    if (className == "Spatial") {
        return true;
    }
    return Instance::IsA(className);
}

void Spatial::setProperty(const std::string& name, const YAML::Node& value) {
    if (name == "Position" || name == "Size") {
        if (value.IsSequence() && value.size() == 3) {
            Vector3 vec;
            vec.x = value[0].as<float>();
            vec.y = value[1].as<float>();
            vec.z = value[2].as<float>();
            if (name == "Position") this->Position = vec;
            else                     this->Size     = vec;
        }
    } else if (name == "Rotation") {
        // [x, y, z, w] 形式で保存された Quaternion を読み込む
        if (value.IsSequence() && value.size() == 4) {
            cframe.Rotation = Quaternion(
                value[3].as<float>(),  // w
                value[0].as<float>(),  // x
                value[1].as<float>(),  // y
                value[2].as<float>()   // z
            );
        }
    } else {
        Instance::setProperty(name, value);
    }
}

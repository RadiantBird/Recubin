#pragma once
#include <Instances/Instance.hpp>

class Folder : public Instance {
public:
    Folder() : Instance("Folder") {}

    std::string getClassName() override { return "Folder"; }

    bool IsA(std::string name) override {
        if (name == "Folder") return true;
        return Instance::IsA(name);
    }

    // 基底のclone()はInstanceを返してしまうため、Folderとして複製する。
    std::shared_ptr<Instance> clone() const override;
};

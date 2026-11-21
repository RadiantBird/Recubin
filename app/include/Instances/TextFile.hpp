#pragma once
#include <Instances/PhysicalFileInstance.hpp>
#include <string>

// Persistent text storage backed by the runtime file service. ContentPath is
// the packaged seed; StorageId identifies the user's mutable copy.
class TextFile final : public PhysicalFileInstance {
public:
    std::string StorageId;

    TextFile();
    std::string getClassName() override { return "TextFile"; }
    bool IsA(std::string className) override;
    void setProperty(const std::string& name, const YAML::Node& value) override;
    std::shared_ptr<Instance> clone() const override;
};

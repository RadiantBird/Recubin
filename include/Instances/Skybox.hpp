#pragma once
#include <include/Instances/Cube.hpp>
#include <string>

class Skybox : public Cube {
public:
    // 6面のテクスチャパス (Right, Left, Top, Bottom, Front, Back)
    std::string skyboxPaths[6];

    Skybox();
    virtual ~Skybox() = default;

    virtual std::string getClassName() override;
    virtual bool IsA(std::string className) override;
    virtual void setProperty(const std::string& name, const YAML::Node& value) override;
    virtual std::shared_ptr<Instance> clone() const override;

    void setSkyboxPath(int faceIndex, const std::string& path);
};

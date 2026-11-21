#pragma once
#include <include/Instances/Cube.hpp>
#include <include/Instances/Named.hpp>
#include <string>

class Skybox : public Named<Skybox, Cube> {
public:
    static constexpr const char* ClassName = "Skybox";

    // 6面のテクスチャパス (Right, Left, Top, Bottom, Front, Back)
    std::string skyboxPaths[6];

    Skybox();
    virtual ~Skybox() = default;

    virtual void setProperty(const std::string& name, const YAML::Node& value) override;
    virtual std::shared_ptr<Instance> clone() const override;

    void setSkyboxPath(int faceIndex, const std::string& path);
};

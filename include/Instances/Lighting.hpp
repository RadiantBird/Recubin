#pragma once
#include <include/Instances/Instance.hpp>
#include <include/Math/Vector3.hpp>
#include <string>

class Lighting : public Instance {
public:
    Vector3      lightDir   = Vector3(1.0f, -1.0f, -1.0f);
    float        brightness = 1.0f;

    // Skybox 6面 順: Right(+X), Left(-X), Top(+Y), Bottom(-Y), Front(+Z), Back(-Z)
    std::string  skyboxPaths[6];
    unsigned int cubemapTexture = 0;
    bool         skyboxDirty    = false;

    Lighting();
    virtual ~Lighting() = default;

    virtual std::string GetClassName() override;
    virtual bool IsA(std::string className) override;
    virtual void setProperty(const std::string& name, const YAML::Node& value) override;
    virtual std::shared_ptr<Instance> clone() const override;

    void setSkyboxPath(int faceIndex, const std::string& path);
};

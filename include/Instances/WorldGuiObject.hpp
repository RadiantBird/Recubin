#pragma once
#include <Instances/Instance.hpp>
#include <Math/Vector2.hpp>
#include <Util/Color4.hpp>

class WorldGuiObject : public Instance {
public:
    Vector2 Size            = {200.f, 100.f};
    Norm    NormType        = Norm::Pixel;
    bool    Active          = true;
    bool    Visible         = true;
    Color4  BackgroundColor = {1.f, 1.f, 1.f, 1.f};
    int     ZIndex          = 0;

    float getTransparency() const    { return 1.f - BackgroundColor.a; }
    void  setTransparency(float t)   { BackgroundColor.a = 1.f - t; }

    explicit WorldGuiObject(std::string className);
    virtual std::string GetClassName() override;
    virtual bool IsA(std::string name) override;
    virtual void setProperty(const std::string& name, const YAML::Node& val) override;
};

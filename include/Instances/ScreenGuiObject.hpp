#pragma once
#include <Instances/Instance.hpp>
#include <Math/Vector2.hpp>
#include <Util/Color4.hpp>

class ScreenGuiObject : public Instance {
public:
    bool    Active          = true;
    Vector2 Position;
    Vector2 Size            = {100.f, 40.f};
    Norm    NormType        = Norm::Pixel;
    bool    Visible         = true;
    Color4  BackgroundColor = {1.f, 1.f, 1.f, 1.f};
    int     ZIndex          = 0;

    float getTransparency() const    { return 1.f - BackgroundColor.a; }
    void  setTransparency(float t)   { BackgroundColor.a = 1.f - t; }

    explicit ScreenGuiObject(std::string className);
    virtual std::string getClassName() override;
    virtual bool IsA(std::string name) override;
    virtual void setProperty(const std::string& name, const YAML::Node& val) override;
};

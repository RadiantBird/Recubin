#pragma once
#include <include/Instances/Instance.hpp>
#include <string>

enum class PostEffectKind {
    None = 0,
    CRT = 1,
    Posterization = 2,
    Pixelize = 3
};

class PostEffect : public Instance {
public:
    bool           Enabled   = true;
    PostEffectKind Type      = PostEffectKind::CRT;
    int            ZIndex    = 0;
    float          Intensity = 1.0f;  // 元画像とのブレンド比率 (0..1)
    float          Param1    = 8.0f;  // ScanlineCount(CRT) / Levels(Posterization) / PixelSize(Pixelize)
    float          Param2    = 0.15f; // CurveAmount(CRT)。他タイプでは未使用

    PostEffect();
    virtual ~PostEffect() = default;

    virtual std::string getClassName() override;
    virtual bool IsA(std::string className) override;
    virtual void setProperty(const std::string& name, const YAML::Node& value) override;
    virtual std::shared_ptr<Instance> clone() const override;
};

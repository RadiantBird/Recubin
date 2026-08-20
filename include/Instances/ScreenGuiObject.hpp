#pragma once
#include <Instances/GuiObject.hpp>
#include <Math/Vector2.hpp>
#include <Core/RCBNScriptSignal.hpp>

enum class SystemFont {
    Default = 0,
    DotGothic16 = 1,
};

class ScreenGuiObject : public GuiObject {
public:
    Vector2 Position;
    float   FontSize        = 0.f;   // 0 = 既定サイズ。文字を持つ GUI(TextLabel/TextButton)のみ使用
    bool    UseFontFile      = false;
    SystemFont Font          = SystemFont::Default;
    std::string FontFile;            // FontFile の Workspace 相対参照

    // マウスカーソルが要素内に入った瞬間に発火する（Roblox の MouseEnter 相当）
    std::shared_ptr<RCBNScriptSignal> Hovered;
    bool    m_wasHovered    = false; // エッジ判定用（Luau 非公開）

    explicit ScreenGuiObject(std::string className);
    virtual bool IsA(std::string name) override;
    virtual void setProperty(const std::string& name, const YAML::Node& val) override;
};

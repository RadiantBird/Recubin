#pragma once
#include <Instances/Instance.hpp>
#include <Instances/GuiContent.hpp>
#include <Math/Vector2.hpp>
#include <Util/Color4.hpp>

// GuiObject — 画面GUI(ScreenGuiObject)とワールドGUI(WorldGuiObject)の共通基底。
// 表示/配置の共通プロパティを一元管理する（ファクトリ非登録の抽象基底）。
class GuiObject : public Instance {
public:
    bool    Active          = true;
    Vector2 Size            = {100.f, 40.f};   // 既定は ScreenGui 系の値。WorldGuiObject はコンストラクタで {200,100} に上書き
    Norm    NormType        = Norm::Pixel;
    bool    Visible         = true;
    Color4  BackgroundColor = {1.f, 1.f, 1.f, 1.f};
    int     ZIndex          = 0;

    float getTransparency() const    { return 1.f - BackgroundColor.a; }
    void  setTransparency(float t)   { BackgroundColor.a = 1.f - t; }

    // HasA コンポーネント問い合わせ（保持する派生だけが override で返す。描画・エディターの分岐一本化用）
    virtual TextContent*  textContent()  { return nullptr; }
    virtual ImageContent* imageContent() { return nullptr; }

    explicit GuiObject(std::string className);
    virtual bool IsA(std::string name) override;
    virtual void setProperty(const std::string& name, const YAML::Node& val) override;
};

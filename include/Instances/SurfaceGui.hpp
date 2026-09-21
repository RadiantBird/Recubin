#pragma once
#include <Instances/WorldGuiObject.hpp>
#include <Instances/Named.hpp>
#include <Instances/Decal.hpp>
#include <cstdint>

class SurfaceGui : public Named<SurfaceGui, WorldGuiObject> {
public:
    static constexpr const char* ClassName = "SurfaceGui";

    Face face = Face::Front;

    // FBO ベイク用リソース（Renderer が管理）
    unsigned int m_fboID = 0;
    unsigned int m_texID = 0;
    int          m_texW  = 0;
    int          m_texH  = 0;

    // 最後にFBOへ正常にベイクした描画内容の署名。
    // ランタイムキャッシュであり、clone/YAMLの永続化対象には含めない。
    std::uint64_t m_bakedContentSignature = 0;
    bool          m_hasBakedContentSignature = false;

    // SurfaceGuiベイクで実際に描画される直接子かを共通判定する。
    static bool isRenderableDirectChild(Instance* child);
    bool hasRenderableDirectChild();
    // Cube面の通常描画を、ベイク済みSurfaceGuiで実際に上書きするか。
    // Rendererのインスタンシング判定とCube::draw()で同じ条件を使う。
    bool contributesBakedVisualOverride();
    // 自身の背景と直接子が生成するピクセルだけから署名を作る。
    std::uint64_t computeRenderContentSignature(float defaultFontSize);

    SurfaceGui();
    ~SurfaceGui();
    bool IsA(std::string name) override;
    void setProperty(const std::string& name, const YAML::Node& val) override;
    std::shared_ptr<Instance> clone() const override;
};

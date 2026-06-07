#pragma once
#include <Instances/WorldGuiObject.hpp>
#include <Instances/Decal.hpp>

class SurfaceGui : public WorldGuiObject {
public:
    Face face = Face::Front;

    // FBO ベイク用リソース（Renderer が管理）
    unsigned int m_fboID = 0;
    unsigned int m_texID = 0;
    int          m_texW  = 0;
    int          m_texH  = 0;

    SurfaceGui();
    ~SurfaceGui();
    std::string getClassName() override;
    bool IsA(std::string name) override;
    void setProperty(const std::string& name, const YAML::Node& val) override;
    std::shared_ptr<Instance> clone() const override;
};

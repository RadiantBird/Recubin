#pragma once
#include <include/Instances/Instance.hpp>
#include <Util/Color4.hpp>
#include <Math/Vector2.hpp>
#include <string>

enum class Face {
    Front = 0,
    Back = 1,
    Top = 2,
    Bottom = 3,
    Right = 4,
    Left = 5
};

// Decalの貼り付けモード。MeshCube配下でのみ意味を持つ
// (Cube等のプリミティブは常にFace貼りで、このモードは無視される)
enum class DecalMode {
    UV = 0,   // UVCenter/UVRadiusによる円形ブレンド
    Face = 1  // 指定Faceのローカル空間ボックス射影で面全体に長方形貼り
};

class Decal : public Instance {
public:
    unsigned int TextureID = 0;
    Face         face      = Face::Front;
    std::string  texturePath;
    Color4       Color;
    // MeshCube配下でのみ意味を持つUV空間配置(Face配置のプリミティブでは無視される)
    Vector2      UVCenter = Vector2(0.5f, 0.5f);
    float        UVRadius = 0.15f;
    // MeshCube配下でのみ意味を持つ貼り付けモード(Face配置のプリミティブでは無視される)
    DecalMode    Mode = DecalMode::UV;

    Decal(unsigned int textureID = 0, Face targetFace = Face::Front);
    virtual ~Decal();

    virtual std::string getClassName() override;
    virtual bool IsA(std::string className) override;
    virtual void setProperty(const std::string& name, const YAML::Node& value) override;
    virtual std::shared_ptr<Instance> clone() const override;

    void setFace(Face f);
    // パスからテクスチャを読み込み texturePath/TextureID を更新する（FileRef.Source 経由でも使用）
    void setTexturePath(const std::string& path);
};

#pragma once
#include <include/Instances/Instance.hpp>
#include <include/Util/Material.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class BaseCube;

// ==================================================================
//  MaterialInstance (ClassName = "Material")
//
//  MaterialServiceの子。PBR値・物理特性・Conductiveを持ち、BaseCubeが
//  Material プロパティ(パス文字列)で参照する。子のDecal/Textureは参照元
//  BaseCubeの6面へ投影される。
//
//  C++側のstruct Material(include/Util/Material.hpp)と衝突するため、
//  クラス名はMaterialInstanceとし、ClassNameのみ"Material"にしている。
// ==================================================================
class MaterialInstance : public Instance {
public:
    // PBR (metallic-roughness)
    float Metallic    = 0.0f;
    float Roughness   = 0.5f;
    float Reflectance = 0.5f;

    // 物理特性。StaticFrictionはBox3Dでは使われないため、エディタ/Luaからは
    // 非表示（将来の実装に備えて保持・YAML保存する）。
    float StaticFriction  = 0.5f;
    float DynamicFriction = 0.5f;
    float Restitution     = 0.1f;
    float MassDensity     = 1.0f;

    // 雷の標的になるか（従来のMaterialType::Metal判定の置き換え）。
    bool Conductive = false;

    MaterialInstance();
    virtual ~MaterialInstance() = default;

    virtual std::string getClassName() override;
    virtual bool IsA(std::string className) override;
    virtual void setProperty(const std::string& name, const YAML::Node& value) override;
    virtual std::shared_ptr<Instance> clone() const override;

    // baseの物理Materialのfriction/restitutionをこのMaterialの値で上書きして返す。
    Material applyPhysicsTo(const Material& base) const;

    // このMaterialを参照するBaseCubeの逆引き。参照元の物理再生成、
    // Decalハイライト、描画で使う。期限切れのweak_ptrは走査時に除去する。
    void registerUser(const std::shared_ptr<BaseCube>& cube);
    void unregisterUser(const BaseCube* cube);
    void forEachUser(const std::function<void(BaseCube&)>& fn);

    // 物理値が変わったので、参照元BaseCubeのactorを再生成させる。
    void notifyPhysicsChanged();

private:
    std::vector<std::weak_ptr<BaseCube>> m_users;
};

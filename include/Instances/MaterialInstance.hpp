#pragma once
#include <include/Instances/Instance.hpp>
#include <include/Util/Material.hpp>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
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

    // 画像マップ(FileRef参照)。値は FileRef のパス文字列で、Material自身の子孫パス、
    // 無ければ最上位の祖先(System)からのパスで解決する。空なら未使用。
    // BaseColor は拡散色に乗算、Roughness/Metallic は上の値に乗算、Normal はタンジェント空間
    // (OpenGL規約、+Yが上)。貼り方はワールド基準のtriplanarで、TextureScale stud ごとに1タイル。
    std::string BaseColorMap;
    std::string RoughnessMap;
    std::string MetallicMap;
    std::string NormalMap;
    float TextureScale   = 4.0f;  // 1タイルあたりのstud数
    float NormalStrength = 1.0f;  // 法線マップの強さ(0で平坦)

    // 物理特性。StaticFrictionはBox3Dでは使われないため、エディタ/Luaからは
    // 非表示（将来の実装に備えて保持・YAML保存する）。
    float StaticFriction  = 0.5f;
    float DynamicFriction = 0.5f;
    float Restitution     = 0.1f;
    float MassDensity     = 1.0f;

    // 雷の標的になるか（従来のMaterialType::Metal判定の置き換え）。
    bool Conductive = false;

    MaterialInstance();
    virtual ~MaterialInstance();

    virtual std::string getClassName() override;
    virtual bool IsA(std::string className) override;
    virtual void setProperty(const std::string& name, const YAML::Node& value) override;
    virtual std::shared_ptr<Instance> clone() const override;

    enum class MapSlot { BaseColor = 0, Roughness, Metallic, Normal };
    static constexpr int MAP_SLOT_COUNT = 4;

    // マップの参照文字列(未設定なら空)。
    const std::string& mapReference(MapSlot slot) const;
    // 参照を解決したFileRefのPath。未設定・未解決・FileRefでない・Pathが空なら空文字列。
    std::string resolveMapPath(MapSlot slot);
    // いずれかのマップが設定されているか(解決はしない。描画の分岐用で軽い)。
    bool hasMaps() const;
    // 面へ投影するDecal/Textureを子に持つか。子の増減時に更新するので、描画の収集(複数
    // スレッドから読む)で文字列比較をせずに使える。
    bool hasFaceVisuals() const { return m_hasFaceVisuals; }
    virtual void onChildrenChanged() override;

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
    // 参照元。重複チェックと解除がO(1)になるよう、生ポインタをキーにする
    // (線形走査だとMaterialを参照するCubeが増えるほど登録が二乗で遅くなる)。
    std::unordered_map<const BaseCube*, std::weak_ptr<BaseCube>> m_users;
    bool m_hasFaceVisuals = false;
};

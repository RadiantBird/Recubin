#pragma once
#include <Instances/Spatial.hpp>
#include <Instances/BaseCube.hpp>
#include <cstdint>

class Model : public Spatial {
    friend class BaseCube;

private:
    std::uint32_t m_characterCollisionGroup = 0;
    void refreshCharacterCollisionGroup();

    // PrimaryCube参照。パス文字列(m_primaryCubeName)が正で、weak_ptrは解決結果のキャッシュ。
    // 読み込み時は子がまだ無いため、getPrimaryCube()で遅延解決する。
    mutable std::weak_ptr<BaseCube> m_primaryCube;
    mutable std::string m_primaryCubeName;
    mutable bool m_primaryCubeWarned = false;
    std::shared_ptr<BaseCube> resolvePrimaryCube(const std::string& path) const;

public:
    Model(Vector3 Pos = {0,0,0}, Vector3 Sz = {1,1,1}) : Spatial(Pos, Sz, "Model") {}
    std::string getClassName() { return "Model"; }
    bool IsA(std::string name) override {
        if (name == "Model") return true;
        return Spatial::IsA(name);
    }
    // PrimaryCube: Modelの原点(Position/Pivot)とみなす子孫のBaseCube。
    // 未設定・未解決・子孫でない場合はnullptr(重心を使う従来の挙動)。
    BaseCube* getPrimaryCube() const;
    std::string getPrimaryCubePath() const;
    // pathが解決できて子孫でない場合はエラーログを出して変更しない。
    // 解決できる場合はModelの原点をPrimaryCubeへ同期する。
    void setPrimaryCubePath(const std::string& path);
    void setPrimaryCube(const std::shared_ptr<BaseCube>& cube);
    // Modelの原点をPrimaryCubeのワールド座標へ更新する(子孫のワールド姿勢は変えない)。
    void syncPivotToPrimaryCube();
    // 登録済みModelのリストに対して適用する。PrimaryCubeを持たないModelは何もしない。
    static void syncPivotsToPrimaryCube(const std::vector<Model*>& models);
    CFrame getPivotCFrame() const;
    void pivotTo(const CFrame& worldCFrame);
    // Modelの原点を子孫の重心へ更新する。PrimaryCubeがある場合は重心ではなく
    // PrimaryCubeの座標を優先する。子孫のワールド姿勢は変えないため、
    // 子が動いて原点が動く、という循環は起きない。動的なBaseCubeを含まない
    // Modelは対象外。重心とのずれが小さい場合は更新しない(近似)。
    void syncPivotToCentroid();
    // root配下の全Modelに対してsyncPivotToCentroidを適用する(親から子の順)
    static void syncPivotsToCentroid(Instance& root);
    // 登録済みModelのリストに対して適用する(ツリー走査なし。リストは親から子の順)
    static void syncPivotsToCentroid(const std::vector<Model*>& models);
    void onChildrenChanged() override;
    void collectInstanceReferences(std::vector<InstanceReference>& out) override;
    std::shared_ptr<Instance> clone() const override;
};

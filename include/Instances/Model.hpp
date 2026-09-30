#pragma once
#include <Instances/Spatial.hpp>
#include <Instances/BaseCube.hpp>
#include <cstdint>

class Model : public Spatial {
    friend class BaseCube;

private:
    std::uint32_t m_characterCollisionGroup = 0;
    void refreshCharacterCollisionGroup();

public:
    Model(Vector3 Pos = {0,0,0}, Vector3 Sz = {1,1,1}) : Spatial(Pos, Sz, "Model") {}
    std::string getClassName() { return "Model"; }
    bool IsA(std::string name) override {
        if (name == "Model") return true;
        return Spatial::IsA(name);
    }
    CFrame getPivotCFrame() const;
    void pivotTo(const CFrame& worldCFrame);
    // Modelの原点を子孫の重心へ更新する。子孫のワールド姿勢は変えないため、
    // 子が動いて原点が動く、という循環は起きない。動的なBaseCubeを含まない
    // Modelは対象外。重心とのずれが小さい場合は更新しない(近似)。
    void syncPivotToCentroid();
    // root配下の全Modelに対してsyncPivotToCentroidを適用する(親から子の順)
    static void syncPivotsToCentroid(Instance& root);
    void onChildrenChanged() override;
    std::shared_ptr<Instance> clone() const override;
};

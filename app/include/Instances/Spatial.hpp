#pragma once

#include "Instance.hpp"
#include "Math/Vector3.hpp"
#include "Math/Quaternion.hpp"
#include "Math/CFrame.hpp"
#include <cstdint>
#include <utility>
#include <vector>

class Spatial : public Instance {
public:
    Vector3 Size;

    enum class SpatialUpdateOrigin { Script, Editor, Deserialization, Physics, Network, Animation };

    Spatial(Vector3 Pos, Vector3 Sz, std::string name) 
        : Instance(name), m_cframe(Pos), Size(Sz) {}
    std::string getClassName() override { return "Spatial"; }
    virtual bool IsA(std::string name) override;
    virtual void setProperty(const std::string& name, const YAML::Node& value) override;

    // 親チェーンを辿ってワールド CFrame を合成する
    // Workspace より上は合成しないため、Workspace 直下の Spatial は自身の cframe がワールド値
    CFrame getWorldCFrame() const;
    Vector3 getWorldPosition() const { return getWorldCFrame().Position; }
    // Transform mutation entry points.  These preserve the world poses of
    // Spatial descendants when this node is moved.
    const CFrame& getCFrame() const { return m_cframe; }
    const Vector3& getPosition() const { return m_cframe.Position; }
    const Quaternion& getRotation() const { return m_cframe.Rotation; }
    virtual void setCFrame(const CFrame& value);
    virtual void setPosition(const Vector3& value);
    virtual void setRotation(const Quaternion& value);
    virtual void setSize(const Vector3& value);
    // Set a world-space position while preserving the current world rotation.
    // The virtual local setter keeps BaseCube physics state synchronized.
    void setWorldPosition(const Vector3& worldPosition);
    void commitCFrame(const CFrame& value, SpatialUpdateOrigin origin);
    // Internal editor/deserialization transaction: applies already-resolved
    // local frames without recursively preserving descendants per element.
    static void applyLocalCFrameBatch(
        const std::vector<std::pair<Spatial*, CFrame>>& values);
    // いずれかのSpatialの姿勢(m_cframe)またはSizeが実際に変わるたびに増えるカウンター。
    // 全Cubeのワールド境界に依存する空間インデックスの無効化検知に使う。値自体に意味は
    // 無い。Sizeは公開フィールドなので、setSize以外で直接書き換えたら
    // notifyBoundsChanged()を呼ぶこと。
    static std::uint64_t boundsEpoch() { return s_boundsEpoch; }
    static void notifyBoundsChanged() { ++s_boundsEpoch; }
    // 座標基準となる最近傍 Spatial 親。Folder 等の非 Spatial は透過する。
    Spatial* getCoordinateParent() const;
    void refreshHierarchyCache() override;
    // ワールドCFrameを親SpatialからのローカルCFrameへ変換して設定する。
    void setWorldCFrame(const CFrame& worldCFrame);

private:
    CFrame m_cframe;
    // Spatialの祖先を持つか。falseなら親チェーンを辿らずlocal==worldで返せる(50000個の
    // Workspace直下Cubeを毎フレーム読むため)。親の付け替え時にsetParentが更新する。
    // 古いtrueは遅い経路で正しく解決されるだけだが、古いfalseは誤った姿勢になるので、
    // 祖先のSpatialが増える経路(setParent)では必ず更新すること。
    bool m_hasSpatialAncestor = false;
    static inline std::uint64_t s_boundsEpoch = 0;
};

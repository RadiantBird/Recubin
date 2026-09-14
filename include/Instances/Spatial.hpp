#pragma once

#include "Instance.hpp"
#include "Math/Vector3.hpp"
#include "Math/Quaternion.hpp"
#include "Math/CFrame.hpp"
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
    void commitCFrame(const CFrame& value, SpatialUpdateOrigin origin);
    // Internal editor/deserialization transaction: applies already-resolved
    // local frames without recursively preserving descendants per element.
    static void applyLocalCFrameBatch(
        const std::vector<std::pair<Spatial*, CFrame>>& values);
    // 座標基準となる最近傍 Spatial 親。Folder 等の非 Spatial は透過する。
    Spatial* getCoordinateParent() const;
    // ワールドCFrameを親SpatialからのローカルCFrameへ変換して設定する。
    void setWorldCFrame(const CFrame& worldCFrame);

private:
    CFrame m_cframe;
};

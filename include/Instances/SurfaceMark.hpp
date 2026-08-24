#pragma once

#include <include/Instances/Spatial.hpp>
#include <include/Util/Color4.hpp>
#include <string>
#include <vector>
#include <memory>

class BaseCube;

enum class SurfaceMarkFilterMode { Exclude = 0, Include = 1 };

// 3D空間から投影方向へ画像を投影する、独立したSpatialインスタンス。
class SurfaceMark : public Spatial {
public:
    unsigned int TextureID = 0;
    std::string texturePath;
    Color4 Color;
    SurfaceMarkFilterMode FilterMode = SurfaceMarkFilterMode::Exclude;

    SurfaceMark();
    explicit SurfaceMark(const Vector3& position, const Vector3& size = Vector3(4.0f, 4.0f, 4.0f));

    std::string getClassName() override;
    bool IsA(std::string className) override;
    void setProperty(const std::string& name, const YAML::Node& value) override;
    std::shared_ptr<Instance> clone() const override;

    void setTexturePath(const std::string& path);

    // SurfaceMarkローカルの-Z軸（親を含むワールド空間）を返す。
    Vector3 getForward() const;
    // 投影ボリュームを保守的な球として扱う候補カリング用ヘルパー。
    bool intersectsSphere(const Vector3& sphereCenter, float sphereRadius) const;

    const std::vector<std::weak_ptr<Instance>>& getFilterInstances() const { return m_filterInstances; }
    const std::vector<std::string>& getFilterPaths() const { return m_filterPaths; }
    void setFilterInstances(const std::vector<std::shared_ptr<Instance>>& instances);
    void setFilterPaths(const std::vector<std::string>& paths);
    void setFilterState(const std::vector<std::shared_ptr<Instance>>& instances,
                        const std::vector<std::string>& paths);
    void resolveFilterInstances(Instance* root);
    void refreshFilterPaths();
    bool allowsSurfaceTarget(const BaseCube& target) const;
    void remapClonedInstances(const CloneRemap& remap) override;
    void collectInstanceReferences(std::vector<InstanceReference>& out) override {
        std::vector<std::shared_ptr<Instance>> instances;
        instances.reserve(m_filterInstances.size());
        for (const auto& weak : m_filterInstances) instances.push_back(weak.lock());
        const auto paths = m_filterPaths;
        for (std::size_t index = 0; index < m_filterInstances.size(); ++index) {
            const auto& weak = m_filterInstances[index];
            auto target = weak.lock();
            out.push_back({target, {}, "SurfaceMark.Filter",
                [this, target, index, instances, paths](std::shared_ptr<Instance> value) mutable {
                    auto updated = instances;
                    for (auto& item : updated) if (item == target) item = value;
                    auto updatedPaths = paths;
                    if (!value && index < updatedPaths.size()) updatedPaths[index].clear();
                    setFilterState(updated, updatedPaths);
                }});
        }
    }

private:
    std::vector<std::weak_ptr<Instance>> m_filterInstances;
    std::vector<std::string> m_filterPaths;
};

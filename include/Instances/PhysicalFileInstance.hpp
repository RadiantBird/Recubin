#pragma once

#include <Instances/Instance.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <utility>

// 物理ファイルへの参照を持つInstanceの共通基底。
// PathはScene上ではContentPathとして保存され、区切り文字は'/'へ正規化される。
class PhysicalFileInstance : public Instance {
public:
    std::string Path;

    explicit PhysicalFileInstance(std::string className);
    ~PhysicalFileInstance() override = default;

    std::string getClassName() override { return "PhysicalFileInstance"; }
    bool IsA(std::string className) override;
    void setProperty(const std::string& name, const YAML::Node& value) override;

protected:
    void clonePhysicalFileFieldsTo(PhysicalFileInstance& copy,
                                   std::string_view className) const;
};

template<class Tag>
class BasicPhysicalFileInstance final : public PhysicalFileInstance {
public:
    BasicPhysicalFileInstance() : PhysicalFileInstance(std::string(Tag::className)) {}

    std::string getClassName() override { return std::string(Tag::className); }

    std::shared_ptr<Instance> clone() const override {
        auto copy = std::make_shared<BasicPhysicalFileInstance<Tag>>();
        clonePhysicalFileFieldsTo(*copy, Tag::className);
        return copy;
    }
};

// 中央の.defから互換型を生成する。新しい単純な物理ファイルInstanceは
// PhysicalFileInstances.defへ1行追加するだけで型も公開される。
#define RCBN_FILE_INSTANCE(ClassName, Kind, Category, DialogLabel, Filter) \
    struct ClassName##PhysicalFileTag {                                  \
        static constexpr const char* className = #ClassName;             \
    };                                                                    \
    using ClassName = BasicPhysicalFileInstance<ClassName##PhysicalFileTag>;
#include <Instances/PhysicalFileInstances.def>
#undef RCBN_FILE_INSTANCE

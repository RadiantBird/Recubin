#pragma once
#include <include/Instances/Instance.hpp>

// ==================================================================
//  MaterialService
//
//  System直下に自動生成されるサービス。配下にMaterialを置き、BaseCubeが
//  Materialを参照することでPBR値・物理特性・6面テクスチャを共有する。
//  自身はプロパティを持たないコンテナ。
// ==================================================================
class MaterialService : public Instance {
public:
    MaterialService();
    virtual ~MaterialService() = default;

    virtual std::string getClassName() override;
    virtual bool IsA(std::string className) override;
    virtual std::shared_ptr<Instance> clone() const override;
};

#pragma once

#include <Instances/PhysicalFileInstance.hpp>

#include <memory>
#include <string>

// C++ Process のシーン定義。Path は C++ ソース、DllPath はビルド済み DLL を指す。
// 実行状態や DLL のロードは ProcessHandler が所有し、この Instance は永続設定だけを保持する。
class Process final : public PhysicalFileInstance {
public:
    bool Enabled = true;
    std::string DllPath;

    Process();
    ~Process() override = default;

    std::string getClassName() override { return "Process"; }
    bool IsA(std::string className) override;
    std::shared_ptr<Instance> clone() const override;
};

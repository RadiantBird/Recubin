#pragma once
#include <include/Instances/Instance.hpp>
#include <string>

// FileRef — ファイル（アセット）への参照を表す軽量インスタンス。
// パスはエディタで設定され YAML（ContentPath キー）に保存されるため、
// Packager に追跡・同梱・書換される。スクリプトは生パスを書かず、この
// インスタンスを消費プロパティ（例: Sound.Source）へ参照として渡す。
class FileRef : public Instance {
public:
    std::string Path;  // 参照先ファイルパス（YAML 上は ContentPath）

    FileRef();
    virtual ~FileRef() = default;

    virtual std::string getClassName() override { return "FileRef"; }
    virtual bool IsA(std::string name) override;
    virtual void setProperty(const std::string& name, const YAML::Node& value) override;
    virtual std::shared_ptr<Instance> clone() const override;
};

#pragma once

#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <yaml-cpp/yaml.h>

#ifdef _WIN32
    #undef getClassName // これでInstance::getClassNameがAに化けるのを防ぐ
#endif

class BaseCube;

class Instance : public std::enable_shared_from_this<Instance> {
    protected:
        using string = std::string;
    public:
        string Name = "Instance";

        std::weak_ptr<Instance> Parent;
        std::unordered_map<string, std::shared_ptr<Instance>> children = {};

        virtual void onAncestorChanged();
        virtual void setParent(std::shared_ptr<Instance> newParent);

        Instance* findFirstAncestorWorkspace();

        Instance(string name);

        virtual string getClassName();
        virtual bool IsA(std::string className);

        // YAMLなどからプロパティを設定するためのインターフェース
        virtual void setProperty(const std::string& name, const YAML::Node& value);

        Instance* getChild(string child_name);
        Instance* getChildByPath(const std::string& path);
        const std::unordered_map<string, std::shared_ptr<Instance>>& getChildren();

        virtual void addChild(std::shared_ptr<Instance> child);
        bool removeChild(string name);

        string getFullPath();

        virtual std::shared_ptr<Instance> clone() const;

        // 制約（Weld/Rope/Rod/Motor）のキューブ参照をクローン先へ張り替えるための仕組み。
        // orig BaseCube* -> clone shared_ptr<BaseCube> の対応表。
        using CubeRemap = std::unordered_map<BaseCube*, std::shared_ptr<BaseCube>>;
        // 既定 no-op。制約クラスが自分の m_cube0/m_cube1 を map で引いて差し替える。
        virtual void remapClonedCubes(const CubeRemap&) {}

        // orig と clone を子名でペアリングして並行走査し、clone 内の制約参照を
        // clone 側のキューブへ張り替える（アセンブリ剛体がクローンで壊れないように）。
        static void rebindClonedConstraints(const Instance& orig, Instance& clone);

        // clone() した上で制約参照をクローン側へ張り替えた完全なサブツリーを返す。
        std::shared_ptr<Instance> cloneTree() const;

        virtual ~Instance();
};

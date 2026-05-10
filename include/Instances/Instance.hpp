#pragma once

#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <yaml-cpp/yaml.h>

#ifdef _WIN32
    #include <windows.h>
    #undef GetClassName // これでInstance::GetClassNameがAに化けるのを防ぐ
#endif

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

        virtual string GetClassName();
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

        virtual ~Instance();
};

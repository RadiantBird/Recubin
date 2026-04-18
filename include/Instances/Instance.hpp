#pragma once

#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include <yaml-cpp/yaml.h>

#ifdef _WIN32
    #include <windows.h>
    #undef GetClassName // これでInstance::GetClassNameがAに化けるのを防ぐ
#endif

class Instance {
    protected:
        using string = std::string;
    public:
        string Name = "Instance";

        Instance* Parent = nullptr;
        std::unordered_map<string, Instance*> children = {};

        virtual void onAncestorChanged();
        virtual void setParent(Instance* newParent);

        Instance* findFirstAncestorWorkspace();

        Instance(string name);

        virtual string GetClassName();
        virtual bool IsA(std::string className);

        // YAMLなどからプロパティを設定するためのインターフェース
        virtual void setProperty(const std::string& name, const YAML::Node& value);

        Instance* getChild(string child_name);
        const std::unordered_map<string, Instance*>& getChildren();

        virtual void addChild(Instance* child);
        bool removeChild(string name);

        string getFullPath();

        virtual ~Instance();
};

#pragma once

#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

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

        Instance* getChild(string child_name);
        const std::unordered_map<string, Instance*>& getChildren();

        virtual void addChild(Instance* child);
        bool removeChild(string name);

        string getFullPath();

        virtual ~Instance();
};

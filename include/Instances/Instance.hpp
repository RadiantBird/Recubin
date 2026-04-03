#pragma once
#include <string>
#include <unordered_map>

class Instance {
    protected:
        using string = std::string;
    public:
        string Name = "Instance";
        string ClassName = "Instance";

        Instance* Parent = nullptr;
        std::unordered_map<string, Instance*> children = {};

        Instance(string name) {
            this->Name = name;
        }

        virtual bool IsA(string className) {
            if (className == this->ClassName) {
                return true;
            }
            return false;
        }

        Instance* getChild(string child_name) {
            auto it = this->children.find(child_name);
            if (it != this->children.end()) {
                return it->second;
            }
            else {
                return nullptr;
            }
        }

        const std::unordered_map<string, Instance*>& getChildren() {
            return this->children;
        }

        void addChild(string name) {
            Instance* child = new Instance(name);
            child->Parent = this;
            this->children[name] = child;
        }

        bool removeChild(string name) {
            Instance* child = getChild(name);
            if (child) {
                Instance* parent = child->Parent;
                // Root cannot be deleted, null check is worthless.
                parent->children.erase(name);
                delete child;
                return true;
            }
            return false;
        }

        string getFullPath() {
            Instance* current = this;
            string path = "";
            std::vector<string> data = {current->Name};

            while (current->Parent != nullptr) {
                current = current->Parent;
                data.push_back(current->Name);
            }

            std::reverse(data.begin(), data.end());

            path = data[0];
            for (int i = 1; i < data.size(); i++) {
                path = path + "\\" + data[i];
            }

            if (data.size() == 1) {
                path += "\\";
            }
            
            return path;
        }

        virtual ~Instance() {
            for (auto const& [_, child] : this->children) { // Hello world! And in case I don’t see ya, good pointer, good borrowing, and goodbye world!
                delete child;
            }
        }
};
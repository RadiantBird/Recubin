#pragma once

#include <iostream>
#include <string>
#include <unordered_map>

class Instance {
    protected:
        using string = std::string;
    public:
        string Name = "Instance";

        Instance* Parent = nullptr;
        std::unordered_map<string, Instance*> children = {};

        virtual void onAncestorChanged() {
            // 子孫にも通知を伝播させる
            for (auto const& [_, child] : this->children) {
                child->onAncestorChanged();
            }
        }

        virtual void setParent(Instance* newParent) {
            if (this->Parent == newParent) return;
            
            // 1. 親子関係の更新
            this->Parent = newParent;
            
            // 2. 自分と子孫に通知 (O(n) だが、移動時のみ実行される)
            this->onAncestorChanged();
        }

        Instance* findFirstAncestorWorkspace() {
            Instance* current = this->Parent;
            while (current) {
                if (current->IsA("Workspace")) return current;
                current = current->Parent;
            }
            return nullptr;
        }

        Instance(string name) {
            this->Name = name;
        }

        virtual bool IsA(std::string className) {
            if (className == "Instance") {
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

        virtual void addChild(Instance* child) {
            // 1. 安全装置：空っぽ（nullptr）が来たら何もしない
            if (child == nullptr) {
                std::cout << "[WARN] addChild called but child is nullptr!\n";
                return;
            }

            // 2. 親子関係の構築
            child->Parent = this;

            // 3. 自分の子供リストに登録
            // 名前（child->Name）をキーにして保存する
            this->children[child->Name] = child;
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
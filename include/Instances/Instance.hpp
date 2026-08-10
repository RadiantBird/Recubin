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

class Instance : public std::enable_shared_from_this<Instance> {
    protected:
        using string = std::string;
    public:
        string Name = "Instance";

        std::weak_ptr<Instance> Parent;
        std::unordered_map<string, std::shared_ptr<Instance>> children = {};

        virtual void onAncestorChanged();
        virtual void onChildrenChanged();
        virtual void setParent(std::shared_ptr<Instance> newParent);

        // Name を変更する唯一の正しい経路。親の children マップとの整合を保つ。
        // 兄弟と名前が衝突する場合は上書きせず、一意な名前へずらして警告する。
        void renameTo(const std::string& newName);
        void lockRuntimeName() { m_runtimeNameLocked = true; }
        bool isRuntimeNameLocked() const { return m_runtimeNameLocked; }
        bool renameToAuthoritative(const std::string& newName);

        Instance* findFirstAncestorWorkspace();
        Instance* findFirstAncestorSystem();

        // stopAt(除外) までの "\\" 区切り相対パス。getChildByPath と対になる形式
        std::string getPathUpTo(Instance* stopAt);
        // Workspace 配下なら Workspace 相対、外なら最上位祖先相対
        std::string getWorkspaceRelativePath();

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

        // 制約（Weld/Rope/Rod/Motor）等がクローン先の参照へ張り替えるための仕組み。
        // orig Instance* -> clone shared_ptr<Instance> の対応表（型は問わず全ノードを含む）。
        using CloneRemap = std::unordered_map<Instance*, std::shared_ptr<Instance>>;
        // 既定 no-op。参照を持つ派生クラスが自分のメンバを map で引いて差し替える。
        virtual void remapClonedInstances(const CloneRemap&) {}

        // orig と clone を子名でペアリングして並行走査し、clone 内の制約参照を
        // clone 側のキューブへ張り替える（アセンブリ剛体がクローンで壊れないように）。
        static void rebindClonedConstraints(const Instance& orig, Instance& clone);

        // clone() した上で制約参照をクローン側へ張り替えた完全なサブツリーを返す。
        std::shared_ptr<Instance> cloneTree() const;

        // 複数ルートを一つの複製単位としてクローンする。選択された Cube と Weld のように、別ルート間の参照も複製先へ張り替える。
        static std::vector<std::shared_ptr<Instance>> cloneForest(
            const std::vector<std::shared_ptr<Instance>>& roots);

        virtual ~Instance();

    private:
        bool m_runtimeNameLocked = false;
};

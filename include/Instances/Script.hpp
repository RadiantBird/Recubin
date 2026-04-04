#include <include/Instances/Instance.hpp>
#include <include/Instances/Workspace.hpp>

class Script : public Instance {
    public:
        string Source = R"(print("Hello world!"))";
        Workspace* lastWorkspace = nullptr;

        bool Enabled = true;
        bool IsA(std::string className) {
            if (className == "Script") {
                return true;
            }
            return Instance::IsA(className);
        }

        void onAncestorChanged() override {
            // Workspace を探す (O(h))
            Instance* ws_raw = findFirstAncestorWorkspace();
            
            if (ws_raw) {
                Workspace* ws = static_cast<Workspace*>(ws_raw);
                ws->registerScript(this);
                lastWorkspace = ws;
            } else {
                // Workspace 外に放り出されたらリストから抜ける
                if (lastWorkspace) {
                    lastWorkspace->unregisterScript(this);
                }
            }

            // 子がいれば通知を継続（Scriptの中にScriptを入れる変態構成にも対応）
            Instance::onAncestorChanged();
        }
};
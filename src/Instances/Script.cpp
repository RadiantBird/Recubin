#include <Instances/Script.hpp>
#include <fstream>
#include <sstream>

Script::Script(string path) : Instance("Script"), Coroutine(nullptr), Path(path) {
    // load source from path
    std::ifstream file(path);
    if (file.is_open()) {
        std::stringstream buffer;
        buffer << file.rdbuf();
        Source = buffer.str();
        std::cout << "Loaded script from " << path << "\n";
    } else {
        std::cerr << "Failed to open script file: " << path << "\n";
        Source = "print('Error: Failed to load script source')";
    }
}  

std::string Script::GetClassName() {
    return "Script";
}

bool Script::IsA(std::string className) {
    if (className == "Script") {
        return true;
    }
    return Instance::IsA(className);
}

void Script::onAncestorChanged() {
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
        lastWorkspace = nullptr;
    }

    // 子がいれば通知を継続（Scriptの中にScriptを入れる変態構成にも対応）
    Instance::onAncestorChanged();
}

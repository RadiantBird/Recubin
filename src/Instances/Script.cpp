#include <Instances/Script.hpp>
#include <Core/FileLoader.hpp>

Script::Script(string path) : Instance("Script"), Coroutine(nullptr), Path(path) {
    Source = FileLoader::readText(path);
    if (Source.empty()) {
        Source = "print('Error: Failed to load script source')";
    } else {
        std::cout << "Loaded script from " << path << "\n";
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

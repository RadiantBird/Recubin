#include <Instances/Script.hpp>
#include <Core/FileLoader.hpp>

Script::Script(string path) : Instance("Script"), Coroutine(nullptr), Path(path) {
    if (!path.empty()) {
        Source = FileLoader::readText(path);
        if (Source.empty()) {
            Source = "print('Error: Failed to load script source')";
        } else {
            std::cout << "Loaded script from " << path << "\n";
        }
    }
}

void Script::setProperty(const std::string& name, const YAML::Node& value) {
    if (name == "Source" || name == "Path" || name == "ContentPath") {
        this->Path = value.as<std::string>();
        this->Source = FileLoader::readText(this->Path);
        if (this->Source.empty()) {
            this->Source = "print('Error: Failed to load script source: " + this->Path + "')";
        }
    } else {
        Instance::setProperty(name, value);
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

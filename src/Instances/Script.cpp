#include <Instances/Script.hpp>
#include <Core/FileLoader.hpp>
#include <Util/Logger.hpp>

Script::Script(string path) : Instance("Script"), Coroutine(nullptr), Path(path) {
    if (!path.empty()) {
        Source = FileLoader::readText(path);
        if (Source.empty()) {
            RCBN_WARN("Failed to load script source: " << path);
            Source = "print('Error: Failed to load script source')";
        } else {
            std::cout << "Loaded script from " << path << "\n";
        }
    }
}

void Script::setProperty(const std::string& name, const YAML::Node& value) {
    if (name == "Source" || name == "Path" || name == "ContentPath") {
        this->Path = value.as<std::string>();
        // .luauc files are pre-compiled bytecode — load as binary
        bool isBytecode = this->Path.size() >= 6 &&
                          this->Path.rfind(".luauc") == this->Path.size() - 6;
        if (isBytecode) {
            auto bytes = FileLoader::readBinary(this->Path);
            if (!bytes.empty()) {
                this->Source = std::string(bytes.begin(), bytes.end());
                this->isPrecompiled = true;
                std::cout << "Loaded bytecode script from " << this->Path << "\n";
            } else {
                RCBN_WARN("Failed to load bytecode: " << this->Path);
                this->Source = "print('Error: Failed to load bytecode: " + this->Path + "')";
                this->isPrecompiled = false;
            }
        } else {
            this->Source = FileLoader::readText(this->Path);
            this->isPrecompiled = false;
            if (this->Source.empty()) {
                RCBN_WARN("Failed to load script source: " << this->Path);
                this->Source = "print('Error: Failed to load script source: " + this->Path + "')";
            }
        }
    } else {
        Instance::setProperty(name, value);
    }
}

std::string Script::getClassName() {
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
        if (lastWorkspace != ws) {
            if (lastWorkspace) {
                lastWorkspace->unregisterScript(std::static_pointer_cast<Script>(shared_from_this()));
            }
            ws->registerScript(std::static_pointer_cast<Script>(shared_from_this()));
        }
        lastWorkspace = ws;
    } else {
        // Workspace 外に放り出されたらリストから抜ける
        if (lastWorkspace) {
            lastWorkspace->unregisterScript(std::static_pointer_cast<Script>(shared_from_this()));
        }
        lastWorkspace = nullptr;
    }

    // 子がいれば通知を継続（Scriptの中にScriptを入れる変態構成にも対応）
    Instance::onAncestorChanged();
}

#include <Instances/Script.hpp>
#include <Instances/System.hpp>
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
    if (name == "Enabled") {
        this->Enabled = value.as<bool>();
    } else if (name == "Source" || name == "Path" || name == "ContentPath") {
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

// 実行中のコルーチンを切り離す。次のexecute()呼び出しがCoroutine==nullptrを
// 見て新規コルーチンを生成し、Sourceから最初から再実行する。古いコルーチンは
// 通常の完了パスと同様GC任せ(MaxRestartsPerFrameが1フレームあたりの増加量を制限する)。
void Script::restart() {
    Coroutine = nullptr;
    CoroutineRef = -1;  // 呼び出し元がlua_unref済みであることを前提とする
    Completed = false;
    Aborted = false;
    Sleeping = false;
    SleepRemaining = 0.0f;
    WaitingForChild = false;
    WaitTarget.reset();
    WaitChildName.clear();
    WaitTimeout = -1.0f;
    WaitElapsed = 0.0f;
}

std::shared_ptr<Instance> Script::clone() const {
    auto copy = std::make_shared<Script>();
    copy->Name          = Name;
    copy->Source        = Source;
    copy->Path          = Path;
    copy->Enabled       = Enabled;
    copy->isPrecompiled = isPrecompiled;
    // 実行時状態(Coroutine/Sleeping/Completed/lastWorkspace等)は複製せず新規のまま
    for (auto const& [n, child] : children)
        copy->addChild(child->clone());
    return copy;
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

        // Workspace配下に入ったらSystem側の管理からは抜ける（相互排他）
        if (lastSystem) {
            lastSystem->unregisterScript(std::static_pointer_cast<Script>(shared_from_this()));
            lastSystem = nullptr;
        }
    } else {
        // Workspace 外に放り出されたらリストから抜ける
        if (lastWorkspace) {
            lastWorkspace->unregisterScript(std::static_pointer_cast<Script>(shared_from_this()));
        }
        lastWorkspace = nullptr;

        // Workspace外でもSystem配下（System直下・Users/User配下など）なら実行対象にする
        Instance* sys_raw = findFirstAncestorSystem();
        if (sys_raw) {
            System* sys = static_cast<System*>(sys_raw);
            if (lastSystem != sys) {
                if (lastSystem) {
                    lastSystem->unregisterScript(std::static_pointer_cast<Script>(shared_from_this()));
                }
                sys->registerScript(std::static_pointer_cast<Script>(shared_from_this()));
            }
            lastSystem = sys;
        } else {
            if (lastSystem) {
                lastSystem->unregisterScript(std::static_pointer_cast<Script>(shared_from_this()));
            }
            lastSystem = nullptr;
        }
    }

    // 子がいれば通知を継続（Scriptの中にScriptを入れる変態構成にも対応）
    Instance::onAncestorChanged();
}

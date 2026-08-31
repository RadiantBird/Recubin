#include <Instances/Script.hpp>
#include <Instances/System.hpp>
#include <Core/FileLoader.hpp>
#include <Core/PropertyRegistry.hpp>
#include <Util/Logger.hpp>

namespace {
void setScriptPath(Script& script, const std::string& path) {
    script.Path = path;
    // .luauc files are pre-compiled bytecode — load as binary
    const bool isBytecode = script.Path.size() >= 6 &&
                            script.Path.rfind(".luauc") == script.Path.size() - 6;
    if (isBytecode) {
        auto bytes = FileLoader::readBinary(script.Path);
        if (!bytes.empty()) {
            script.Source = std::string(bytes.begin(), bytes.end());
            script.isPrecompiled = true;
            std::cout << "Loaded bytecode script from " << script.Path << "\n";
        } else {
            RCBN_WARN("Failed to load bytecode: " << script.Path);
            script.Source = "print('Error: Failed to load bytecode: " + script.Path + "')";
            script.isPrecompiled = false;
        }
        return;
    }

    script.Source = FileLoader::readText(script.Path);
    script.isPrecompiled = false;
    if (script.Source.empty()) {
        RCBN_WARN("Failed to load script source: " << script.Path);
        script.Source = "print('Error: Failed to load script source: " + script.Path + "')";
    }
}

const bool s_scriptRegistered = [] {
    using namespace PropertyRegistry;
    registerClass("Script", "Instance", {
        field<&Script::Enabled>("Enabled"),
        custom("Path", PropType::String,
            [](Instance* instance) -> PropValue {
                return static_cast<Script*>(instance)->Path;
            },
            [](Instance* instance, const PropValue& value) {
                setScriptPath(*static_cast<Script*>(instance), std::get<std::string>(value));
            }).yaml("ContentPath").filePath("Luau Script (*.luau;*.lua;*.luauc)",
                                            "*.luau;*.lua;*.luauc").noClone(),
    });
    return true;
}();
} // namespace

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
    if (PropertyRegistry::loadProperty(this, "Script", name, value)) return;

    // Path と Source は旧YAMLの別名。ContentPathは上のスキーマ定義が受け持つ。
    if (name == "Source" || name == "Path") {
        setScriptPath(*this, value.as<std::string>());
        return;
    }
    Instance::setProperty(name, value);
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
    WaitingForPath = false;
}

std::shared_ptr<Instance> Script::clone() const {
    auto copy = std::make_shared<Script>();
    copy->Name          = Name;
    PropertyRegistry::cloneFields(this, copy.get(), "Script");
    // Path はclone時に再読込せず、元のソース／bytecode状態をそのまま復元する。
    copy->Source        = Source;
    copy->Path          = Path;
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

#include <Core/Packager.hpp>
#include "include/luau/luacode.h"
#include <yaml-cpp/yaml.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <cstdlib>

namespace fs = std::filesystem;

// ---- helpers ----

static bool endsWith(const std::string& s, const std::string& suffix) {
    if (s.size() < suffix.size()) return false;
    return s.rfind(suffix) == s.size() - suffix.size();
}

static bool isScript(const std::string& path) {
    return endsWith(path, ".luau") || endsWith(path, ".lua");
}

static std::string assetSubdir(const std::string& path) {
    std::string ext = fs::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".mp3" || ext == ".wav" || ext == ".ogg" || ext == ".flac")
        return "assets/sound";
    if (ext == ".luau" || ext == ".lua" || ext == ".luauc")
        return "assets/scripts";
    return "assets/image";
}

// Copy a file, creating parent dirs as needed. Returns false on error.
static bool copyFile(const fs::path& src, const fs::path& dst,
                     std::function<void(const std::string&)>& log) {
    std::error_code ec;
    fs::create_directories(dst.parent_path(), ec);
    if (ec) { log("[WARN] mkdir failed: " + dst.parent_path().string()); }
    fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        log("[WARN] Copy failed: " + src.string() + " -> " + dst.string() + " : " + ec.message());
        return false;
    }
    return true;
}

// Compile a .luau source file to .luauc bytecode in-process. Returns output path, or "" on failure.
static std::string compileLuauInProc(const fs::path& src, const fs::path& dstDir,
                                      std::function<void(const std::string&)>& log) {
    std::error_code ec;
    fs::create_directories(dstDir, ec);

    std::ifstream f(src, std::ios::binary);
    if (!f) {
        log("[WARN] Cannot read script: " + src.string());
        return "";
    }
    std::string source((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    size_t bytecodeSize = 0;
    char* bytecode = luau_compile(source.c_str(), source.size(), nullptr, &bytecodeSize);

    // First byte == 0 means compile error; rest of buffer is the error message
    if (!bytecode || bytecodeSize == 0 || (unsigned char)bytecode[0] == 0) {
        std::string errMsg = (bytecode && bytecodeSize > 1)
            ? std::string(bytecode + 1, bytecodeSize - 1)
            : "unknown compile error";
        if (bytecode) free(bytecode);
        log("[WARN] Compile failed (" + src.filename().string() + "): " + errMsg);
        return "";
    }

    fs::path outPath = dstDir / (src.stem().string() + ".luauc");
    std::ofstream out(outPath, std::ios::binary);
    out.write(bytecode, (std::streamsize)bytecodeSize);
    free(bytecode);
    return outPath.string();
}

// Walk YAML tree and collect file-referencing values for given keys.
// out: vector of (yamlNodeRef-like info isn't trackable, so we collect string paths)
static void collectPaths(const YAML::Node& node, std::vector<std::string>& paths) {
    if (node.IsMap()) {
        for (auto it = node.begin(); it != node.end(); ++it) {
            std::string key = it->first.as<std::string>();
            if ((key == "ContentPath" || key == "Texture" || key == "FacePath") && it->second.IsScalar()) {
                std::string v = it->second.as<std::string>();
                if (!v.empty()) paths.push_back(v);
            } else if (key == "SkyboxPaths" && it->second.IsSequence()) {
                for (const auto& elem : it->second) {
                    if (elem.IsScalar()) {
                        std::string v = elem.as<std::string>();
                        if (!v.empty()) paths.push_back(v);
                    }
                }
            } else {
                collectPaths(it->second, paths);
            }
        }
    } else if (node.IsSequence()) {
        for (const auto& child : node) {
            collectPaths(child, paths);
        }
    }
}

// Rewrite file-referencing paths inside a YAML node tree.
// pathMap: old absolute/relative path -> new relative path inside the package
static void rewritePaths(YAML::Node node,
                         const std::unordered_map<std::string, std::string>& pathMap) {
    if (node.IsMap()) {
        for (auto it = node.begin(); it != node.end(); ++it) {
            std::string key = it->first.as<std::string>();
            if ((key == "ContentPath" || key == "Texture" || key == "FacePath") && it->second.IsScalar()) {
                std::string v = it->second.as<std::string>();
                auto found = pathMap.find(v);
                if (found != pathMap.end()) it->second = found->second;
            } else if (key == "SkyboxPaths" && it->second.IsSequence()) {
                YAML::Node seq = it->second;
                for (std::size_t i = 0; i < seq.size(); ++i) {
                    if (seq[i].IsScalar()) {
                        std::string v = seq[i].as<std::string>();
                        auto found = pathMap.find(v);
                        if (found != pathMap.end()) seq[i] = found->second;
                    }
                }
            } else {
                rewritePaths(it->second, pathMap);
            }
        }
    } else if (node.IsSequence()) {
        for (auto child : node) {
            rewritePaths(child, pathMap);
        }
    }
}

// ---- public API ----

bool Packager::package(const Config& cfg, std::function<void(const std::string&)> log) {
    // Resolve output game folder
    fs::path gameDir = fs::path(cfg.outputDir) / cfg.gameName;
    std::error_code ec;
    fs::create_directories(gameDir, ec);
    if (ec) { log("[ERROR] Cannot create output folder: " + gameDir.string()); return false; }

    log("Output: " + gameDir.string());

    // Create subdirs
    for (const char* sub : { "assets/image", "assets/sound", "assets/scripts", "assets/scenes" }) {
        fs::create_directories(gameDir / sub, ec);
    }

    // Copy shader files (src/*.glsl) — Renderer looks for them at "src/..." relative to cwd
    {
        fs::path shaderSrcDir("src");
        fs::path shaderDstDir = gameDir / "src";
        fs::create_directories(shaderDstDir, ec);
        int shaderCount = 0;
        if (fs::exists(shaderSrcDir)) {
            for (auto& entry : fs::directory_iterator(shaderSrcDir, ec)) {
                if (entry.path().extension() == ".glsl") {
                    if (copyFile(entry.path(), shaderDstDir / entry.path().filename(), log)) {
                        ++shaderCount;
                    }
                }
            }
        }
        if (shaderCount > 0) log("[OK] Shaders copied: " + std::to_string(shaderCount) + " file(s)");
        else log("[WARN] No .glsl files found in src/ — rendering may be broken");
    }

    // Load scene YAML
    std::ifstream sceneFile(cfg.scenePath);
    if (!sceneFile.is_open()) {
        log("[ERROR] Cannot open scene: " + cfg.scenePath);
        return false;
    }
    std::stringstream ss;
    ss << sceneFile.rdbuf();
    YAML::Node sceneNode;
    try {
        sceneNode = YAML::Load(ss.str());
    } catch (const std::exception& e) {
        log("[ERROR] Failed to parse scene YAML: " + std::string(e.what()));
        return false;
    }

    // Collect all asset paths referenced in the scene
    std::vector<std::string> rawPaths;
    collectPaths(sceneNode, rawPaths);

    // Deduplicate
    std::sort(rawPaths.begin(), rawPaths.end());
    rawPaths.erase(std::unique(rawPaths.begin(), rawPaths.end()), rawPaths.end());

    // Process each referenced file
    std::unordered_map<std::string, std::string> pathMap; // old -> new (relative to gameDir)
    for (const std::string& rawPath : rawPaths) {
        fs::path src(rawPath);
        if (!fs::exists(src)) {
            log("[WARN] File not found, skipping: " + rawPath);
            continue;
        }

        if (isScript(rawPath)) {
            fs::path dstDir = gameDir / "assets/scripts";
            std::string compiled = compileLuauInProc(src, dstDir, log);
            if (!compiled.empty()) {
                fs::path rel = fs::relative(compiled, gameDir, ec);
                pathMap[rawPath] = rel.string();
                log("[OK] Compiled: " + src.filename().string() + " -> " + rel.string());
            } else {
                // Fallback: copy source (compile failed)
                fs::path dst = dstDir / src.filename();
                if (copyFile(src, dst, log))
                    pathMap[rawPath] = "assets/scripts/" + src.filename().string();
            }
        } else {
            // Determine destination subdir from file extension
            std::string sub = assetSubdir(rawPath);
            // For relative paths, preserve directory structure under the subdir
            bool isRel = src.is_relative();
            fs::path dst;
            if (isRel) {
                dst = gameDir / rawPath;
            } else {
                dst = gameDir / sub / src.filename();
            }
            if (copyFile(src, dst, log)) {
                fs::path rel = fs::relative(dst, gameDir, ec);
                pathMap[rawPath] = rel.string();
                log("[OK] Copied: " + rawPath + " -> " + rel.string());
            }
        }
    }

    // Rewrite paths in YAML and write the scene file
    rewritePaths(sceneNode, pathMap);
    {
        YAML::Emitter emit;
        emit << sceneNode;
        fs::path sceneOut = gameDir / "assets/scenes" / (cfg.gameName + ".yaml");
        std::ofstream outFile(sceneOut);
        if (!outFile) { log("[ERROR] Cannot write scene YAML to: " + sceneOut.string()); return false; }
        outFile << emit.c_str();
        log("[OK] Scene written: " + sceneOut.string());
    }

    // Copy RecubinEngine.exe (game runtime) and sibling DLLs
    if (!cfg.engineExePath.empty()) {
        fs::path exeDir  = fs::path(cfg.engineExePath).parent_path();
        fs::path runtime = exeDir / "RecubinEngine.exe";
        if (fs::exists(runtime)) {
            copyFile(runtime, gameDir / "RecubinEngine.exe", log);
            log("[OK] Runtime: RecubinEngine.exe");
        } else {
            log("[WARN] RecubinEngine.exe not found next to editor exe — packaging editor exe instead");
            fs::path editorExe(cfg.engineExePath);
            if (fs::exists(editorExe)) {
                copyFile(editorExe, gameDir / editorExe.filename(), log);
                log("[WARN] Packaged editor exe: " + editorExe.filename().string());
            }
        }

        // Copy launcher.exe
        fs::path launcher = exeDir / "launcher.exe";
        if (fs::exists(launcher)) {
            copyFile(launcher, gameDir / "launcher.exe", log);
            log("[OK] launcher.exe");
        } else {
            log("[WARN] launcher.exe not found — run 'py build.py launcher Release' first");
        }

        // Copy all DLLs from the same directory
        for (auto& entry : fs::directory_iterator(exeDir, ec)) {
            if (entry.path().extension() == ".dll") {
                if (copyFile(entry.path(), gameDir / entry.path().filename(), log)) {
                    log("[OK] DLL: " + entry.path().filename().string());
                }
            }
        }
    }

    // Write startup.yaml (read by RecubinEngine.exe on launch)
    {
        YAML::Emitter startup;
        startup << YAML::BeginMap
                << YAML::Key << "GameName"   << YAML::Value << cfg.gameName
                << YAML::Key << "StartScene" << YAML::Value
                    << ("assets/scenes/" + cfg.gameName + ".yaml")
                << YAML::EndMap;
        std::ofstream startupFile(gameDir / "startup.yaml");
        if (startupFile) {
            startupFile << startup.c_str();
            log("[OK] startup.yaml written");
        }
    }

    // Write README.txt
    {
        std::ofstream readme(gameDir / "README.txt");
        if (readme) {
            readme << cfg.gameName << "\n\n";
            readme << "起動方法:\n";
            readme << "  RecubinEngine.exe\n\n";
            readme << "または launcher.exe から起動するとデスクトップショートカットを作成できます。\n";
        }
    }

    log("[DONE] Package created: " + gameDir.string());
    return true;
}

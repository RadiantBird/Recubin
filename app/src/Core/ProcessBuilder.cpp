#include <Core/ProcessBuilder.hpp>

#include <Util/AssetPath.hpp>
#include <Util/IPlatform.hpp>
#include <Util/Platform.hpp>
#include <Util/Logger.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>
#include <array>
#include <cstdio>
#include <cstring>
#include <string_view>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace ProcessBuilder {
namespace {
std::string g_msvcEnvironmentDiagnostic;
std::string escapeBatchValue(const std::string& value);

std::filesystem::path resolvePath(const std::filesystem::path& value,
                                  const std::filesystem::path& base) {
    if (value.empty() || value.is_absolute() || base.empty()) return value;
    return (base / value).lexically_normal();
}

std::string pathToUtf8(const std::filesystem::path& path) {
    return AssetPath::toStored(path);
}

std::optional<std::filesystem::file_time_type> writeTime(
    const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) return std::nullopt;
    const auto time = std::filesystem::last_write_time(path, error);
    if (error) return std::nullopt;
    return time;
}

bool needsRebuild(const std::filesystem::path& source,
                  const std::filesystem::path& output,
                  const std::vector<std::filesystem::path>& headers) {
    const auto outputTime = writeTime(output);
    if (!outputTime) return true;
    const auto sourceTime = writeTime(source);
    if (!sourceTime || *sourceTime > *outputTime) return true;
    for (const auto& header : headers) {
        const auto time = writeTime(header);
        if (!time || *time > *outputTime) return true;
    }
    return false;
}

void removeCompilerArtifacts(const std::filesystem::path& output) {
    const std::array<std::string_view, 3> extensions = {".exp", ".lib", ".pdb"};
    std::error_code error;
    for (const std::string_view extension : extensions) {
        std::filesystem::path artifact = output;
        artifact.replace_extension(std::string(extension));
        std::filesystem::remove(artifact, error);
        error.clear();
    }
}

#ifdef _WIN32
std::string wideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                        static_cast<int>(value.size()), nullptr, 0,
                                        nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        result.data(), size, nullptr, nullptr);
    return result;
}

std::string commandPath(const std::filesystem::path& path) {
    const std::wstring input = path.wstring();
    const DWORD needed = GetShortPathNameW(input.c_str(), nullptr, 0);
    if (needed == 0) {
        const auto parent = path.parent_path();
        if (parent.empty()) return pathToUtf8(path);
        const auto shortParent = commandPath(parent);
        if (shortParent == pathToUtf8(parent)) return pathToUtf8(path);
        std::filesystem::path joined(shortParent);
        joined /= path.filename();
        return pathToUtf8(joined.lexically_normal());
    }
    std::wstring shortPath(needed, L'\0');
    const DWORD written = GetShortPathNameW(input.c_str(), shortPath.data(), needed);
    if (written == 0 || written >= needed) return pathToUtf8(path);
    shortPath.resize(written);
    return pathToUtf8(std::filesystem::path(shortPath).lexically_normal());
}

std::map<std::string, std::string> msvcEnvironment(
    const std::string& vcvars, const std::filesystem::path& workingDirectory) {
    std::map<std::string, std::string> environment;
    g_msvcEnvironmentDiagnostic.clear();
    ChildProcessLaunchOptions options;
    options.executable = "C:\\Windows\\System32\\cmd.exe";
    const auto uniqueSuffix = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto envLog = workingDirectory /
        ("recubin-msvc-env-" + uniqueSuffix + ".log");
    const auto envScript = workingDirectory /
        ("recubin-msvc-env-" + uniqueSuffix + ".bat");
    options.outputLogPath = pathToUtf8(envLog);
    options.workingDirectory = pathToUtf8(workingDirectory);
    const auto vcvarsCommandPath = pathToUtf8(std::filesystem::path(vcvars));
    {
        std::ofstream script(envScript, std::ios::binary | std::ios::trunc);
        if (!script) {
            g_msvcEnvironmentDiagnostic = "failed to create environment script";
            return environment;
        }
        script << "@echo off\r\n"
               << "call \"" << escapeBatchValue(vcvarsCommandPath) << "\"\r\n"
               << "if errorlevel 1 exit /b %errorlevel%\r\n"
               << "set\r\n";
        script.close();
        if (!script) {
            g_msvcEnvironmentDiagnostic = "failed to write environment script";
            std::error_code ignored;
            std::filesystem::remove(envScript, ignored);
            return environment;
        }
    }
    const std::string commandExecutable = "\"" + options.executable + "\"";
    const std::string scriptNativePath = wideToUtf8(envScript.wstring());
    options.rawCommandLine = commandExecutable + " /d /u /s /c \"\"" +
        scriptNativePath + "\"\"";
    std::string cleanupDiagnostic;
    const auto removeTemporaryFile = [&](const std::filesystem::path& path) {
        for (int attempt = 0; attempt < 20; ++attempt) {
            std::error_code cleanupError;
            std::filesystem::remove(path, cleanupError);
            if (!cleanupError || !std::filesystem::exists(path)) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        if (cleanupDiagnostic.empty()) {
            cleanupDiagnostic = "failed to remove temporary environment file " + pathToUtf8(path);
        }
    };
    const auto cleanup = [&]() {
        removeTemporaryFile(envLog);
        removeTemporaryFile(envScript);
    };
    auto process = getPlatform().launchChildProcess(options);
    if (!process) {
        cleanup();
        g_msvcEnvironmentDiagnostic = "launch failed";
        return environment;
    }
    std::string allOutput;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (process->isRunning()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            process->terminate();
            const auto terminateDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (process->isRunning() && std::chrono::steady_clock::now() < terminateDeadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            process.reset();
            cleanup();
            g_msvcEnvironmentDiagnostic = "environment command timed out";
            return environment;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const int processExitCode = process->exitCode().value_or(-1);
    process.reset();
    std::ifstream envFile(envLog, std::ios::binary);
    if (envFile) allOutput.assign(std::istreambuf_iterator<char>(envFile), std::istreambuf_iterator<char>());
    envFile.close();
    cleanup();
    std::wstring wideOutput;
    if (allOutput.size() % sizeof(wchar_t) == 0) {
        wideOutput.resize(allOutput.size() / sizeof(wchar_t));
        std::memcpy(wideOutput.data(), allOutput.data(), allOutput.size());
        while (!wideOutput.empty() && wideOutput.back() == L'\0') wideOutput.pop_back();
        if (!wideOutput.empty() && wideOutput.front() == L'\ufeff') wideOutput.erase(0, 1);
    }
    std::wstring diagnosticOutput;
    std::size_t begin = 0;
    while (begin < wideOutput.size()) {
        const std::size_t end = wideOutput.find(L'\n', begin);
        std::wstring line = wideOutput.substr(begin,
            end == std::wstring::npos ? end : end - begin);
        while (!line.empty() && (line.back() == L'\r' || line.back() == L'\n')) line.pop_back();
        const std::size_t equal = line.find(L'=');
        if (equal != std::wstring::npos && equal > 0) {
            environment[wideToUtf8(line.substr(0, equal))] = wideToUtf8(line.substr(equal + 1));
        } else if (!line.empty() && diagnosticOutput.size() < 1024) {
            diagnosticOutput += line + L'\n';
        }
        if (end == std::wstring::npos) break;
        begin = end + 1;
    }
    g_msvcEnvironmentDiagnostic = "exitCode=" + std::to_string(processExitCode) +
        " output=" + wideToUtf8(diagnosticOutput) +
        " include=" + (environment.contains("INCLUDE") ? "present" : "missing") +
        " lib=" + (environment.contains("LIB") ? "present" : "missing") +
        " script=" + pathToUtf8(envScript);
    if (!cleanupDiagnostic.empty()) g_msvcEnvironmentDiagnostic += " " + cleanupDiagnostic;
    if (processExitCode != 0 || wideOutput.empty() ||
        !environment.contains("INCLUDE") || !environment.contains("LIB")) {
        environment.clear();
    }
    return environment;
}

struct MsvcToolchain { std::string compiler; std::string vcvars; };

std::optional<MsvcToolchain> findMsvcCompiler() {
    wchar_t path[MAX_PATH] = {};
    const DWORD length = SearchPathW(nullptr, L"cl.exe", nullptr,
                                     MAX_PATH, path, nullptr);
    if (length > 0 && length < MAX_PATH) {
        const std::string result = wideToUtf8(std::wstring(path, length));
        if (!result.empty()) {
            std::filesystem::path cl(result);
            auto vcvars = cl.parent_path().parent_path().parent_path().parent_path().parent_path().parent_path().parent_path() /
                          "Auxiliary" / "Build" / "vcvars64.bat";
            if (std::filesystem::is_regular_file(vcvars)) return MsvcToolchain{result, vcvars.string()};
        }
    }
    wchar_t tools[MAX_PATH] = {};
    const DWORD toolsLength = GetEnvironmentVariableW(L"VCToolsInstallDir", tools, MAX_PATH);
    if (toolsLength > 0 && toolsLength < MAX_PATH) {
        std::wstring candidate(tools, toolsLength);
        candidate += L"\\bin\\Hostx64\\x64\\cl.exe";
        if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
            const auto compilerPath = wideToUtf8(candidate);
            std::filesystem::path cl(candidate);
            auto vcvars = cl.parent_path().parent_path().parent_path().parent_path().parent_path().parent_path().parent_path() /
                          "Auxiliary" / "Build" / "vcvars64.bat";
            if (std::filesystem::is_regular_file(vcvars)) return MsvcToolchain{compilerPath, vcvars.string()};
        }
    }
    const std::array<std::filesystem::path, 2> roots = {
        std::filesystem::path("C:/Program Files/Microsoft Visual Studio"),
        std::filesystem::path("C:/Program Files (x86)/Microsoft Visual Studio")};
    // Avoid relying only on PATH: the Studio launcher normally does not
    // inherit a developer command prompt. Probe the stable VC layout first.
    for (const auto& root : roots) {
        std::error_code rootError;
        if (!std::filesystem::is_directory(root, rootError)) continue;
        for (const auto& version : std::filesystem::directory_iterator(root, rootError)) {
            if (rootError) break;
            for (const auto& edition : std::filesystem::directory_iterator(version.path(), rootError)) {
                if (rootError) break;
                const auto vcRoot = edition.path() / "VC" / "Tools" / "MSVC";
            std::error_code versionError;
            if (!std::filesystem::is_directory(vcRoot, versionError)) continue;
            for (const auto& toolset : std::filesystem::directory_iterator(vcRoot, versionError)) {
                if (versionError) break;
                const auto cl = toolset.path() / "bin" / "Hostx64" / "x64" / "cl.exe";
                const auto vcvars = edition.path() / "VC" / "Auxiliary" / "Build" / "vcvars64.bat";
                if (std::filesystem::is_regular_file(cl, versionError) &&
                    std::filesystem::is_regular_file(vcvars, versionError)) {
                    return MsvcToolchain{pathToUtf8(cl), pathToUtf8(vcvars)};
                }
            }
            }
        }
    }
    std::error_code error;
    for (const auto& root : roots) {
        if (!std::filesystem::is_directory(root, error)) continue;
        std::filesystem::recursive_directory_iterator entries(
            root, std::filesystem::directory_options::skip_permission_denied, error);
        for (const auto& entry : entries) {
            if (error) {
                error.clear();
                continue;
            }
            if (entry.path().filename() != "cl.exe") continue;
            const auto cl = entry.path();
            const auto vcvars = cl.parent_path().parent_path().parent_path().parent_path().parent_path().parent_path().parent_path() /
                                "Auxiliary" / "Build" / "vcvars64.bat";
            if (std::filesystem::is_regular_file(vcvars, error))
                return MsvcToolchain{cl.string(), vcvars.string()};
        }
    }
    return std::nullopt;
}

std::optional<MsvcToolchain> selectedMsvcCompiler(const std::filesystem::path& selected) {
    // Studio requires an explicit compiler selection. This prevents a
    // machine-dependent PATH/compiler choice from silently changing builds.
    if (selected.empty()) return std::nullopt;
    std::error_code error;
    if (!std::filesystem::is_regular_file(selected, error) || error) return std::nullopt;
    const auto cl = std::filesystem::absolute(selected, error);
    if (error) return std::nullopt;
    // cl.exe is under .../VC/Tools/MSVC/<version>/bin/Hostx64/x64.
    const auto vcvars = cl.parent_path().parent_path().parent_path().parent_path()
        .parent_path().parent_path().parent_path() / "Auxiliary" / "Build" / "vcvars64.bat";
    if (!std::filesystem::is_regular_file(vcvars, error) || error) return std::nullopt;
    return MsvcToolchain{pathToUtf8(cl), pathToUtf8(vcvars)};
}

std::optional<MsvcToolchain> selectedClangCl(const std::filesystem::path& selected) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(selected, error) || error) return std::nullopt;
    const std::filesystem::path compiler = std::filesystem::absolute(selected, error);
    if (error) return std::nullopt;
    for (std::filesystem::path current = compiler.parent_path(); !current.empty();
         current = current.parent_path()) {
        if (current.filename() == "VC") {
            const std::filesystem::path vcvars = current / "Auxiliary" / "Build" / "vcvars64.bat";
            if (std::filesystem::is_regular_file(vcvars, error) && !error)
                return MsvcToolchain{pathToUtf8(compiler), pathToUtf8(vcvars)};
        }
        error.clear();
        if (current == current.root_path()) break;
    }
    const std::optional<MsvcToolchain> msvc = findMsvcCompiler();
    if (!msvc) return std::nullopt;
    return MsvcToolchain{pathToUtf8(compiler), msvc->vcvars};
}

std::optional<CompilerKind> compilerKindFromPath(const std::filesystem::path& path) {
    if (path.empty()) return std::nullopt;
    std::string name = path.filename().string();
    std::transform(name.begin(), name.end(), name.begin(),
        [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    if (name == "cl.exe") return CompilerKind::Msvc;
    if (name == "clang-cl.exe") return CompilerKind::ClangCl;
    if (name.ends_with("g++.exe") || name.ends_with("gcc.exe")) return CompilerKind::Gnu;
    return std::nullopt;
}
#else
struct MsvcToolchain { std::string compiler; std::string vcvars; };
std::optional<MsvcToolchain> findMsvcCompiler() { return std::nullopt; }
std::optional<MsvcToolchain> selectedMsvcCompiler(const std::filesystem::path&) { return std::nullopt; }
std::optional<MsvcToolchain> selectedClangCl(const std::filesystem::path&) { return std::nullopt; }
std::optional<CompilerKind> compilerKindFromPath(const std::filesystem::path& path) {
    (void)path;
    return std::nullopt;
}
std::string commandPath(const std::filesystem::path& path) { return pathToUtf8(path); }
std::map<std::string, std::string> msvcEnvironment(
    const std::string&, const std::filesystem::path&) { return {}; }
#endif

std::string quoteForLog(const std::string& value) {
    std::string result = "\"";
    for (const char c : value) {
        if (c == '"') result += '\\';
        result += c;
    }
    result += '"';
    return result;
}

std::string quoteForCommand(const std::string& value) {
    // Every path is passed as one quoted command argument. In particular,
    // do not double '%' here: cmd.exe treats '%%' literally in /c mode and
    // would change valid user paths containing a percent sign.
    return quoteForLog(value);
}

std::string escapeBatchValue(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (const char c : value) {
        if (c == '%') escaped += "%%";
        else escaped += c;
    }
    return escaped;
}

} // namespace

std::optional<CompilerKind> compilerKindForPath(const std::filesystem::path& path) {
    return ProcessBuilder::compilerKindFromPath(path);
}

BuildResult buildIfNeeded(const BuildRequest& request) {
    BuildResult result;
    const auto base = request.assetBase.empty()
        ? AssetPath::sceneAssetBaseDirectory()
        : request.assetBase;
    const auto source = resolvePath(request.sourcePath, base);
    const auto output = resolvePath(request.dllPath, base);
    std::vector<std::filesystem::path> headers;
    headers.reserve(request.headerPaths.size());
    for (const auto& header : request.headerPaths) headers.push_back(resolvePath(header, base));

    if (source.empty() || output.empty()) {
        result.log = "Process build requires both sourcePath and dllPath.";
        return result;
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(source, error) || error) {
        result.log = "Process source does not exist: " + AssetPath::toStored(source);
        return result;
    }
    if (!needsRebuild(source, output, headers)) {
        result.success = true;
        result.log = "Process DLL is up to date: " + AssetPath::toStored(output);
        return result;
    }

    const std::optional<CompilerKind> selectedKind = compilerKindForPath(request.compilerPath);
    if (!selectedKind) {
        result.log = request.compilerPath.empty()
            ? "No Process compiler was selected."
            : "Unsupported Process compiler. Select cl.exe, clang-cl.exe, g++.exe, or gcc.exe.";
        return result;
    }
    const CompilerKind compilerKind = *selectedKind;
    std::optional<MsvcToolchain> compiler;
    if (compilerKind == CompilerKind::Msvc) {
        compiler = selectedMsvcCompiler(request.compilerPath);
    } else if (compilerKind == CompilerKind::ClangCl) {
        compiler = selectedClangCl(request.compilerPath);
    } else {
        std::error_code compilerError;
        if (!request.compilerPath.empty() &&
            std::filesystem::is_regular_file(request.compilerPath, compilerError) && !compilerError) {
            compiler = MsvcToolchain{pathToUtf8(std::filesystem::absolute(request.compilerPath)), {}};
        }
    }
    if (!compiler) {
        result.log = request.compilerPath.empty()
            ? "No Process compiler was selected."
            : "The selected compiler was not found or its required toolchain environment is unavailable: "
              + pathToUtf8(request.compilerPath);
        return result;
    }
    result.compilerFound = true;
    result.compiler = compiler->compiler;
    result.compilerKind = compilerKind;

    const auto parent = output.parent_path();
    std::filesystem::create_directories(parent, error);
    if (error) {
        result.log = "Failed to create Process DLL directory: " + pathToUtf8(parent);
        return result;
    }
    const auto temporary = output.parent_path() /
        (".recubin-process.building-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".dll");
    const auto wrapperPath = std::filesystem::temp_directory_path() /
        ("recubin-process-host-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".cpp");
    {
        std::ofstream wrapper(wrapperPath, std::ios::binary | std::ios::trunc);
        if (!wrapper) {
            result.log = "Failed to create the temporary Process host wrapper.";
            return result;
        }
        wrapper << "#include <recubin.h>\n"
                   "#include <exception>\n"
                   "#include <Windows.h>\n"
                   "extern \"C\" __declspec(dllexport) void rcbn_process_entry() { try { start(); } catch (const std::exception& e) { auto* api = recubin::process::recubin_process_host_api(); if (api && api->reportError) api->reportError(api->userdata, e.what()); } catch (...) { auto* api = recubin::process::recubin_process_host_api(); if (api && api->reportError) api->reportError(api->userdata, \"Unknown exception in Process start()\"); } }\n"
                   "";
        wrapper << "extern \"C\" __declspec(dllexport) void rcbn_process_stop_entry() { HMODULE module = nullptr; if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&rcbn_process_entry), &module)) { auto* api = recubin::process::recubin_process_host_api(); if (api && api->reportError) api->reportError(api->userdata, \"Failed to resolve Process DLL\"); return; } auto fn = reinterpret_cast<RCBNProcessStopFn>(GetProcAddress(module, \"stop\")); if (!fn) return; try { fn(); } catch (const std::exception& e) { auto* api = recubin::process::recubin_process_host_api(); if (api && api->reportError) api->reportError(api->userdata, e.what()); } catch (...) { auto* api = recubin::process::recubin_process_host_api(); if (api && api->reportError) api->reportError(api->userdata, \"Unknown exception in Process stop()\"); } }\n";
        wrapper << "static const RCBNProcessHostApi* g_api = nullptr;\n"
                       "extern \"C\" __declspec(dllexport) void rcbn_process_bind_host(const RCBNProcessHostApi* api) { g_api = api; recubin::process::InstallHostApi(api); }\n"
                       "extern \"C\" __declspec(dllexport) void rcbn_process_unbind_host(const RCBNProcessHostApi* api) { recubin::process::UninstallHostApi(api); if (g_api == api) g_api = nullptr; }\n"
                       "extern \"C\" const RCBNProcessHostApi* recubin_process_host_api() { return g_api; }\n";
    }

    ChildProcessLaunchOptions options;
    options.executable = "cmd.exe";
    options.workingDirectory = base.empty() ? pathToUtf8(std::filesystem::current_path())
                                            : pathToUtf8(base);
    options.executable = compiler->compiler;
    if (compilerKind == CompilerKind::Gnu) {
        options.arguments = {"-shared", "-static-libgcc", "-static-libstdc++", "-fexceptions",
            "-std=c++23", "-o", commandPath(temporary), commandPath(wrapperPath), commandPath(source)};
        for (const auto& include : request.includeDirectories) {
            options.arguments.push_back("-I" + commandPath(resolvePath(include, base)));
        }
    } else {
        options.arguments = {"/nologo", "/LD", "/EHsc", "/utf-8", "/std:c++latest",
                             commandPath(wrapperPath), commandPath(source), "/Fe:" + commandPath(temporary)};
        for (const auto& include : request.includeDirectories) {
            options.arguments.push_back("/I" + commandPath(resolvePath(include, base)));
        }
        options.environment = msvcEnvironment(compiler->vcvars, parent);
        if (options.environment.empty()) {
            result.log = "Failed to initialize the selected compiler environment: " + g_msvcEnvironmentDiagnostic;
            std::filesystem::remove(wrapperPath, error);
            return result;
        }
    }

    std::ostringstream command;
    command << quoteForLog(options.executable);
    for (const auto& argument : options.arguments) command << ' ' << quoteForLog(argument);
    result.log = command.str() + "\n";

    auto process = getPlatform().launchPipedProcess(options);
    if (!process) {
        result.log += "Failed to launch the selected Process compiler.\n";
        std::filesystem::remove(wrapperPath, error);
        return result;
    }
    while (process->isRunning()) {
        std::string outputChunk;
        if (process->readAvailable(outputChunk)) result.log += outputChunk;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    std::string outputChunk;
    process->readAvailable(outputChunk);
    result.log += outputChunk;
    std::filesystem::remove(wrapperPath, error);
    result.exitCode = process->exitCode().value_or(-1);
    if (result.exitCode != 0 || !std::filesystem::is_regular_file(temporary, error) || error) {
        result.log += "Compiler failed; the previous DLL was not used.\n";
        std::filesystem::remove(temporary, error);
        removeCompilerArtifacts(temporary);
        return result;
    }

    const auto backup = output.parent_path() /
        (".recubin-process.previous-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".dll");
    std::error_code moveError;
    const bool hadOutput = std::filesystem::exists(output, moveError) && !moveError;
    if (hadOutput) {
        std::filesystem::rename(output, backup, moveError);
        if (moveError) {
            result.log += "Failed to stage the previous Process DLL for replacement: " +
                          AssetPath::toStored(output) + "\n";
            std::filesystem::remove(temporary, error);
            removeCompilerArtifacts(temporary);
            return result;
        }
    }
    std::filesystem::rename(temporary, output, moveError);
    if (moveError) {
        result.log += "Failed to install the newly built Process DLL: " +
                      AssetPath::toStored(output) + "\n";
        if (hadOutput) {
            std::error_code restoreError;
            std::filesystem::rename(backup, output, restoreError);
            if (restoreError) result.log += "Failed to restore the previous Process DLL.\n";
        }
        std::filesystem::remove(temporary, error);
        removeCompilerArtifacts(temporary);
        return result;
    }
    removeCompilerArtifacts(temporary);
    std::error_code removeError;
    if (hadOutput) std::filesystem::remove(backup, removeError);
    if (removeError) {
        result.log += "Warning: temporary Process DLL could not be removed: " +
                      pathToUtf8(temporary) + "\n";
    }
    result.success = true;
    result.rebuilt = true;
    result.log += "Process DLL build completed successfully.\n";
    return result;
}

} // namespace ProcessBuilder

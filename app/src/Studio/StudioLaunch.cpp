#include "Studio.hpp"

#ifdef _WIN32
    #include <windows26.h>
#endif
#ifdef __APPLE__
    #include <mach-o/dyld.h>
#endif

#include <Core/Physics.hpp>
#include <Core/AudioService.hpp>
#include <Core/PhysicalFileInstanceRegistry.hpp>
#include <Core/RbxlxConverter.hpp>
#include <Core/SceneLoader.hpp>
#include <Editor/GuiAutomation.hpp>
#include <Editor/Localization.hpp>
#include <Instances/AppImage.hpp>
#include <Instances/Lighting.hpp>
#include <Instances/Script.hpp>
#include <Instances/System.hpp>
#include <Instances/Workspace.hpp>
#include <Util/AssetPath.hpp>
#include <Util/EditorLaunchPath.hpp>
#include <Util/IPlatform.hpp>
#include <Util/Logger.hpp>
#include <Util/Platform.hpp>
#include <Util/RuntimeLaunchArgs.hpp>

#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace StudioMain {
namespace {

std::string absoluteDisplayPath(const std::filesystem::path& path)
{
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    return AssetPath::toStored((error ? path : absolute).lexically_normal());
}

// --convert <in.rbxlx> [-o <out.rcbn>]。指定が無ければ nullopt（通常起動を続ける）。
// -o を省略すると入力と同じ場所の .rcbn に出力する。
static std::optional<int> runConvertFromArgs(int argc, char* argv[]) {
    std::optional<std::filesystem::path> input;
    std::optional<std::filesystem::path> output;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--convert") {
            if (i + 1 >= argc) {
                std::cerr << "[convert] ERROR: --convert requires an input .rbxlx path\n";
                return 1;
            }
            input = std::filesystem::path(argv[++i]);
        } else if (arg == "-o") {
            if (i + 1 >= argc) {
                std::cerr << "[convert] ERROR: -o requires an output path\n";
                return 1;
            }
            output = std::filesystem::path(argv[++i]);
        }
    }
    if (!input) return std::nullopt;
    if (!output) output = std::filesystem::path(*input).replace_extension(".rcbn");

    const RbxlxConverter::Result result = RbxlxConverter::convertFile(*input, *output);
    for (const std::string& warning : result.warnings) {
        std::cout << "[convert] WARNING: " << warning << "\n";
    }
    if (!result) {
        std::cerr << "[convert] ERROR: " << result.message << "\n";
        return 1;
    }
    std::cout << "[convert] " << result.message << " (skipped " << result.skippedCount
              << " Roblox instances)\n";
    return 0;
}

static int runGenTestScene(const std::string& outputPath) {
    getPlatform().setupConsoleUtf8();

    auto audioService = std::make_unique<AudioService>();
    if (!audioService->initialize()) {
        std::cerr << "[gen-test-scene] ERROR: AudioService initialization failed\n";
        return 1;
    }

    auto system    = std::make_shared<System>();
    auto workspace = std::make_shared<Workspace>();
    system->addChild(workspace);

    // SceneLoader::createInstance() が対応する全クラス（System/Workspace/
    // PathfindingService/Script は特殊なため除外。Script は末尾で検証用に個別追加する）。
    // createInstance() に新しいクラスを追加した場合はここにも追加すること。
    static const char* kClassNames[] = {
        "Cube", "Cylinder", "TriangularPrism", "Wedge", "Truss", "Seat", "Sphere", "MeshCube", "LiquidCube", "SpawnLocation",
        "Skybox", "Sun", "Moon", "Model", "Sound", "SurfaceMark",
        "Lighting", "Material", "PointLight", "SpotLight", "PostEffect",
        "AppImage", "Humanoid", "Animation", "StarterCharacter", "Terrain", "Instance",
        "Rope", "Rod", "Spring", "PrismaticConstraint", "Weld", "Motor", "Attachment", "Force",
        "TextLabel", "TextButton", "ImageLabel", "ImageButton", "SurfaceGui", "BillboardGui",
        "ProximityPrompt", "Folder", "Tool", "ParticleEmitter", "Weather",
    };

    int generated = 0;
    for (const char* className : kClassNames) {
        auto inst = SceneLoader::createInstance(className);
        if (!inst) {
            std::cout << "[gen-test-scene] WARNING: failed to create " << className << "\n";
            continue;
        }
        inst->Name = className;
        workspace->addChild(inst);
        ++generated;
    }
    for (const auto& type : PhysicalFileInstanceRegistry::types()) {
        auto inst = PhysicalFileInstanceRegistry::create(type.className);
        if (!inst) {
            std::cout << "[gen-test-scene] WARNING: failed to create "
                      << type.className << "\n";
            continue;
        }
        inst->Name = std::string(type.className);
        workspace->addChild(inst);
        ++generated;
    }

    // Decal/Texture はBaseCubeの子としてのみ意味を持つため、専用の親Cubeを作って生成する
    auto decalHost = SceneLoader::createInstance("Cube");
    decalHost->Name = "DecalHost";
    workspace->addChild(decalHost);
    for (const char* className : { "Decal", "Texture" }) {
        auto inst = SceneLoader::createInstance(className);
        if (!inst) continue;
        inst->Name = className;
        decalHost->addChild(inst);
    }

    // 検証用スクリプト: 代表的なインスタンスが揃っているかを確認する
    // (scripts/gen_test_scene_check.luau)。ContentPath参照にしているのは、
    // SceneLoader::saveNode がScriptのSourceを直接保存せずContentPathのみ
    // 書き出すため（インラインSourceは保存→再ロードで消えてしまう）。
    auto script = std::make_shared<Script>("scripts/gen_test_scene_check.luau");
    script->Name = "GenTestSceneCheck";
    workspace->addChild(script);

    SceneLoader::saveScene(system.get(), outputPath);
    std::cout << "[gen-test-scene] Wrote " << generated << " instances (+DecalHost, +check script) to "
              << outputPath << "\n";
    return 0;
}

// ===================================================
//  main
// ===================================================
#ifdef _WIN32
struct EditorExecutableLocation {
    std::filesystem::path executablePath;
    std::filesystem::path storageRoot;
};

static EditorExecutableLocation resolveEditorExecutableLocation(int argc, char* argv[]) {
    std::vector<wchar_t> buffer(32768, L'\0');
    SetLastError(ERROR_SUCCESS);
    const DWORD length = GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length > 0 && length < buffer.size()) {
        const std::filesystem::path executable(
            std::wstring(buffer.data(), length));
        return {executable.lexically_normal(), executable.parent_path().lexically_normal()};
    }

    const DWORD moduleError = GetLastError();
    if (length == 0) {
        RCBN_ERROR("GetModuleFileNameW failed while resolving the editor executable "
                   "(error " << moduleError << "); falling back to argv[0]");
    } else {
        RCBN_ERROR("GetModuleFileNameW returned a truncated editor executable path "
                   "(buffer size " << buffer.size() << ", error " << moduleError
                   << "); falling back to argv[0]");
    }

    std::error_code pathError;
    if (argc > 0 && argv[0] && argv[0][0] != '\0') {
        const auto executable = std::filesystem::absolute(
            std::filesystem::path(argv[0]), pathError);
        if (!pathError) {
            const auto normalized = executable.lexically_normal();
            return {normalized, normalized.parent_path()};
        }
        RCBN_ERROR("Failed to make argv[0] absolute while resolving the editor "
                   "executable: " << pathError.message()
                   << "; falling back to the current working directory");
    } else {
        RCBN_ERROR("argv[0] is unavailable while resolving the editor executable; "
                   "falling back to the current working directory");
    }

    pathError.clear();
    const auto currentDirectory = std::filesystem::current_path(pathError);
    if (pathError) {
        RCBN_ERROR("Failed to resolve the current working directory for Autosave: "
                   << pathError.message());
        return {std::filesystem::path("Recubin.exe"), std::filesystem::path(".")};
    }
    const auto normalizedDirectory = currentDirectory.lexically_normal();
    return {normalizedDirectory / "Recubin.exe", normalizedDirectory};
}
#elif defined(__APPLE__)
static std::filesystem::path resolveMacEditorExecutablePath(int argc, char* argv[]) {
    uint32_t bufferSize = 0;
    _NSGetExecutablePath(nullptr, &bufferSize);
    if (bufferSize > 0) {
        std::vector<char> buffer(bufferSize, '\0');
        if (_NSGetExecutablePath(buffer.data(), &bufferSize) == 0) {
            std::error_code canonicalError;
            const auto executable = std::filesystem::weakly_canonical(
                std::filesystem::path(buffer.data()), canonicalError);
            if (!canonicalError) return executable.lexically_normal();
            RCBN_ERROR("Failed to canonicalize _NSGetExecutablePath result: "
                       << canonicalError.message() << "; falling back to argv[0]");
        } else {
            RCBN_ERROR("_NSGetExecutablePath failed with buffer size " << bufferSize
                       << "; falling back to argv[0]");
        }
    } else {
        RCBN_ERROR("_NSGetExecutablePath did not provide a buffer size; falling back to argv[0]");
    }

    std::error_code pathError;
    if (argc > 0 && argv[0] && argv[0][0] != '\0') {
        const auto executable = std::filesystem::absolute(argv[0], pathError);
        if (!pathError) return executable.lexically_normal();
        RCBN_ERROR("Failed to make argv[0] absolute while resolving the editor executable: "
                   << pathError.message() << "; falling back to the current working directory");
    }
    pathError.clear();
    const auto currentDirectory = std::filesystem::current_path(pathError);
    if (!pathError) return (currentDirectory / "Recubin").lexically_normal();
    RCBN_ERROR("Failed to resolve the current working directory for the editor executable: "
               << pathError.message());
    return "Recubin";
}
#endif

void startWatcher()
{
#ifdef _WIN32
    const DWORD pid = GetCurrentProcessId();
    std::string command = "Watcher.exe " + std::to_string(pid);
    STARTUPINFOA startupInfo = {sizeof(startupInfo)};
    PROCESS_INFORMATION processInfo = {};
    if (CreateProcessA(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                       nullptr, &startupInfo, &processInfo)) {
        CloseHandle(processInfo.hProcess);
        CloseHandle(processInfo.hThread);
    } else {
        std::cerr << "Failed to start watcher.exe: " << GetLastError() << '\n';
    }
#endif
}

} // namespace

std::variant<int, LaunchOptions> prepareLaunch(int argc, char* argv[])
{
    GuiAutomation::configureFromArgs(argc, argv);
    const RuntimeLaunchArgs runtimeArgs = parseRuntimeLaunchArgs(argc, argv);
    if (!runtimeArgs.valid) {
        RCBN_ERROR("Invalid runtime arguments: " << runtimeArgs.error);
        return -1;
    }

    const bool hasAutomationScene = runtimeArgs.uiAutomationScene.has_value() ||
                                    runtimeArgs.uiAutomationSettings.has_value();
    const bool hasAutomationFixture = hasAutomationScene || runtimeArgs.uiAutomationLayout.has_value();
    if (hasAutomationFixture && !GuiAutomation::enabled()) {
        RCBN_ERROR("UI automation fixture arguments require --ui-automation");
        return -1;
    }
    if (hasAutomationScene &&
        (!runtimeArgs.uiAutomationScene || !runtimeArgs.uiAutomationSettings)) {
        RCBN_ERROR("--ui-automation-scene and --ui-automation-settings must be provided together");
        return -1;
    }
    if (hasAutomationScene) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(*runtimeArgs.uiAutomationScene, error)) {
            RCBN_ERROR("Invalid --ui-automation-scene path: "
                       << *runtimeArgs.uiAutomationScene);
            return -1;
        }
    }
    if (runtimeArgs.uiAutomationLayout) {
        if (!runtimeArgs.gpuDriver) {
            RCBN_ERROR("--ui-automation-layout requires an SDL GPU Studio driver");
            return -1;
        }
        std::error_code error;
        if (!std::filesystem::is_regular_file(*runtimeArgs.uiAutomationLayout, error)) {
            RCBN_ERROR("Invalid --ui-automation-layout path: "
                       << *runtimeArgs.uiAutomationLayout);
            return -1;
        }
    }

    getPlatform().setupConsoleUtf8();
    if (const auto conversionResult = runConvertFromArgs(argc, argv)) {
        return *conversionResult;
    }
    GuiAutomation::start();
    getPlatform().setupDllSearchPath();
    if (!Physics::configureBackendFromCommandLine(argc, argv)) {
        return -1;
    }

    std::cout << "Hello world!\nRecubin Studio v Delta_1.0\nbuild: "
              << __DATE__ << ' ' << __TIME__ << '\n';

    LaunchOptions options;
    options.initialScenePath = runtimeArgs.uiAutomationScene.value_or("");
    options.automationLayoutPath = runtimeArgs.uiAutomationLayout;
    options.gpuDriver = runtimeArgs.gpuDriver;
    options.settingsPath = hasAutomationScene
        ? AssetPath::fromStored(*runtimeArgs.uiAutomationSettings)
        : std::filesystem::path("editor_settings.yaml");
#ifdef _WIN32
    const auto location = resolveEditorExecutableLocation(argc, argv);
    options.executablePath = location.executablePath;
    options.autosaveRoot = location.storageRoot;
    startWatcher();
#elif defined(__APPLE__)
    std::error_code launchError;
    auto launchRoot = std::filesystem::current_path(launchError);
    if (launchError) {
        RCBN_ERROR("Failed to resolve the launch working directory: " << launchError.message());
        launchRoot = ".";
    }
    const auto executable = resolveMacEditorExecutablePath(argc, argv);
    const auto selectedRoot = selectMacEditorRoot(executable, launchRoot);
    options.executablePath = executable;
    options.autosaveRoot = selectedRoot.root;
    if (selectedRoot.portable) {
        std::error_code changeError;
        std::filesystem::current_path(selectedRoot.root, changeError);
        if (changeError) {
            RCBN_ERROR("Failed to switch to the portable Studio root "
                       << AssetPath::toStored(selectedRoot.root) << ": " << changeError.message());
            showInternalIoFailure(
                "Use portable Studio folder", selectedRoot.root, changeError.message());
            options.autosaveRoot = launchRoot;
        } else if (!hasAutomationScene) {
            options.settingsPath = (selectedRoot.root / "editor_settings.yaml").lexically_normal();
        }
    }
#else
    std::error_code rootError;
    options.autosaveRoot = std::filesystem::current_path(rootError);
    if (rootError) {
        RCBN_ERROR("Failed to resolve the current working directory for Autosave: "
                   << rootError.message());
        options.autosaveRoot = ".";
    }
    std::error_code executableError;
    options.executablePath = (argc > 0 && argv[0])
        ? std::filesystem::absolute(argv[0], executableError)
        : std::filesystem::path();
#endif

    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--gen-test-scene" && i + 1 < argc) {
            return runGenTestScene(argv[i + 1]);
        }
    }
    return options;
}

void showInternalIoFailure(
    const std::string& operation,
    const std::filesystem::path& path,
    const std::string& reason)
{
    std::string message = Loc::t(Loc::LocKey::InternalIoPermissionHint);
    message += "\n\n";
    message += Loc::t(Loc::LocKey::InternalIoOperationLabel);
    message += ": " + operation + "\n";
    message += Loc::t(Loc::LocKey::InternalIoPathLabel);
    message += ": " + absoluteDisplayPath(path) + "\n";
    message += Loc::t(Loc::LocKey::InternalIoReasonLabel);
    message += ": " + reason;
    getPlatform().showErrorDialog(Loc::t(Loc::LocKey::InternalIoErrorTitle), message);
}

} // namespace StudioMain

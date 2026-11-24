#include <Util/RuntimeLaunchArgs.hpp>

#include <string_view>
#include <utility>

namespace {

bool readValue(int argc, char* argv[], int& index, std::string& value) {
    if (index + 1 >= argc) return false;
    const std::string_view candidate = argv[index + 1];
    if (candidate.empty() || candidate.starts_with("--")) return false;
    value.assign(candidate);
    ++index;
    return true;
}

void setError(RuntimeLaunchArgs& args, const std::string& message) {
    if (args.valid) args.error = message;
    args.valid = false;
}

std::optional<SdlGpuDriver> parseGpuDriver(std::string_view value) {
    if (value == "auto") return SdlGpuDriver::Auto;
    if (value == "vulkan") return SdlGpuDriver::Vulkan;
    if (value == "direct3d12") return SdlGpuDriver::Direct3D12;
    if (value == "metal") return SdlGpuDriver::Metal;
    return std::nullopt;
}

} // namespace

RuntimeLaunchArgs parseRuntimeLaunchArgs(int argc, char* argv[]) {
    RuntimeLaunchArgs args;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--scene") {
            std::string value;
            if (args.scenePath.has_value()) {
                setError(args, "--scene may only be specified once");
            } else if (!readValue(argc, argv, index, value)) {
                setError(args, "--scene requires a non-empty path");
            } else {
                args.scenePath = std::move(value);
            }
        } else if (argument == "--window-title") {
            std::string value;
            if (args.windowTitle.has_value()) {
                setError(args, "--window-title may only be specified once");
            } else if (!readValue(argc, argv, index, value)) {
                setError(args, "--window-title requires a non-empty title");
            } else {
                args.windowTitle = std::move(value);
            }
        } else if (argument == "--editor-test") {
            if (args.editorTest) {
                setError(args, "--editor-test may only be specified once");
            } else {
                args.editorTest = true;
            }
        } else if (argument == "--ui-automation-scene") {
            std::string value;
            if (args.uiAutomationScene.has_value()) {
                setError(args, "--ui-automation-scene may only be specified once");
            } else if (!readValue(argc, argv, index, value)) {
                setError(args, "--ui-automation-scene requires a non-empty path");
            } else {
                args.uiAutomationScene = std::move(value);
            }
        } else if (argument == "--ui-automation-settings") {
            std::string value;
            if (args.uiAutomationSettings.has_value()) {
                setError(args, "--ui-automation-settings may only be specified once");
            } else if (!readValue(argc, argv, index, value)) {
                setError(args, "--ui-automation-settings requires a non-empty path");
            } else {
                args.uiAutomationSettings = std::move(value);
            }
        } else if (argument == "--ui-automation-layout") {
            std::string value;
            if (args.uiAutomationLayout.has_value()) {
                setError(args, "--ui-automation-layout may only be specified once");
            } else if (!readValue(argc, argv, index, value)) {
                setError(args, "--ui-automation-layout requires a non-empty path");
            } else {
                args.uiAutomationLayout = std::filesystem::u8path(value);
            }
        } else if (argument == "--self-check") {
            std::string value;
            if (args.selfCheckDir.has_value()) {
                setError(args, "--self-check may only be specified once");
            } else if (!readValue(argc, argv, index, value)) {
                setError(args, "--self-check requires a non-empty output directory");
            } else {
                args.selfCheckDir = std::move(value);
            }
        } else if (argument == "--gpu-preview") {
            if (args.gpuPreview) {
                setError(args, "--gpu-preview may only be specified once");
            }
            args.gpuPreview = true;
        } else if (argument == "--gpu-driver" || argument.starts_with("--gpu-driver=")) {
            std::string value;
            if (args.gpuDriver.has_value()) {
                setError(args, "--gpu-driver may only be specified once");
                continue;
            }
            if (argument == "--gpu-driver") {
                readValue(argc, argv, index, value);
            } else {
                value = argument.substr(std::string_view("--gpu-driver=").size());
            }
            args.gpuDriver = parseGpuDriver(value);
            if (!args.gpuDriver) {
                setError(args, "--gpu-driver requires auto, vulkan, direct3d12, or metal");
            }
        } else if (argument.starts_with("--scene=") ||
                   argument.starts_with("--window-title=") ||
                   argument.starts_with("--editor-test=") ||
                   argument.starts_with("--ui-automation-scene=") ||
                   argument.starts_with("--ui-automation-settings=") ||
                   argument.starts_with("--ui-automation-layout=") ||
                   argument.starts_with("--self-check=") ||
                   argument.starts_with("--gpu-preview=")) {
            setError(args, "launch options require separate values");
        }
    }
    return args;
}

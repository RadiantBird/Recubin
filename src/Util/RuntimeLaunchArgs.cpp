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
        } else if (argument.starts_with("--scene=") ||
                   argument.starts_with("--window-title=") ||
                   argument.starts_with("--editor-test=")) {
            setError(args, "use --scene <path>, --window-title <title>, and --editor-test");
        }
    }
    return args;
}

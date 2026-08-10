#pragma once

#include <optional>
#include <string>

struct RuntimeLaunchArgs {
    std::optional<std::string> scenePath;
    std::optional<std::string> windowTitle;
    bool editorTest = false;
    bool valid = true;
    std::string error;
};

// RecubinEngine固有の引数だけを読む。ネットワーク/物理など他機能の引数は無視する。
RuntimeLaunchArgs parseRuntimeLaunchArgs(int argc, char* argv[]);

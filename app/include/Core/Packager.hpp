#pragma once
#include <string>
#include <functional>

class Packager {
public:
    struct Config {
        std::string gameName;
        std::string outputDir;
        std::string scenePath;
        std::string engineExePath;
    };

    // Returns true on success. Progress lines are delivered via log callback.
    static bool package(const Config& cfg, std::function<void(const std::string&)> log);
};

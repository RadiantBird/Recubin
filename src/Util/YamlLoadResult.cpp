#include <Util/YamlLoadResult.hpp>

#include <fstream>
#include <sstream>
#include <cerrno>
#include <system_error>

namespace {
std::string ioReason(const char* fallback) {
    return errno != 0 ? std::generic_category().message(errno) : fallback;
}
}

YamlLoadResult loadYamlText(std::string_view text, std::string_view source) {
    YamlLoadResult result;
    try {
        result.node = YAML::Load(std::string(text));
        result.success = true;
    } catch (const std::exception& error) {
        result.loadFailed = true;
        result.failureKind = YamlFailureKind::Parse;
        result.error = std::string(source) + ": " + error.what();
    } catch (...) {
        result.loadFailed = true;
        result.failureKind = YamlFailureKind::Parse;
        result.error = std::string(source) + ": unknown YAML parse error";
    }
    return result;
}

YamlLoadResult loadYamlFile(const std::string& path) {
    errno = 0;
    std::ifstream file(path);
    if (!file.is_open()) {
        YamlLoadResult result;
        result.loadFailed = true;
        result.failureKind = YamlFailureKind::Io;
        result.error = path + ": " + ioReason("unable to open file");
        return result;
    }
    std::stringstream content;
    content << file.rdbuf();
    if (file.bad()) {
        YamlLoadResult result;
        result.loadFailed = true;
        result.failureKind = YamlFailureKind::Io;
        result.error = path + ": " + ioReason("read failed");
        return result;
    }
    return loadYamlText(content.str(), path);
}

YamlSaveResult saveYamlFileGuarded(const std::string& path,
                                   const YAML::Node& node,
                                   bool loadFailed) {
    YamlSaveResult result;
    if (loadFailed) {
        result.failureKind = YamlFailureKind::Guard;
        result.error = path + ": refusing to overwrite a failed YAML load";
        return result;
    }
    try {
        YAML::Emitter out;
        out << node;
        if (!out.good()) {
            result.failureKind = YamlFailureKind::Emit;
            result.error = path + ": YAML emitter failed";
            return result;
        }
        errno = 0;
        std::ofstream file(path);
        if (!file.is_open()) {
            result.failureKind = YamlFailureKind::Io;
            result.error = path + ": " + ioReason("unable to open file for writing");
            return result;
        }
        file << out.c_str();
        file.flush();
        if (!file) {
            result.failureKind = YamlFailureKind::Io;
            result.error = path + ": " + ioReason("write failed");
            return result;
        }
        result.success = true;
    } catch (const std::exception& error) {
        result.failureKind = YamlFailureKind::Emit;
        result.error = path + ": " + error.what();
    } catch (...) {
        result.failureKind = YamlFailureKind::Emit;
        result.error = path + ": unknown YAML write error";
    }
    return result;
}

#include <Util/YamlLoadResult.hpp>

#include <fstream>
#include <sstream>

YamlLoadResult loadYamlText(std::string_view text, std::string_view source) {
    YamlLoadResult result;
    try {
        result.node = YAML::Load(std::string(text));
        result.success = true;
    } catch (const std::exception& error) {
        result.loadFailed = true;
        result.error = std::string(source) + ": " + error.what();
    } catch (...) {
        result.loadFailed = true;
        result.error = std::string(source) + ": unknown YAML parse error";
    }
    return result;
}

YamlLoadResult loadYamlFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        YamlLoadResult result;
        result.loadFailed = true;
        result.error = path + ": unable to open file";
        return result;
    }
    std::stringstream content;
    content << file.rdbuf();
    return loadYamlText(content.str(), path);
}

YamlSaveResult saveYamlFileGuarded(const std::string& path,
                                   const YAML::Node& node,
                                   bool loadFailed) {
    YamlSaveResult result;
    if (loadFailed) {
        result.error = path + ": refusing to overwrite a failed YAML load";
        return result;
    }
    try {
        YAML::Emitter out;
        out << node;
        std::ofstream file(path);
        if (!file.is_open()) {
            result.error = path + ": unable to open file for writing";
            return result;
        }
        file << out.c_str();
        file.flush();
        if (!file) {
            result.error = path + ": write failed";
            return result;
        }
        result.success = true;
    } catch (const std::exception& error) {
        result.error = path + ": " + error.what();
    } catch (...) {
        result.error = path + ": unknown YAML write error";
    }
    return result;
}

#include <Core/AssetDependencies.hpp>
#include <Util/AssetPath.hpp>
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

namespace AssetDependencies {
namespace {
bool containsKey(const std::vector<std::string_view>& keys, std::string_view key) {
    return std::find(keys.begin(), keys.end(), key) != keys.end();
}

bool hasAutosaveComponent(const fs::path& path) {
    for (const auto& component : path) {
        if (component == ".autosave") return true;
    }
    return false;
}

std::string lowerExtension(std::string_view path) {
    std::string ext = AssetPath::toStored(AssetPath::fromStored(path).extension());
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

void addDependency(const std::string& source, bool directory, std::vector<Dependency>& out) {
    if (source.empty()) return;
    const auto duplicate = std::find_if(out.begin(), out.end(), [&](const Dependency& d) {
        return d.source == source && d.directory == directory;
    });
    if (duplicate != out.end()) return;

    Dependency dependency;
    dependency.source = source;
    dependency.directory = directory;
    const fs::path path = AssetPath::fromStored(source);
    dependency.absolute = path.is_absolute();
    dependency.autosave = hasAutosaveComponent(path);
    std::error_code ec;
    if (directory) {
        dependency.exists = fs::is_directory(path, ec);
    } else {
        dependency.exists = fs::is_regular_file(path, ec);
        dependency.executable = isExecutablePath(source);
    }
    if (dependency.exists && !dependency.autosave) {
        for (const auto& entry : listFiles(dependency)) {
            std::error_code sizeEc;
            const auto bytes = fs::file_size(entry.path, sizeEc);
            if (!sizeEc) dependency.size += bytes;
            if (directory && isExecutablePath(entry.relative)) dependency.executable = true;
        }
    }
    out.push_back(std::move(dependency));
}

void collectNode(const YAML::Node& node, std::vector<Dependency>& out) {
    if (node.IsMap()) {
        for (auto it = node.begin(); it != node.end(); ++it) {
            const std::string key = it->first.as<std::string>("");
            // it-> は一時プロキシを返すため、参照ではなくNodeハンドルをコピーして保持する。
            const YAML::Node value = it->second;
            if (containsKey(fileKeys(), key) && value.IsScalar()) {
                addDependency(value.as<std::string>(""), false, out);
            } else if (containsKey(directoryKeys(), key) && value.IsScalar()) {
                addDependency(value.as<std::string>(""), true, out);
            } else if (containsKey(sequenceKeys(), key) && value.IsSequence()) {
                for (const auto& element : value) {
                    if (element.IsScalar()) addDependency(element.as<std::string>(""), false, out);
                }
            } else {
                collectNode(value, out);
            }
        }
    } else if (node.IsSequence()) {
        for (const auto& child : node) collectNode(child, out);
    }
}

void rewriteScalar(YAML::Node value, const std::unordered_map<std::string, std::string>& map) {
    const auto found = map.find(value.as<std::string>(""));
    if (found != map.end()) value = found->second;
}

void rewriteNode(YAML::Node node, const std::unordered_map<std::string, std::string>& map) {
    if (node.IsMap()) {
        for (auto it = node.begin(); it != node.end(); ++it) {
            const std::string key = it->first.as<std::string>("");
            YAML::Node value = it->second;
            if ((containsKey(fileKeys(), key) || containsKey(directoryKeys(), key)) &&
                value.IsScalar()) {
                rewriteScalar(value, map);
            } else if (containsKey(sequenceKeys(), key) && value.IsSequence()) {
                for (std::size_t i = 0; i < value.size(); ++i) {
                    if (value[i].IsScalar()) rewriteScalar(value[i], map);
                }
            } else {
                rewriteNode(value, map);
            }
        }
    } else if (node.IsSequence()) {
        for (auto child : node) rewriteNode(child, map);
    }
}
} // namespace

const std::vector<std::string_view>& fileKeys() {
    static const std::vector<std::string_view> keys = {
        "ContentPath", "Texture", "FacePath", "MeshFile", "IconPath", "FragmentShaderFile",
        "Image", "ClearAmbientPath", "RainAmbientPath", "SnowAmbientPath"};
    return keys;
}

const std::vector<std::string_view>& sequenceKeys() {
    static const std::vector<std::string_view> keys = {"SkyboxPaths"};
    return keys;
}

const std::vector<std::string_view>& directoryKeys() {
    static const std::vector<std::string_view> keys = {"DataPath"};
    return keys;
}

std::string logicalPath(std::string_view source) {
    const fs::path path = AssetPath::fromStored(source);
    std::string stored = AssetPath::toStored(path);
    bool escapes = path.is_absolute() || path.has_root_name() || path.has_root_directory();
    for (const auto& component : path) {
        if (component == "..") escapes = true;
    }
    if (escapes || stored.empty()) {
        const std::string name = AssetPath::toStored(path.filename());
        return "external/" + (name.empty() ? std::string("file") : name);
    }
    constexpr std::string_view prefix = "assets/";
    if (stored.size() > prefix.size() && stored.compare(0, prefix.size(), prefix) == 0)
        stored.erase(0, prefix.size());
    while (stored.starts_with("./")) stored.erase(0, 2);
    return stored;
}

bool isExecutablePath(std::string_view path) {
    static const std::vector<std::string_view> extensions = {
        ".exe", ".dll", ".bat", ".cmd", ".com", ".ps1", ".psm1", ".sh", ".msi", ".scr",
        ".jar", ".vbs", ".vbe", ".wsf", ".lnk", ".cpl", ".pif", ".reg", ".so", ".dylib",
        ".appimage"};
    const std::string ext = lowerExtension(path);
    return std::find(extensions.begin(), extensions.end(), ext) != extensions.end();
}

void collect(const YAML::Node& node, std::vector<Dependency>& out) {
    collectNode(node, out);
}

std::vector<FileEntry> listFiles(const Dependency& dependency) {
    std::vector<FileEntry> files;
    const fs::path root = AssetPath::fromStored(dependency.source);
    std::error_code ec;
    if (!dependency.directory) {
        if (fs::is_regular_file(root, ec))
            files.push_back({AssetPath::toStored(root.filename()), root});
        return files;
    }
    if (!fs::is_directory(root, ec)) return files;
    for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const fs::path relative = fs::relative(it->path(), root, ec);
        if (ec || hasAutosaveComponent(relative)) continue;
        files.push_back({AssetPath::toStored(relative), it->path()});
    }
    std::sort(files.begin(), files.end(),
              [](const FileEntry& a, const FileEntry& b) { return a.relative < b.relative; });
    return files;
}

void rewrite(YAML::Node node, const std::unordered_map<std::string, std::string>& map) {
    rewriteNode(node, map);
}

} // namespace AssetDependencies

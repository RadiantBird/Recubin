#include <Core/AssetContainer.hpp>
#include <Util/AssetPath.hpp>
#include <Util/Logger.hpp>
#include <Util/Sha256.hpp>
#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace fs = std::filesystem;

namespace AssetContainer {
namespace {
constexpr std::string_view HEADER_PREFIX = "#rcaet ";
constexpr std::string_view HASH_PREFIX = "sha256:";

Result fail(const std::string& message) {
    Result result;
    result.message = message;
    return result;
}

Result ok() {
    Result result;
    result.success = true;
    return result;
}

YAML::Node makeFileNode(const EmbeddedFile& file, bool includeSource) {
    YAML::Node node(YAML::NodeType::Map);
    if (includeSource) node["Source"] = file.source;
    node["File"] = file.file;
    node["Size"] = static_cast<std::uint64_t>(file.size);
    node["Hash"] = file.hash;
    node["Offset"] = static_cast<std::uint64_t>(file.offset);
    return node;
}

// 同じ source のディレクトリ依存を1エントリにまとめて Embedded 配列を作る。
YAML::Node buildEmbeddedNode(const std::vector<EmbeddedFile>& files) {
    YAML::Node list(YAML::NodeType::Sequence);
    std::vector<std::string> directorySources;
    for (const auto& file : files) {
        if (!file.directory) {
            list.push_back(makeFileNode(file, true));
            continue;
        }
        if (std::find(directorySources.begin(), directorySources.end(), file.source) !=
            directorySources.end()) {
            continue;
        }
        directorySources.push_back(file.source);
        YAML::Node dirNode(YAML::NodeType::Map);
        dirNode["Source"] = file.source;
        dirNode["Dir"] = true;
        YAML::Node entries(YAML::NodeType::Sequence);
        for (const auto& member : files) {
            if (member.directory && member.source == file.source)
                entries.push_back(makeFileNode(member, false));
        }
        dirNode["Files"] = entries;
        list.push_back(dirNode);
    }
    return list;
}

Result parseFileEntry(const YAML::Node& node, EmbeddedFile& file) {
    if (!node.IsMap() || !node["File"] || !node["Size"] || !node["Hash"] || !node["Offset"])
        return fail("Embedded entry is missing File/Size/Hash/Offset");
    file.file = node["File"].as<std::string>("");
    file.size = node["Size"].as<std::uint64_t>(0);
    file.hash = node["Hash"].as<std::string>("");
    file.offset = node["Offset"].as<std::uint64_t>(0);
    if (!isSafeRelativePath(file.file))
        return fail("Embedded file has an unsafe path: " + file.file);
    return ok();
}

Result parseEmbedded(const YAML::Node& list, std::vector<EmbeddedFile>& files) {
    if (!list) return ok();
    if (!list.IsSequence()) return fail("Embedded must be a sequence");
    for (const auto& entry : list) {
        if (!entry.IsMap() || !entry["Source"])
            return fail("Embedded entry is missing Source");
        const std::string source = entry["Source"].as<std::string>("");
        if (entry["Dir"] && entry["Dir"].as<bool>(false)) {
            const YAML::Node members = entry["Files"];
            if (!members || !members.IsSequence())
                return fail("Embedded directory '" + source + "' has no Files");
            for (const auto& member : members) {
                EmbeddedFile file;
                file.source = source;
                file.directory = true;
                if (Result r = parseFileEntry(member, file); !r) return r;
                files.push_back(std::move(file));
            }
        } else {
            EmbeddedFile file;
            file.source = source;
            if (Result r = parseFileEntry(entry, file); !r) return r;
            files.push_back(std::move(file));
        }
    }
    return ok();
}

Result verifyBlobs(std::vector<EmbeddedFile>& files, const std::vector<char>& tail) {
    for (auto& file : files) {
        if (file.offset > tail.size() || file.size > tail.size() - file.offset)
            return fail("Embedded file is out of range: " + file.file);
        const char* begin = tail.data() + file.offset;
        const std::string actual = std::string(HASH_PREFIX) + Sha256::hex(begin, file.size);
        if (actual != file.hash)
            return fail("Embedded file is corrupted (hash mismatch): " + file.file);
        file.data.assign(begin, begin + file.size);
    }
    return ok();
}
} // namespace

bool isSafeRelativePath(std::string_view path) {
    if (path.empty()) return false;
    if (path.front() == '/' || path.front() == '\\') return false;
    if (path.find(':') != std::string_view::npos) return false;
    std::size_t start = 0;
    while (start <= path.size()) {
        std::size_t end = path.find_first_of("/\\", start);
        if (end == std::string_view::npos) end = path.size();
        const std::string_view segment = path.substr(start, end - start);
        if (segment.empty() || segment == "." || segment == "..") return false;
        start = end + 1;
    }
    return true;
}

Result write(const std::string& path, const YAML::Node& document,
             std::vector<EmbeddedFile>& files) {
    std::uint64_t offset = 0;
    for (auto& file : files) {
        if (!isSafeRelativePath(file.file)) return fail("Unsafe embedded path: " + file.file);
        file.size = file.data.size();
        file.hash = std::string(HASH_PREFIX) + Sha256::hex(file.data.data(), file.data.size());
        file.offset = offset;
        offset += file.size;
    }

    YAML::Node body = YAML::Clone(document);
    if (!files.empty()) body["Embedded"] = buildEmbeddedNode(files);
    std::string yamlText;
    try {
        yamlText = YAML::Dump(body);
    } catch (const std::exception& e) {
        return fail(std::string("YAML emit failed: ") + e.what());
    }
    yamlText.push_back('\n');

    const std::string header = "#rcaet " + std::to_string(FORMAT_VERSION) +
                               " yaml=" + std::to_string(yamlText.size()) + "\n";
    const fs::path target = AssetPath::fromStored(path);
    fs::path temp = target;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return fail("Failed to open for write: " + path);
        out.write(header.data(), static_cast<std::streamsize>(header.size()));
        out.write(yamlText.data(), static_cast<std::streamsize>(yamlText.size()));
        for (const auto& file : files)
            out.write(file.data.data(), static_cast<std::streamsize>(file.data.size()));
        out.flush();
        if (!out) {
            RCBN_ERROR("Failed to write asset: " << path);
            return fail("Failed to write: " + path);
        }
    }
    std::error_code ec;
    fs::rename(temp, target, ec);
    if (ec) {
        // Windowsでは既存ファイルへのrenameが失敗するため、置換にフォールバックする。
        fs::remove(target, ec);
        ec.clear();
        fs::rename(temp, target, ec);
        if (ec) {
            fs::remove(temp, ec);
            return fail("Failed to replace file: " + path);
        }
    }
    return ok();
}

Result read(const std::string& path, Document& out) {
    std::ifstream in(AssetPath::fromStored(path), std::ios::binary);
    if (!in) return fail("Failed to open: " + path);
    const std::vector<char> bytes((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());

    const auto newline = std::find(bytes.begin(), bytes.end(), '\n');
    if (newline == bytes.end()) return fail("Not a .rcaet file (no header line)");
    const std::string header(bytes.begin(), newline);
    if (header.compare(0, HEADER_PREFIX.size(), HEADER_PREFIX) != 0)
        return fail("Not a .rcaet file (bad header)");

    // "#rcaet <version> yaml=<N>"
    const std::size_t versionBegin = HEADER_PREFIX.size();
    const std::size_t space = header.find(' ', versionBegin);
    constexpr std::string_view YAML_KEY = "yaml=";
    if (space == std::string::npos || header.compare(space + 1, YAML_KEY.size(), YAML_KEY) != 0)
        return fail("Malformed .rcaet header");
    int version = 0;
    const auto versionEnd = std::from_chars(header.data() + versionBegin, header.data() + space, version);
    if (versionEnd.ec != std::errc() || versionEnd.ptr != header.data() + space)
        return fail("Malformed .rcaet header version");
    if (version != FORMAT_VERSION)
        return fail("Unsupported .rcaet version: " + std::to_string(version));
    std::uint64_t yamlBytes = 0;
    const char* lengthBegin = header.data() + space + 1 + YAML_KEY.size();
    const char* lengthEnd = header.data() + header.size();
    const auto lengthResult = std::from_chars(lengthBegin, lengthEnd, yamlBytes);
    if (lengthResult.ec != std::errc() || lengthResult.ptr != lengthEnd)
        return fail("Malformed .rcaet header length");

    const std::uint64_t yamlStart = static_cast<std::uint64_t>(header.size()) + 1;
    if (yamlBytes > bytes.size() - yamlStart) return fail("YAML section exceeds file size");

    YAML::Node root;
    try {
        root = YAML::Load(std::string(bytes.data() + yamlStart, yamlBytes));
    } catch (const std::exception& e) {
        return fail(std::string("YAML parse error: ") + e.what());
    }
    if (!root.IsMap() || !root["recubin"] || !root["recubin"].IsMap())
        return fail("Missing recubin header");
    const std::string type = root["recubin"]["type"].as<std::string>("");
    if (type != DOCUMENT_TYPE) return fail("Unsupported Recubin document type: " + type);
    const int documentVersion = root["recubin"]["version"].as<int>(-1);
    if (documentVersion != FORMAT_VERSION)
        return fail("Unsupported asset version: " + std::to_string(documentVersion));

    Document document;
    if (Result r = parseEmbedded(root["Embedded"], document.files); !r) return r;
    const std::size_t tailStart = static_cast<std::size_t>(yamlStart + yamlBytes);
    const std::vector<char> tail(bytes.begin() + tailStart, bytes.end());
    if (Result r = verifyBlobs(document.files, tail); !r) return r;

    root.remove("Embedded");
    // YAML::Nodeの代入(operator=)は、共有している実体の中身を書き換える。out が別の保持者と
    // ノードを共有していても壊さないよう、代入ではなく reset で結び直す。
    out.files = std::move(document.files);
    out.yaml.reset(root);
    return ok();
}

} // namespace AssetContainer

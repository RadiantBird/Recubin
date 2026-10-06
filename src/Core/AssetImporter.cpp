#include <Core/AssetImporter.hpp>
#include <Core/AssetDependencies.hpp>
#include <Core/SceneLoader.hpp>
#include <Instances/BaseCube.hpp>
#include <Instances/Instance.hpp>
#include <Instances/MaterialInstance.hpp>
#include <Util/AssetPath.hpp>
#include <Util/Logger.hpp>
#include <Util/Sha256.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <unordered_map>

namespace fs = std::filesystem;

namespace AssetImporter {
namespace {
constexpr const char* DEFAULT_DIRECTORY_NAME = "Asset";
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

// ファイルシステムで使えない文字・先頭末尾のドット/空白を除いた展開先ディレクトリ名。
std::string sanitizeDirectoryName(const std::string& name) {
    std::string result;
    for (unsigned char c : name) {
        const bool forbidden = c < 0x20 || std::string_view("<>:\"/\\|?*").find(static_cast<char>(c)) !=
                                              std::string_view::npos;
        result.push_back(forbidden ? '_' : static_cast<char>(c));
    }
    while (!result.empty() && (result.back() == '.' || result.back() == ' ')) result.pop_back();
    while (!result.empty() && (result.front() == '.' || result.front() == ' ')) result.erase(0, 1);
    return result.empty() ? std::string(DEFAULT_DIRECTORY_NAME) : result;
}

bool readFileBytes(const fs::path& path, std::vector<char>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

// 既存ファイルがあり、内容がembeddedと同じか。
bool hasSameContent(const fs::path& path, const AssetContainer::EmbeddedFile& file) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec) || fs::file_size(path, ec) != file.size) return false;
    std::vector<char> existing;
    if (!readFileBytes(path, existing)) return false;
    return std::string(HASH_PREFIX) + Sha256::hex(existing.data(), existing.size()) == file.hash;
}

bool conflictsWith(const fs::path& path, const AssetContainer::EmbeddedFile& file) {
    std::error_code ec;
    return fs::exists(path, ec) && !hasSameContent(path, file);
}

fs::path withNumberSuffix(const fs::path& path, int index) {
    if (index == 0) return path;
    return path.parent_path() / (path.stem().generic_string() + "_" + std::to_string(index) +
                                 path.extension().generic_string());
}

fs::path directoryWithSuffix(const fs::path& path, int index) {
    if (index == 0) return path;
    return path.parent_path() / (path.filename().generic_string() + "_" + std::to_string(index));
}

Result writeNewFile(const fs::path& path, const std::vector<char>& data) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) return fail("Failed to create directory: " + AssetPath::toStored(path.parent_path()));
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return fail("Failed to open for write: " + AssetPath::toStored(path));
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    out.flush();
    if (!out) return fail("Failed to write: " + AssetPath::toStored(path));
    return ok();
}

// 埋め込みファイルを展開し、source -> 展開先 の書換表を作る。
// 既存ファイルは上書きしない（同内容なら再利用、違えば連番の別名）。
struct Extraction {
    std::unordered_map<std::string, std::string> rewrites;
    std::vector<std::string> warnings;
};

Result extractSingleFile(const AssetContainer::EmbeddedFile& file, const Options& options,
                         const fs::path& baseDir, Extraction& extraction) {
    if (!options.allowExecutables && AssetDependencies::isExecutablePath(file.file)) {
        extraction.rewrites[file.source] = "";
        extraction.warnings.push_back("Executable excluded: " + file.file);
        return ok();
    }
    const fs::path wanted = AssetPath::fromStored(file.file);
    fs::path chosen;
    for (int i = 0;; ++i) {
        const fs::path candidate = baseDir / withNumberSuffix(wanted, i);
        if (!conflictsWith(candidate, file)) { chosen = candidate; break; }
    }
    if (!hasSameContent(chosen, file)) {
        if (Result r = writeNewFile(chosen, file.data); !r) return r;
    }
    extraction.rewrites[file.source] = AssetPath::toStored(chosen);
    if (AssetDependencies::isExecutablePath(file.file))
        extraction.warnings.push_back("Executable extracted: " + AssetPath::toStored(chosen));
    return ok();
}

Result extractDirectory(const std::vector<const AssetContainer::EmbeddedFile*>& members,
                        const Options& options, const fs::path& baseDir,
                        Extraction& extraction) {
    const std::string source = members.front()->source;
    const fs::path wanted = baseDir / AssetPath::fromStored(AssetDependencies::logicalPath(source));

    const auto extractable = [&](const AssetContainer::EmbeddedFile* member) {
        return options.allowExecutables || !AssetDependencies::isExecutablePath(member->file);
    };
    // ディレクトリ全体で衝突の無い名前（dir, dir_1, ...）を選ぶ。
    fs::path chosen;
    for (int i = 0;; ++i) {
        const fs::path candidate = directoryWithSuffix(wanted, i);
        const bool clean = std::none_of(members.begin(), members.end(), [&](const auto* member) {
            return extractable(member) &&
                   conflictsWith(candidate / AssetPath::fromStored(member->file), *member);
        });
        if (clean) { chosen = candidate; break; }
    }
    for (const auto* member : members) {
        if (!extractable(member)) {
            extraction.warnings.push_back("Executable excluded: " + source + "/" + member->file);
            continue;
        }
        const fs::path target = chosen / AssetPath::fromStored(member->file);
        if (!hasSameContent(target, *member)) {
            if (Result r = writeNewFile(target, member->data); !r) return r;
        }
        if (AssetDependencies::isExecutablePath(member->file))
            extraction.warnings.push_back("Executable extracted: " + AssetPath::toStored(target));
    }
    extraction.rewrites[source] = AssetPath::toStored(chosen);
    return ok();
}

Result extractAll(const Loaded& loaded, const Options& options, Extraction& extraction) {
    const fs::path baseDir =
        AssetPath::fromStored(options.extractRoot) / sanitizeDirectoryName(loaded.name);
    std::vector<std::string> handledDirectories;
    for (const auto& file : loaded.document.files) {
        if (!file.directory) {
            if (Result r = extractSingleFile(file, options, baseDir, extraction); !r) return r;
            continue;
        }
        if (std::find(handledDirectories.begin(), handledDirectories.end(), file.source) !=
            handledDirectories.end())
            continue;
        handledDirectories.push_back(file.source);
        std::vector<const AssetContainer::EmbeddedFile*> members;
        for (const auto& other : loaded.document.files) {
            if (other.directory && other.source == file.source) members.push_back(&other);
        }
        if (Result r = extractDirectory(members, options, baseDir, extraction); !r) return r;
    }
    return ok();
}

// TextFileのStorageIdは複製ごとに新規採番する（ユーザー領域のコピーを共有しない）。
void dropStorageIds(YAML::Node node) {
    if (!node.IsMap()) return;
    if (node["ClassName"] && node["ClassName"].as<std::string>("") == "TextFile") {
        YAML::Node props = node["Properties"];
        if (props && props.IsMap()) props.remove("StorageId");
    }
    YAML::Node children = node["Children"];
    if (children && children.IsSequence()) {
        for (auto child : children) dropStorageIds(child);
    }
}

void forEachNode(Instance& node, const std::function<void(Instance&)>& visit) {
    visit(node);
    for (const auto& [name, child] : node.children) {
        if (child) forEachNode(*child, visit);
    }
}

// 遅延解決の参照（Model.PrimaryCube等）を、改名・親付け替えの前に解決させる。
void resolveLazyReferences(Instance& bag) {
    forEachNode(bag, [](Instance& node) {
        std::vector<Instance::InstanceReference> references;
        node.collectInstanceReferences(references);
    });
}

Result parseInto(const YAML::Node& asset, Instance& bag, Instance& materialBag,
                 std::vector<std::string>& warnings) {
    const YAML::Node materials = asset["Materials"];
    if (materials && materials.IsSequence()) {
        for (const auto& node : materials) {
            auto material = SceneLoader::parseNode(node);
            if (!material || !material->IsA("Material")) {
                warnings.push_back("Skipped a bundled node that is not a Material");
                continue;
            }
            SceneLoader::attachParsedChild(materialBag, material);
        }
    }
    for (const auto& node : asset["Roots"]) {
        auto root = SceneLoader::parseNode(node);
        if (!root) {
            warnings.push_back("Skipped an asset node that could not be created");
            continue;
        }
        SceneLoader::attachParsedChild(bag, root);
    }
    if (bag.children.size() <= 1) return fail("Asset has no importable roots");
    return ok();
}

// 取り込み先に同名Materialがあれば既存を使い、無ければ追加対象にする。
void matchMaterials(Instance& materialBag, Instance* materialService, Instance& bag,
                    Imported& out) {
    std::unordered_map<const Instance*, std::shared_ptr<MaterialInstance>> replaced;
    std::vector<std::shared_ptr<Instance>> bundled;
    for (const auto& [name, material] : materialBag.children) bundled.push_back(material);

    for (const auto& material : bundled) {
        if (!materialService) {
            out.warnings.push_back("No MaterialService; bundled Material skipped: " +
                                   material->Name);
            continue;
        }
        Instance* existing = materialService->getChild(material->Name);
        if (existing && existing->IsA("Material")) {
            replaced[material.get()] =
                std::static_pointer_cast<MaterialInstance>(existing->shared_from_this());
        } else {
            out.materialsToAdd.push_back(material);
        }
    }
    for (const auto& material : out.materialsToAdd) materialBag.removeChild(material->Name);

    if (replaced.empty()) return;
    forEachNode(bag, [&](Instance& node) {
        if (!node.IsA("BaseCube")) return;
        auto& cube = static_cast<BaseCube&>(node);
        const auto current = cube.getMaterialInstance();
        if (!current) return;
        const auto found = replaced.find(current.get());
        if (found != replaced.end()) cube.setMaterialInstance(found->second);
    });
}
} // namespace

Result load(const std::string& path, Loaded& out) {
    AssetContainer::Document document;
    const AssetContainer::Result read = AssetContainer::read(path, document);
    if (!read) return fail(read.message);

    const YAML::Node asset = document.yaml["Asset"];
    if (!asset || !asset.IsMap()) return fail("Missing Asset section");
    const YAML::Node roots = asset["Roots"];
    if (!roots || !roots.IsSequence() || roots.size() == 0) return fail("Asset has no Roots");

    Loaded loaded;
    loaded.name = asset["Name"].as<std::string>("");
    if (loaded.name.empty()) loaded.name = DEFAULT_DIRECTORY_NAME;
    for (const auto& file : document.files) {
        if (!AssetDependencies::isExecutablePath(file.file)) continue;
        loaded.executables.push_back({file.file, file.size, file.hash});
    }
    // YAML::Nodeの代入は共有実体の中身を書き換えるため、out が他と共有していても壊れないよう
    // メンバごとに置き換え、ノードは reset で結び直す。
    out.name = std::move(loaded.name);
    out.executables = std::move(loaded.executables);
    out.document.files = std::move(document.files);
    out.document.yaml.reset(document.yaml);
    return ok();
}

Result instantiate(const Loaded& loaded, const Options& options, Instance* materialService,
                   Imported& out) {
    Extraction extraction;
    if (Result r = extractAll(loaded, options, extraction); !r) {
        RCBN_ERROR("Asset import: " << r.message);
        return r;
    }

    YAML::Node asset = YAML::Clone(loaded.document.yaml["Asset"]);
    for (auto root : asset["Roots"]) dropStorageIds(root);
    if (asset["Materials"]) {
        for (auto material : asset["Materials"]) dropStorageIds(material);
    }
    AssetDependencies::rewrite(asset, extraction.rewrites);

    // 切り離し状態のまま参照を解決するための一時コンテナ。Materialは本来と同じ
    // "MaterialService\<名前>" のパスで解決できるよう、同名の子コンテナに入れる。
    auto bag = std::make_shared<Instance>("__asset__");
    auto materialBag = std::make_shared<Instance>("MaterialService");
    bag->addChild(materialBag);

    Imported imported;
    imported.warnings = std::move(extraction.warnings);
    if (Result r = parseInto(asset, *bag, *materialBag, imported.warnings); !r) return r;
    SceneLoader::resolveConstraintRefs(bag.get());
    resolveLazyReferences(*bag);
    matchMaterials(*materialBag, materialService, *bag, imported);

    std::vector<std::shared_ptr<Instance>> roots;
    for (const auto& [name, child] : bag->children) {
        if (child != materialBag) roots.push_back(child);
    }
    for (const auto& root : roots) bag->removeChild(root->Name);
    imported.roots = std::move(roots);
    out = std::move(imported);
    return ok();
}

} // namespace AssetImporter

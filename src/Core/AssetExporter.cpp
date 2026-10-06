#include <Core/AssetExporter.hpp>
#include <Core/FileLoader.hpp>
#include <Core/SceneLoader.hpp>
#include <Instances/BaseCube.hpp>
#include <Instances/Instance.hpp>
#include <Instances/MaterialInstance.hpp>
#include <Util/AssetPath.hpp>
#include <Util/Logger.hpp>
#include <algorithm>
#include <functional>
#include <unordered_set>

namespace fs = std::filesystem;

namespace AssetExporter {
namespace {
constexpr const char* DEFAULT_ASSET_NAME = "Asset";

// 書き出し対象にできないサービス/シングルトン系クラス。
const std::vector<std::string_view>& deniedClasses() {
    static const std::vector<std::string_view> classes = {
        "System", "Workspace", "MaterialService", "PathfindingService", "ChatService",
        "Users", "User", "Lighting", "UserInput", "Event"};
    return classes;
}

bool isRuntimeCharacterName(const std::string& name) {
    constexpr std::string_view prefix = "PlayerCharacter_";
    if (name == "PlayerCharacter") return true;
    return name.size() > prefix.size() && name.starts_with(prefix) &&
           std::all_of(name.begin() + static_cast<std::ptrdiff_t>(prefix.size()), name.end(),
                       [](unsigned char c) { return c >= '0' && c <= '9'; });
}

bool isAncestor(const Instance* ancestor, const Instance* node) {
    for (auto parent = node->Parent.lock(); parent; parent = parent->Parent.lock()) {
        if (parent.get() == ancestor) return true;
    }
    return false;
}

void forEachNode(Instance& node, const std::function<void(Instance&)>& visit) {
    visit(node);
    for (const auto& [name, child] : node.children) {
        if (child) forEachNode(*child, visit);
    }
}

// 複製ツリーから書き出せない子孫（サービス系・ランタイムキャラ）を取り除く。
void removeUnexportableDescendants(Instance& node, std::vector<std::string>& warnings) {
    std::vector<std::string> removed;
    for (const auto& [name, child] : node.children) {
        if (child && !isExportable(*child)) removed.push_back(name);
    }
    for (const auto& name : removed) {
        warnings.push_back("Skipped '" + name + "' under '" + node.Name +
                           "' (class cannot be exported)");
        node.removeChild(name);
    }
    for (const auto& [name, child] : node.children) {
        if (child) removeUnexportableDescendants(*child, warnings);
    }
}

// clone()を上書きしていないクラスは基底のInstanceへ化けるため、複製後にクラスが変わった
// ノードを警告する（書き出し結果が黙って別クラスになるのを防ぐ）。
void warnClassChanges(Instance& original, Instance& clone, std::vector<std::string>& warnings) {
    if (original.getClassName() != clone.getClassName()) {
        warnings.push_back("Class changed while copying: " + original.getFullPath() + " (" +
                           original.getClassName() + " -> " + clone.getClassName() + ")");
    }
    for (const auto& [name, child] : original.children) {
        const auto found = clone.children.find(name);
        if (child && found != clone.children.end() && found->second)
            warnClassChanges(*child, *found->second, warnings);
    }
}

// 複製森の外を指す参照を切り、警告に積む。
void clearExternalReferences(const std::vector<std::shared_ptr<Instance>>& clones,
                             std::vector<std::string>& warnings) {
    std::unordered_set<const Instance*> inside;
    for (const auto& clone : clones)
        forEachNode(*clone, [&](Instance& node) { inside.insert(&node); });
    for (const auto& clone : clones) {
        forEachNode(*clone, [&](Instance& node) {
            std::vector<Instance::InstanceReference> references;
            node.collectInstanceReferences(references);
            for (auto& reference : references) {
                if (!reference.target || inside.count(reference.target.get())) continue;
                warnings.push_back("External reference cleared: " + node.getFullPath() + " " +
                                   reference.ownerLabel);
                if (reference.set) reference.set(nullptr);
            }
        });
    }
}

// 部分木のBaseCubeが参照する解決済みMaterialを、重複なく出現順で集める。
std::vector<std::shared_ptr<MaterialInstance>> collectMaterials(
    const std::vector<std::shared_ptr<Instance>>& clones) {
    std::vector<std::shared_ptr<MaterialInstance>> materials;
    for (const auto& clone : clones) {
        forEachNode(*clone, [&](Instance& node) {
            if (!node.IsA("BaseCube")) return;
            auto material = static_cast<BaseCube&>(node).getMaterialInstance();
            if (!material) return;
            if (std::find(materials.begin(), materials.end(), material) == materials.end())
                materials.push_back(std::move(material));
        });
    }
    return materials;
}

YAML::Node emitNodes(const std::vector<std::shared_ptr<Instance>>& nodes) {
    YAML::Emitter out;
    out << YAML::BeginSeq;
    for (const auto& node : nodes) SceneLoader::emitNode(out, node.get());
    out << YAML::EndSeq;
    return YAML::Load(out.c_str());
}

std::string withSuffix(const std::string& logical, int index) {
    const fs::path path = AssetPath::fromStored(logical);
    fs::path renamed = path.parent_path() /
        (path.stem().generic_string() + "_" + std::to_string(index) +
         path.extension().generic_string());
    return AssetPath::toStored(renamed);
}

std::string uniqueLogicalPath(const std::string& logical,
                              const std::unordered_set<std::string>& taken) {
    if (!taken.count(logical)) return logical;
    for (int i = 1;; ++i) {
        std::string candidate = withSuffix(logical, i);
        if (!taken.count(candidate)) return candidate;
    }
}

bool readFile(const fs::path& path, std::vector<char>& out) {
    out = FileLoader::readBinary(AssetPath::toStored(path));
    std::error_code ec;
    const auto expected = fs::file_size(path, ec);
    // 空ファイルは readBinary が空を返すため、サイズで区別する。
    return !ec && out.size() == expected;
}
} // namespace

bool isExportableClass(std::string_view className) {
    return std::find(deniedClasses().begin(), deniedClasses().end(), className) ==
           deniedClasses().end();
}

bool isExportable(Instance& instance) {
    return isExportableClass(instance.getClassName()) && !isRuntimeCharacterName(instance.Name);
}

Plan prepare(const std::vector<Instance*>& roots) {
    Plan plan;
    std::vector<std::shared_ptr<Instance>> targets;
    for (Instance* root : roots) {
        if (!root) continue;
        if (!isExportable(*root)) {
            plan.warnings.push_back("Skipped '" + root->Name + "' (class cannot be exported)");
            continue;
        }
        // 別の選択ルートの子孫は、その祖先に含まれるため単独では書き出さない。
        const bool covered = std::any_of(roots.begin(), roots.end(), [&](Instance* other) {
            return other && other != root && isExportable(*other) && isAncestor(other, root);
        });
        if (!covered) targets.push_back(root->shared_from_this());
    }
    if (targets.empty()) return plan;

    auto clones = Instance::cloneForest(targets);
    for (std::size_t i = 0; i < targets.size() && i < clones.size(); ++i)
        warnClassChanges(*targets[i], *clones[i], plan.warnings);
    for (const auto& clone : clones) removeUnexportableDescendants(*clone, plan.warnings);
    clearExternalReferences(clones, plan.warnings);
    const auto materials = collectMaterials(clones);

    plan.rootCount = static_cast<int>(clones.size());
    plan.defaultName = clones.size() == 1 ? clones.front()->Name : DEFAULT_ASSET_NAME;

    YAML::Node document;
    document["recubin"]["type"] = AssetContainer::DOCUMENT_TYPE;
    document["recubin"]["version"] = AssetContainer::FORMAT_VERSION;
    document["Asset"]["Name"] = plan.defaultName;
    document["Asset"]["Roots"] = emitNodes(clones);
    if (!materials.empty()) {
        std::vector<std::shared_ptr<Instance>> materialNodes(materials.begin(), materials.end());
        document["Asset"]["Materials"] = emitNodes(materialNodes);
        for (const auto& material : materials) plan.materialNames.push_back(material->Name);
    }
    plan.document = document;

    AssetDependencies::collect(document["Asset"], plan.dependencies);
    for (const auto& dependency : plan.dependencies) {
        if (!dependency.exists)
            plan.warnings.push_back("File not found, cannot be embedded: " + dependency.source);
        else if (dependency.directory)
            plan.warnings.push_back("Directory '" + dependency.source +
                                    "' is shared with the source scene unless embedded");
    }
    return plan;
}

AssetContainer::Result write(const Plan& plan, const Options& options, const std::string& path) {
    AssetContainer::Result failure;
    if (!plan) {
        failure.message = "Nothing to export";
        return failure;
    }

    std::vector<AssetContainer::EmbeddedFile> files;
    std::unordered_set<std::string> takenLogical;
    for (const auto& dependency : plan.dependencies) {
        if (std::find(options.embedSources.begin(), options.embedSources.end(),
                      dependency.source) == options.embedSources.end())
            continue;
        if (!dependency.exists || dependency.autosave) continue;
        const std::string directoryLogical = AssetDependencies::logicalPath(dependency.source);
        for (const auto& entry : AssetDependencies::listFiles(dependency)) {
            AssetContainer::EmbeddedFile file;
            file.source = dependency.source;
            file.directory = dependency.directory;
            if (dependency.directory) {
                file.file = entry.relative;
            } else {
                file.file = uniqueLogicalPath(directoryLogical, takenLogical);
                takenLogical.insert(file.file);
            }
            if (!readFile(entry.path, file.data)) {
                failure.message = "Failed to read dependency: " + AssetPath::toStored(entry.path);
                RCBN_ERROR("Asset export: " << failure.message);
                return failure;
            }
            files.push_back(std::move(file));
        }
    }

    YAML::Node document = YAML::Clone(plan.document);
    if (!options.name.empty()) document["Asset"]["Name"] = options.name;
    return AssetContainer::write(path, document, files);
}

} // namespace AssetExporter

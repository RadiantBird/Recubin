#include <Core/HighlightTargets.hpp>

#include <Instances/BaseCube.hpp>
#include <Instances/Instance.hpp>

namespace {
void collectBaseCubesRecursive(Instance* instance, std::vector<BaseCube*>& out) {
    if (!instance) return;
    if (instance->IsA("BaseCube")) {
        out.push_back(static_cast<BaseCube*>(instance));
    }
    for (const auto& [name, child] : instance->getChildren()) {
        (void)name;
        collectBaseCubesRecursive(child.get(), out);
    }
}
}

void collectHighlightTargets(Instance* parent, std::vector<BaseCube*>& out) {
    if (!parent) return;
    if (parent->IsA("Model")) {
        for (const auto& [name, child] : parent->getChildren()) {
            (void)name;
            collectBaseCubesRecursive(child.get(), out);
        }
        return;
    }
    if (parent->IsA("BaseCube")) {
        out.push_back(static_cast<BaseCube*>(parent));
    }
}

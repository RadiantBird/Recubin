#pragma once

// Shared searchable class catalog for Explorer insertion, grouping and replacement.

#include <Core/SceneLoader.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// Shared class list used by insertion, grouping, and replacement pickers.
// Keeping this data independent from ImGui makes filtering testable and avoids
// the three menus slowly acquiring different class inventories.
enum class InstanceCategory { Cubes, Effects, Environment, Gui, Physics, Values,
                               Container, File, Script, Other };

struct InstanceCatalogEntry {
    std::string_view className;
    InstanceCategory category;
};

namespace InstanceCatalog {
const std::vector<InstanceCatalogEntry>& entries();
std::vector<InstanceCatalogEntry> search(std::string_view query);
std::shared_ptr<Instance> create(std::string_view className);
const char* categoryLabel(InstanceCategory category);
}

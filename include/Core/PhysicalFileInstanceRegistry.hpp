#pragma once

#include <Instances/PhysicalFileInstance.hpp>
#include <Core/PropertyRegistry.hpp>

#include <functional>
#include <memory>
#include <string_view>
#include <vector>

enum class PhysicalFileKind {
    Generic,
    Font,
};

enum class PhysicalFileInsertCategory {
    Other,
};

struct PhysicalFileInstanceType {
    std::string_view className;
    PhysicalFileKind kind = PhysicalFileKind::Generic;
    PhysicalFileInsertCategory insertCategory = PhysicalFileInsertCategory::Other;
    std::string_view dialogLabel;
    std::string_view dialogFilter;
    std::function<std::shared_ptr<PhysicalFileInstance>()> factory;
};

namespace PhysicalFileInstanceRegistry {

const PhysicalFileInstanceType* find(std::string_view className);
std::shared_ptr<PhysicalFileInstance> create(std::string_view className);
const std::vector<PhysicalFileInstanceType>& types();

// ContentPath以外の状態を持つ手書き派生型向けのescape hatch。
// className等のstring_viewはプログラム終了まで有効な文字列を指すこと。
bool registerType(PhysicalFileInstanceType type,
                  std::vector<PropertyDesc> additionalProperties = {});

} // namespace PhysicalFileInstanceRegistry

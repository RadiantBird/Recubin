#pragma once

#include <memory>
#include <string_view>

class Instance;

// BaseCube派生クラスの生成をSceneLoaderとLuau Instance.newで共有する。
std::shared_ptr<Instance> createBaseCubeInstance(std::string_view className);

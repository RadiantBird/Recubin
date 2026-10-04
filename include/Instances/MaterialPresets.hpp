#pragma once
#include <memory>

class MaterialInstance;

// 画像マップと値を割り当て済みのMaterialプリセット。
// 画像は assets/materials/ (tools/generate_material_textures.py で生成)。
// 作られるMaterialは子に FileRef(BaseColor/Roughness/...) を持ち、マップ参照はその子の名前。
namespace MaterialPresets {

enum class Preset { RoughPlastic, WoodPlanks, ScratchedMetal };
constexpr int PRESET_COUNT = 3;

// Materialの既定名(Explorerでは重複時に連番が付く)。
const char* presetName(Preset preset);

// 子のFileRefまで含めたMaterialを新規に作る。MaterialServiceへの追加は呼び出し側が行う。
std::shared_ptr<MaterialInstance> create(Preset preset);

}

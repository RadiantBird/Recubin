#include <include/Instances/MaterialPresets.hpp>
#include <include/Instances/MaterialInstance.hpp>
#include <include/Instances/FileRef.hpp>
#include <algorithm>
#include <cctype>
#include <string>

namespace MaterialPresets {

namespace {

constexpr const char* TEXTURE_DIRECTORY = "assets/materials/";

void addMap(MaterialInstance& material, const char* mapName, const char* fileStem,
            std::string MaterialInstance::* reference) {
    auto file = std::make_shared<FileRef>();
    file->Name = mapName;
    // 画像ファイル名はすべて小文字(大文字小文字を区別するFSでも読めるようにする)。
    std::string suffix = mapName;
    std::transform(suffix.begin(), suffix.end(), suffix.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    file->Path = std::string(TEXTURE_DIRECTORY) + fileStem + "_" + suffix + ".png";
    material.addChild(file);
    material.*reference = mapName;
}

} // namespace

const char* presetName(Preset preset) {
    switch (preset) {
        case Preset::RoughPlastic:   return "RoughPlastic";
        case Preset::WoodPlanks:     return "WoodPlanks";
        case Preset::ScratchedMetal: return "ScratchedMetal";
    }
    return "Material";
}

std::shared_ptr<MaterialInstance> create(Preset preset) {
    auto material = std::make_shared<MaterialInstance>();
    material->Name = presetName(preset);
    // Roughness/Metallicは画像の値に乗算されるので、画像が実際の値を決めるよう1にしておく。
    material->Roughness = 1.0f;

    switch (preset) {
        case Preset::RoughPlastic:
            material->Metallic = 0.0f;
            material->Reflectance = 0.5f;
            material->TextureScale = 2.0f;
            material->NormalStrength = 0.6f;
            addMap(*material, "BaseColor", "rough_plastic", &MaterialInstance::BaseColorMap);
            addMap(*material, "Roughness", "rough_plastic", &MaterialInstance::RoughnessMap);
            addMap(*material, "Normal",    "rough_plastic", &MaterialInstance::NormalMap);
            break;
        case Preset::WoodPlanks:
            material->Metallic = 0.0f;
            material->Reflectance = 0.3f;
            material->TextureScale = 5.0f;
            material->NormalStrength = 1.0f;
            material->DynamicFriction = 0.6f;
            material->Restitution = 0.05f;
            material->MassDensity = 0.7f;
            addMap(*material, "BaseColor", "wood_planks", &MaterialInstance::BaseColorMap);
            addMap(*material, "Roughness", "wood_planks", &MaterialInstance::RoughnessMap);
            addMap(*material, "Normal",    "wood_planks", &MaterialInstance::NormalMap);
            break;
        case Preset::ScratchedMetal:
            material->Metallic = 1.0f;
            material->Reflectance = 0.5f;
            material->TextureScale = 3.0f;
            material->NormalStrength = 1.0f;
            material->DynamicFriction = 0.4f;
            material->Restitution = 0.2f;
            material->MassDensity = 3.0f;
            material->Conductive = true;
            addMap(*material, "BaseColor", "scratched_metal", &MaterialInstance::BaseColorMap);
            addMap(*material, "Roughness", "scratched_metal", &MaterialInstance::RoughnessMap);
            addMap(*material, "Metallic",  "scratched_metal", &MaterialInstance::MetallicMap);
            addMap(*material, "Normal",    "scratched_metal", &MaterialInstance::NormalMap);
            break;
    }
    return material;
}

} // namespace MaterialPresets

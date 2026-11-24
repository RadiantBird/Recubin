#pragma once

#include <Core/GameGuiDrawing.hpp>
#include <SDL3/SDL_gpu.h>

#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Workspace;
class ScreenGuiObject;
struct ImFont;

// GPU-owned assets used by the game GUI draw list.  The owner is kept separate
// from SdlBasicRenderer because GUI textures can outlive a scene reload.
class SdlGameGuiResources final {
public:
    explicit SdlGameGuiResources(SDL_GPUDevice& device);
    ~SdlGameGuiResources();

    SdlGameGuiResources(const SdlGameGuiResources&) = delete;
    SdlGameGuiResources& operator=(const SdlGameGuiResources&) = delete;

    // The optional font is the Studio UI font already present in its atlas.
    // Engine callers omit it and get the same MPLUS/DotGothic selection.
    void initializeFonts(ImFont* suppliedMplus = nullptr);
    void prepareFonts(Workspace& workspace);
    const GameGuiDrawing::Resources& drawing() const { return m_drawing; }

private:
    struct TextureEntry {
        SDL_GPUTexture* texture = nullptr;
    };

    SDL_GPUDevice* m_device = nullptr;
    ImFont* m_defaultFont = nullptr;
    ImFont* m_mplusFont = nullptr;
    ImFont* m_dotGothicFont = nullptr;
    std::unordered_map<std::string, TextureEntry> m_textures;
    std::unordered_map<std::wstring, ImFont*> m_fonts;
    std::unordered_set<std::string> m_warnedTextures;
    std::unordered_set<std::wstring> m_warnedFonts;
    GameGuiDrawing::Resources m_drawing;

    ImFont* resolveFont(ScreenGuiObject* object) const;
    ImTextureRef resolveImage(ScreenGuiObject* object);
    bool loadTexture(const std::string& path, TextureEntry& entry);
};

#include <Core/SdlGameGuiResources.hpp>

#include <Core/RgbaImage.hpp>
#include <Core/SdlBasicRenderer.hpp>
#include <Instances/ScreenGuiObject.hpp>
#include <Instances/Workspace.hpp>
#include <Util/AssetPath.hpp>
#include <Util/AssetGuard.hpp>
#include <Util/Logger.hpp>
#include <include/imgui/imgui.h>

SdlGameGuiResources::SdlGameGuiResources(SDL_GPUDevice& device) : m_device(&device) {
    m_drawing.font = [this](ScreenGuiObject* object) { return resolveFont(object); };
    m_drawing.image = [this](ScreenGuiObject* object) { return resolveImage(object); };
    m_drawing.flipImageY = false;
}

SdlGameGuiResources::~SdlGameGuiResources() {
    if (!m_device) return;
    for (auto& [path, entry] : m_textures) {
        (void)path;
        if (entry.texture) SDL_ReleaseGPUTexture(m_device, entry.texture);
    }
}

void SdlGameGuiResources::initializeFonts(ImFont* suppliedMplus) {
    ImGuiIO& io = ImGui::GetIO();
    m_defaultFont = io.FontDefault;
    if (!m_defaultFont && io.Fonts->Fonts.Size > 0)
        m_defaultFont = io.Fonts->Fonts.front();
    if (!m_defaultFont)
        m_defaultFont = io.Fonts->AddFontDefault();
    m_mplusFont = suppliedMplus;
    if (!m_mplusFont && std::filesystem::exists("assets/fonts/MPLUS1p-Regular.ttf"))
        m_mplusFont = io.Fonts->AddFontFromFileTTF(
            "assets/fonts/MPLUS1p-Regular.ttf", 22.0f, nullptr,
            io.Fonts->GetGlyphRangesJapanese());
    if (std::filesystem::exists("assets/fonts/DotGothic16-Regular.ttf"))
        m_dotGothicFont = io.Fonts->AddFontFromFileTTF(
            "assets/fonts/DotGothic16-Regular.ttf", 22.0f, nullptr,
            io.Fonts->GetGlyphRangesJapanese());
    if (!m_mplusFont) m_mplusFont = m_dotGothicFont ? m_dotGothicFont : m_defaultFont;
}

void SdlGameGuiResources::prepareFonts(Workspace& workspace) {
    for (ScreenGuiObject* object : workspace.getRenderScreenGuiObjects()) {
        if (!object || !object->UseFontFile || object->FontFile.empty()) continue;
        const auto path = GameGuiDrawing::customFontPath(*object);
        if (path.empty()) {
            const std::wstring reference(object->FontFile.begin(), object->FontFile.end());
            if (m_warnedFonts.insert(reference).second)
                RCBN_WARN("SDL GUI font reference could not be resolved: " << object->FontFile);
            continue;
        }
        const auto key = path.lexically_normal().wstring();
        if (m_fonts.contains(key)) continue;
        ImFont* loaded = GameGuiDrawing::addCustomFont(path);
        if (!loaded && m_warnedFonts.insert(key).second)
            RCBN_WARN("SDL GUI font could not be loaded: " << path.string());
        m_fonts.emplace(key, loaded);
    }
}

ImFont* SdlGameGuiResources::resolveFont(ScreenGuiObject* object) const {
    if (!object) return m_mplusFont ? m_mplusFont : m_defaultFont;
    if (!object->UseFontFile) {
        if (object->Font == SystemFont::DotGothic16 && m_dotGothicFont) return m_dotGothicFont;
        return m_mplusFont ? m_mplusFont : m_defaultFont;
    }
    const auto path = GameGuiDrawing::customFontPath(*object);
    if (path.empty()) return m_mplusFont ? m_mplusFont : m_defaultFont;
    const auto key = path.lexically_normal().wstring();
    auto it = m_fonts.find(key);
    return it != m_fonts.end() && it->second ? it->second : (m_mplusFont ? m_mplusFont : m_defaultFont);
}

bool SdlGameGuiResources::loadTexture(const std::string& path, TextureEntry& entry) {
    const auto filePath = AssetPath::fromStored(path);
    RgbaImage image;
    std::string decodeError;
    if (!decodeRgbaImage(filePath, image, decodeError)) {
        if (m_warnedTextures.insert(path).second)
            RCBN_WARN("SDL GUI image could not be decoded: " << filePath.string()
                                                              << " error=" << decodeError);
        return false;
    }
    std::string error;
    entry.texture = uploadSdlRgbaTexture(*m_device, image.width, image.height, image.pixels, error);
    if (!entry.texture) {
        if (m_warnedTextures.insert(path).second)
            RCBN_WARN("SDL GUI image could not be uploaded: " << path << " error=" << error);
        return false;
    }
    return true;
}

ImTextureRef SdlGameGuiResources::resolveImage(ScreenGuiObject* object) {
    if (!object) return ImTextureRef{};
    const auto* content = object->imageContent();
    if (!content || content->path.empty()) return ImTextureRef{};
    const std::string key = AssetPath::normalize(content->path);
    auto it = m_textures.find(key);
    if (it == m_textures.end()) {
        if (!AssetGuard::allow(key)) return ImTextureRef{};
        TextureEntry entry;
        if (!loadTexture(key, entry)) return ImTextureRef{};
        it = m_textures.emplace(key, entry).first;
    }
    return ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(it->second.texture)));
}

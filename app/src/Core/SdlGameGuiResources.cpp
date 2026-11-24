#include <Core/SdlGameGuiResources.hpp>

#include <Core/SdlBasicRenderer.hpp>
#include <Instances/ScreenGuiObject.hpp>
#include <Instances/Workspace.hpp>
#include <Util/AssetPath.hpp>
#include <Util/AssetGuard.hpp>
#include <Core/SvgRaster.hpp>
#include <Util/Logger.hpp>
#include <include/stb_image.h>
#include <include/imgui/imgui.h>

#include <algorithm>
#include <fstream>
#include <limits>

namespace {

void flipRows(std::vector<std::uint8_t>& pixels, int width, int height) {
    const std::size_t rowSize = static_cast<std::size_t>(width) * 4;
    for (int y = 0; y < height / 2; ++y) {
        auto first = pixels.begin() + static_cast<std::size_t>(y) * rowSize;
        auto last = pixels.begin() + static_cast<std::size_t>(height - 1 - y) * rowSize;
        std::swap_ranges(first, first + rowSize, last);
    }
}

} // namespace

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
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;
    if (SvgRaster::isSvgPath(filePath)) {
        if (!SvgRaster::rasterizeFile(filePath, pixels, width, height)) {
            if (m_warnedTextures.insert(path).second)
                RCBN_WARN("SDL GUI SVG could not be rasterized: " << filePath.string());
            return false;
        }
        flipRows(pixels, width, height);
    } else {
        std::ifstream input(filePath, std::ios::binary | std::ios::ate);
        if (!input) {
            if (m_warnedTextures.insert(path).second)
                RCBN_WARN("SDL GUI image could not be opened: " << filePath.string());
            return false;
        }
        const std::streamsize size = input.tellg();
        if (size <= 0 || size > std::numeric_limits<int>::max()) {
            if (m_warnedTextures.insert(path).second)
                RCBN_WARN("SDL GUI image has an invalid byte count: " << path << " size=" << size);
            return false;
        }
        std::vector<std::uint8_t> encoded(static_cast<std::size_t>(size));
        input.seekg(0, std::ios::beg);
        if (!input.read(reinterpret_cast<char*>(encoded.data()), size)) {
            if (m_warnedTextures.insert(path).second)
                RCBN_WARN("SDL GUI image could not be read: " << path);
            return false;
        }
        int channels = 0;
        stbi_set_flip_vertically_on_load_thread(0);
        stbi_uc* source = stbi_load_from_memory(encoded.data(), static_cast<int>(encoded.size()),
                                                 &width, &height, &channels, 4);
        if (!source || width <= 0 || height <= 0) {
            if (source) stbi_image_free(source);
            if (m_warnedTextures.insert(path).second)
                RCBN_WARN("SDL GUI image could not be decoded: " << filePath.string());
            return false;
        }
        pixels.assign(source, source + static_cast<std::size_t>(width) * height * 4);
        stbi_image_free(source);
    }
    std::string error;
    entry.texture = uploadSdlRgbaTexture(*m_device, static_cast<unsigned>(width),
                                          static_cast<unsigned>(height), pixels, error);
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

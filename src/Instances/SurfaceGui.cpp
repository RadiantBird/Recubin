#include <Instances/SurfaceGui.hpp>
#include <Instances/ScreenGuiObject.hpp>
#include <include/Core/PropertyRegistry.hpp>
#include <GL/glew.h>

#include <bit>
#include <cstdint>
#include <string_view>

namespace {
constexpr std::uint64_t FNV_OFFSET_BASIS = 14695981039346656037ull;
constexpr std::uint64_t FNV_PRIME = 1099511628211ull;

void hashByte(std::uint64_t& hash, std::uint8_t value) {
    hash ^= value;
    hash *= FNV_PRIME;
}

void hashUint64(std::uint64_t& hash, std::uint64_t value) {
    for (unsigned int shift = 0; shift < 64; shift += 8) {
        hashByte(hash, static_cast<std::uint8_t>(value >> shift));
    }
}

void hashFloat(std::uint64_t& hash, float value) {
    hashUint64(hash, std::bit_cast<std::uint32_t>(value));
}

void hashString(std::uint64_t& hash, std::string_view value) {
    hashUint64(hash, value.size());
    for (const unsigned char character : value) {
        hashByte(hash, character);
    }
}

void hashVector2(std::uint64_t& hash, const Vector2& value) {
    hashFloat(hash, value.x);
    hashFloat(hash, value.y);
}

void hashColor(std::uint64_t& hash, const Color4& value) {
    hashFloat(hash, value.r);
    hashFloat(hash, value.g);
    hashFloat(hash, value.b);
    hashFloat(hash, value.a);
}
}

static const bool s_surfaceGuiRegistered = []{
    using namespace PropertyRegistry;
    registerClass("SurfaceGui", "WorldGuiObject", {
        enumProp<&SurfaceGui::face>("Face",
            {{"Front",0},{"Back",1},{"Top",2},{"Bottom",3},{"Right",4},{"Left",5}},
            /*yamlAsString*/true),
    });
    return true;
}();

SurfaceGui::SurfaceGui() : Named<SurfaceGui, WorldGuiObject>("SurfaceGui") {}

bool SurfaceGui::isRenderableDirectChild(Instance* child) {
    if (!child || !child->IsA("ScreenGuiObject")) return false;
    return static_cast<ScreenGuiObject*>(child)->hasRenderableOwnContent();
}

bool SurfaceGui::hasRenderableDirectChild() {
    for (auto const& [name, child] : getChildren()) {
        (void)name;
        if (isRenderableDirectChild(child.get())) return true;
    }
    return false;
}

bool SurfaceGui::contributesBakedVisualOverride() {
    if (!Visible) return false;
    if (m_texID == 0) return false;
    return hasRenderableOwnContent() || hasRenderableDirectChild();
}

std::uint64_t SurfaceGui::computeRenderContentSignature(float defaultFontSize) {
    std::uint64_t hash = FNV_OFFSET_BASIS;
    hashString(hash, "SurfaceGuiRenderContentV1");
    hashVector2(hash, Size);

    const bool drawsBackground = hasRenderableOwnContent();
    hashByte(hash, drawsBackground ? 1 : 0);
    if (drawsBackground) {
        hashColor(hash, BackgroundColor);
    }

    std::uint64_t renderableChildCount = 0;
    for (auto const& [name, child] : getChildren()) {
        (void)name;
        if (isRenderableDirectChild(child.get())) ++renderableChildCount;
    }
    hashUint64(hash, renderableChildCount);

    // Rendererの既存走査順と同じ順でハッシュする。構造変更でunordered_mapの
    // 走査順が変わった場合も、実際の重なり順の変化として再ベイクされる。
    for (auto const& [name, instance] : getChildren()) {
        (void)name;
        if (!isRenderableDirectChild(instance.get())) continue;
        auto* child = static_cast<ScreenGuiObject*>(instance.get());
        hashString(hash, child->getClassName());
        hashString(hash, child->Name);
        hashUint64(hash, static_cast<std::uint64_t>(child->NormType));
        hashVector2(hash, child->Position);
        hashVector2(hash, child->Size);

        const bool childDrawsBackground = child->BackgroundColor.a > 0.001f;
        hashByte(hash, childDrawsBackground ? 1 : 0);
        if (childDrawsBackground) {
            hashColor(hash, child->BackgroundColor);
        }

        TextContent* text = child->textContent();
        const bool drawsText = text && !text->Text.empty() &&
            text->TextColor.a > 0.001f;
        hashByte(hash, drawsText ? 1 : 0);
        if (drawsText) {
            hashString(hash, text->Text);
            hashColor(hash, text->TextColor);
            const float effectiveFontSize = child->FontSize > 0.0f
                ? child->FontSize
                : defaultFontSize;
            hashFloat(hash, effectiveFontSize);
            hashByte(hash, child->UseFontFile ? 1 : 0);
            if (child->UseFontFile) {
                hashString(hash, child->FontFile);
            } else {
                hashUint64(hash, static_cast<std::uint64_t>(child->Font));
            }
        }

        ImageContent* image = child->imageContent();
        const bool drawsImage = image && image->textureID != 0;
        hashByte(hash, drawsImage ? 1 : 0);
        if (drawsImage) {
            hashString(hash, image->path);
            hashUint64(hash, image->textureID);
        }
    }
    return hash;
}

SurfaceGui::~SurfaceGui() {
    if (m_fboID) glDeleteFramebuffers(1, &m_fboID);
    if (m_texID) glDeleteTextures(1, &m_texID);
}

bool SurfaceGui::IsA(std::string name) {
    if (name == "SurfaceGui") return true;
    return WorldGuiObject::IsA(name);
}

void SurfaceGui::setProperty(const std::string& name, const YAML::Node& val) {
    if (PropertyRegistry::loadProperty(this, "SurfaceGui", name, val)) return;
    WorldGuiObject::setProperty(name, val);
}

std::shared_ptr<Instance> SurfaceGui::clone() const {
    auto copy = std::make_shared<SurfaceGui>();
    copy->Name = Name;
    PropertyRegistry::cloneFields(this, copy.get(), "SurfaceGui");
    for (auto const& [n, child] : children)
        copy->addChild(child->clone());
    return copy;
}

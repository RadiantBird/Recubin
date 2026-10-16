#pragma once
#include <string>
#include <Util/Color4.hpp>
#include <include/Core/GfxTexture.hpp>

// GUI コンテンツのコンポーネント（HasA）。
// TextLabel/TextButton が TextContent を、ImageLabel/ImageButton が ImageContent を保持し、
// 描画・エディター・Luau は GuiObject::textContent()/imageContent() 経由で問い合わせる。
// 要素矩形内での文字の揃え（既定は従来の描画と同じ「左寄せ・縦中央」）
enum class TextXAlignment { Left = 0, Center = 1, Right = 2 };
enum class TextYAlignment { Top = 0, Center = 1, Bottom = 2 };

struct TextContent {
    std::string    Text;
    Color4         TextColor = {0.f, 0.f, 0.f, 1.f};
    TextXAlignment XAlignment = TextXAlignment::Left;
    TextYAlignment YAlignment = TextYAlignment::Center;
    bool           TextScaled = false;  // true: FontSizeを無視し、要素矩形いっぱいに拡大・縮小する
};

struct ImageContent {
    std::string  path;          // 画像アセットパス（YAML キーは "Image"）
    TextureHandle textureID; // Renderer::loadTexture 経由

    void        setImage(const std::string& p);   // 実装は GuiContent.cpp（Renderer 依存のため）
    std::string getImage() const { return path; }
};

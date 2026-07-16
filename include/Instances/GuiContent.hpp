#pragma once
#include <string>
#include <Util/Color4.hpp>

// GUI コンテンツのコンポーネント（HasA）。
// TextLabel/TextButton が TextContent を、ImageLabel/ImageButton が ImageContent を保持し、
// 描画・エディター・Luau は GuiObject::textContent()/imageContent() 経由で問い合わせる。
struct TextContent {
    std::string Text;
    Color4      TextColor = {0.f, 0.f, 0.f, 1.f};
};

struct ImageContent {
    std::string  path;          // 画像アセットパス（YAML キーは "Image"）
    unsigned int textureID = 0; // GL テクスチャ（Renderer::loadTexture 経由）

    void        setImage(const std::string& p);   // 実装は GuiContent.cpp（Renderer 依存のため）
    std::string getImage() const { return path; }
};

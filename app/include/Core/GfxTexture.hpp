#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

// sokol の sg_image を指す型付きハンドル。既定値（id 0）は「テクスチャ無し」。
// 値として自由にコピーでき、所有はしない。破棄は作った側が GfxTextures::destroy で行う。
class TextureHandle {
public:
    TextureHandle() = default;
    explicit TextureHandle(std::uint32_t id);

    std::uint32_t id() const;
    bool isValid() const;
    explicit operator bool() const;
    bool operator==(const TextureHandle& other) const;
    bool operator!=(const TextureHandle& other) const;

    // 描画が生 GL のあいだ（移行期間）だけ使う。sokol が作った GL テクスチャ名。無効なら 0。
    unsigned int glName() const;

private:
    std::uint32_t m_id = 0;
};

template <>
struct std::hash<TextureHandle> {
    std::size_t operator()(const TextureHandle& handle) const noexcept {
        return std::hash<std::uint32_t>{}(handle.id());
    }
};

// テクスチャの作成・破棄（sokol_gfx 経由）。GfxDevice::setup 前や shutdown 後は何もしない。
namespace GfxTextures {

// RGBA16F / Depth / DepthStencil は描画先（renderTarget）専用。深度は sokol の形式（32F / 32F+S8）になる。
enum class Format { RGBA8, R8, RGBA16F, Depth, DepthStencil };
// サンプリングの設定。描画が生 GL のあいだは、作成時に GL テクスチャ側へ同じ設定を入れる。
enum class Filter { Nearest, Linear, LinearMipmap };
// ClampToBorderWhite は範囲外を 1.0 で読む（深度マップの外側を「遮られていない」とするため）。
enum class Wrap { Repeat, ClampToEdge, ClampToBorderWhite };

struct Desc {
    int width = 0;
    int height = 0;
    int arrayLayers = 0;              // 0 なら 2D テクスチャ、1 以上なら 2D 配列テクスチャ
    bool cubemap = false;             // cube テクスチャ（arrayLayers は 0 のまま。pixels は +X,-X,+Y,-Y,+Z,-Z の順に 6 面）
    Format format = Format::RGBA8;
    const void* pixels = nullptr;     // 先頭 mip の画素（glTexImage に渡していたのと同じ並び。配列なら全レイヤー分）。nullptr なら 0 埋め
    bool generateMipmaps = false;     // CPU で mip を作る（RGBA8 の 2D と cube のみ）
    bool renderTarget = false;        // 描画先として FBO に付ける（pixels は使わない）。深度の形式なら深度の描画先
    int mipLevels = 1;                // 描画先の mip 段数（各段へ個別に描く場合）
    Filter filter = Filter::Linear;
    Wrap wrap = Wrap::Repeat;
    const char* label = nullptr;
};

// 失敗時は無効なハンドルを返す（ログ出力済み）。
TextureHandle create(const Desc& desc);
// 破棄して handle を無効にする。無効なハンドルや shutdown 後は何もしない。
void destroy(TextureHandle& handle);
// 作成時の大きさ。無効なら false。
bool size(TextureHandle handle, int& width, int& height);

// シーンを描く色の描画先（RGBA8、線形、端で止める）。
TextureHandle createColorTarget(int width, int height, const char* label);
// シーンを描く深度の描画先（DepthStencil、最近傍、端で止める）。
// シーン間で深度をブリットするので、シーンの深度はすべてこれで作って形式を揃える。
TextureHandle createSceneDepthTarget(int width, int height, const char* label);

} // namespace GfxTextures

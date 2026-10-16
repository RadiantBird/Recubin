#include <Core/GfxTexture.hpp>
#include <Core/GfxDevice.hpp>
#include <Util/Logger.hpp>
#include <GL/glew.h>
#include <sokol/sokol_gfx.h>

#include <algorithm>
#include <cstring>
#include <vector>

// ---- TextureHandle ----
TextureHandle::TextureHandle(std::uint32_t id) : m_id(id) {}

std::uint32_t TextureHandle::id() const {
    return m_id;
}

bool TextureHandle::isValid() const {
    return m_id != 0;
}

TextureHandle::operator bool() const {
    return isValid();
}

bool TextureHandle::operator==(const TextureHandle& other) const {
    return m_id == other.m_id;
}

bool TextureHandle::operator!=(const TextureHandle& other) const {
    return m_id != other.m_id;
}

unsigned int TextureHandle::glName() const {
    if (!m_id || !GfxDevice::isValid()) return 0;
    const sg_gl_image_info info = sg_gl_query_image_info(sg_image{m_id});
    return info.tex[info.active_slot];
}

namespace GfxTextures {
namespace {

int bytesPerPixel(Format format) {
    return format == Format::R8 ? 1 : 4;
}

sg_pixel_format pixelFormat(Format format) {
    return format == Format::R8 ? SG_PIXELFORMAT_R8 : SG_PIXELFORMAT_RGBA8;
}

// RGBA8 の mip を 1 段縮小する（2x2 の平均。奇数の辺は端の画素を繰り返す）。
std::vector<unsigned char> downsampleRgba8(const unsigned char* src, int srcW, int srcH, int dstW, int dstH) {
    std::vector<unsigned char> dst(static_cast<size_t>(dstW) * static_cast<size_t>(dstH) * 4);
    for (int y = 0; y < dstH; ++y) {
        const int y0 = std::min(y * 2, srcH - 1);
        const int y1 = std::min(y * 2 + 1, srcH - 1);
        for (int x = 0; x < dstW; ++x) {
            const int x0 = std::min(x * 2, srcW - 1);
            const int x1 = std::min(x * 2 + 1, srcW - 1);
            for (int c = 0; c < 4; ++c) {
                const int sum = src[(static_cast<size_t>(y0) * srcW + x0) * 4 + c] +
                                src[(static_cast<size_t>(y0) * srcW + x1) * 4 + c] +
                                src[(static_cast<size_t>(y1) * srcW + x0) * 4 + c] +
                                src[(static_cast<size_t>(y1) * srcW + x1) * 4 + c];
                dst[(static_cast<size_t>(y) * dstW + x) * 4 + c] = static_cast<unsigned char>((sum + 2) / 4);
            }
        }
    }
    return dst;
}

// 先頭 mip から 1x1 までの mip を作る（sokol に mip 生成の API が無いため）。
std::vector<std::vector<unsigned char>> buildMipChain(const unsigned char* level0, int width, int height) {
    std::vector<std::vector<unsigned char>> chain;
    int w = width;
    int h = height;
    const unsigned char* src = level0;
    while ((w > 1 || h > 1) && static_cast<int>(chain.size()) + 1 < SG_MAX_MIPMAPS) {
        const int nextW = std::max(w / 2, 1);
        const int nextH = std::max(h / 2, 1);
        chain.push_back(downsampleRgba8(src, w, h, nextW, nextH));
        src = chain.back().data();
        w = nextW;
        h = nextH;
    }
    return chain;
}

GLint glMinFilter(Filter filter) {
    switch (filter) {
        case Filter::Nearest:      return GL_NEAREST;
        case Filter::LinearMipmap: return GL_LINEAR_MIPMAP_LINEAR;
        default:                   return GL_LINEAR;
    }
}

// 描画が生 GL のあいだは、テクスチャ側のサンプリング設定で描く。sokol 移行前と同じ設定を入れる。
void applyGlSampling(TextureHandle handle, const Desc& desc) {
    const sg_gl_image_info info = sg_gl_query_image_info(sg_image{handle.id()});
    const GLenum target = static_cast<GLenum>(info.tex_target);
    const GLenum bindingQuery = target == GL_TEXTURE_2D_ARRAY ? GL_TEXTURE_BINDING_2D_ARRAY : GL_TEXTURE_BINDING_2D;
    GLint previous = 0;
    glGetIntegerv(bindingQuery, &previous);
    glBindTexture(target, info.tex[info.active_slot]);
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, glMinFilter(desc.filter));
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, desc.filter == Filter::Nearest ? GL_NEAREST : GL_LINEAR);
    const GLint wrap = desc.wrap == Wrap::Repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    glTexParameteri(target, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, wrap);
    glBindTexture(target, static_cast<GLuint>(previous));
}

bool validateDesc(const Desc& desc) {
    if (desc.width <= 0 || desc.height <= 0 || desc.arrayLayers < 0) {
        RCBN_ERROR("GfxTextures: invalid size " << desc.width << "x" << desc.height << " layers "
                   << desc.arrayLayers << " (" << (desc.label ? desc.label : "") << ")");
        return false;
    }
    if (desc.generateMipmaps && (desc.format != Format::RGBA8 || desc.arrayLayers > 0 || desc.renderTarget)) {
        RCBN_ERROR("GfxTextures: mipmaps are only generated for RGBA8 2D textures ("
                   << (desc.label ? desc.label : "") << ")");
        return false;
    }
    return true;
}

} // namespace

TextureHandle create(const Desc& desc) {
    if (!GfxDevice::isValid()) return {};
    if (!validateDesc(desc)) return {};

    const int layers = std::max(desc.arrayLayers, 1);
    const size_t levelBytes = static_cast<size_t>(desc.width) * static_cast<size_t>(desc.height) *
                              static_cast<size_t>(layers) * static_cast<size_t>(bytesPerPixel(desc.format));

    sg_image_desc imageDesc = {};
    imageDesc.type = desc.arrayLayers > 0 ? SG_IMAGETYPE_ARRAY : SG_IMAGETYPE_2D;
    imageDesc.width = desc.width;
    imageDesc.height = desc.height;
    imageDesc.num_slices = layers;
    imageDesc.pixel_format = pixelFormat(desc.format);
    imageDesc.label = desc.label;

    // 不変の画像は作成時に内容が必要。画素が無ければ 0 で埋める。
    std::vector<unsigned char> zeroPixels;
    std::vector<std::vector<unsigned char>> mipChain;
    if (desc.renderTarget) {
        imageDesc.usage.color_attachment = true;
        imageDesc.num_mipmaps = 1;
    } else {
        const void* level0 = desc.pixels;
        if (!level0) {
            zeroPixels.assign(levelBytes, 0);
            level0 = zeroPixels.data();
        }
        imageDesc.data.mip_levels[0] = sg_range{level0, levelBytes};
        if (desc.generateMipmaps) {
            mipChain = buildMipChain(static_cast<const unsigned char*>(level0), desc.width, desc.height);
            for (size_t i = 0; i < mipChain.size(); ++i) {
                imageDesc.data.mip_levels[i + 1] = sg_range{mipChain[i].data(), mipChain[i].size()};
            }
        }
        imageDesc.num_mipmaps = static_cast<int>(mipChain.size()) + 1;
    }

    sg_image image = {};
    {
        GfxDevice::SokolSection section;
        image = sg_make_image(&imageDesc);
    }
    if (sg_query_image_state(image) != SG_RESOURCESTATE_VALID) {
        RCBN_ERROR("GfxTextures: sg_make_image failed (" << (desc.label ? desc.label : "") << ")");
        sg_destroy_image(image);
        return {};
    }
    const TextureHandle handle(image.id);
    applyGlSampling(handle, desc);
    return handle;
}

void destroy(TextureHandle& handle) {
    // GL バックエンドの破棄は glDeleteTextures だけ（区間の外では sokol のキャッシュは空）。
    // SokolSection で囲むと、終了時の復元が削除済みの名前を束縛し直そうとして GL エラーになるので囲まない。
    if (handle && GfxDevice::isValid()) sg_destroy_image(sg_image{handle.id()});
    handle = TextureHandle{};
}

bool size(TextureHandle handle, int& width, int& height) {
    if (!handle || !GfxDevice::isValid()) return false;
    const sg_image image{handle.id()};
    if (sg_query_image_state(image) != SG_RESOURCESTATE_VALID) return false;
    width = sg_query_image_width(image);
    height = sg_query_image_height(image);
    return true;
}

} // namespace GfxTextures

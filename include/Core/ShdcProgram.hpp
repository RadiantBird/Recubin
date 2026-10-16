#pragma once

#include <array>
#include <cstddef>
#include <string>

struct sg_shader_desc;

// sokol-shdc が生成したシェーダー（GLSL 4.1）を、生 GL のプログラムとして使うための移行用ラッパー。
// 描画を sokol へ移す（段階 4）までの間、シェーダーとユニフォームの形だけ先に sokol 形式へ揃える。
// - ユニフォームは sokol-shdc 生成の構造体（<module>_<block>_t）ごと applyUniforms で送る。
//   GL では各ブロックが vec4 配列 1 つになっているので、glUniform4fv 1 回で済む。
// - テクスチャユニットは texture_sampler_pairs の番号を使う（sokol の GL バックエンドと同じ割り当て）。
// - 頂点属性の位置はシェーダー側の layout(location = N) で決まる。
class ShdcProgram {
public:
    ShdcProgram();
    ~ShdcProgram();
    ShdcProgram(const ShdcProgram&) = delete;
    ShdcProgram& operator=(const ShdcProgram&) = delete;

    // 生成ヘッダーの <module>_<program>_shader_desc(SG_BACKEND_GLCORE) を渡す。
    // 失敗時は false（ログ出力済み）で、プログラムは無効のまま。
    bool create(const sg_shader_desc& desc, const char* label);
    void destroy();
    bool isValid() const;
    unsigned int program() const;
    void use() const;

    // blockSlot には生成ヘッダーの UB_xxx を渡す。use() の後で呼ぶこと。
    template <class T>
    void applyUniforms(int blockSlot, const T& data) const { applyUniforms(blockSlot, &data, sizeof(T)); }
    void applyUniforms(int blockSlot, const void* data, std::size_t size) const;

    // viewSlot には生成ヘッダーの VIEW_xxx を渡す。そのビューを使うペアのユニットへ束縛する。
    // target は GL_TEXTURE_2D など。
    void bindTexture(int viewSlot, unsigned int target, unsigned int texture) const;

    static constexpr int MAX_UNIFORM_BLOCKS = 8;
    static constexpr int MAX_TEXTURE_SAMPLER_PAIRS = 32;

private:
    struct UniformBlock {
        int location = -1;
        int vec4Count = 0;
        std::size_t size = 0;
    };

    bool cacheUniformBlocks(const sg_shader_desc& desc);
    bool assignTextureUnits(const sg_shader_desc& desc);

    unsigned int m_program = 0;
    std::string m_label;
    std::array<UniformBlock, MAX_UNIFORM_BLOCKS> m_uniformBlocks{};
    std::array<int, MAX_TEXTURE_SAMPLER_PAIRS> m_pairViewSlot{}; // ペア番号（=ユニット）→ ビュースロット。-1 は未使用
};

#pragma once
#include <Instances/Named.hpp>
#include <Instances/Instance.hpp>
#include <Instances/Decal.hpp>
#include <Util/Color4.hpp>
#include <Math/Vector2.hpp>
#include <Math/Vector3.hpp>
#include <vector>

// Cubeの面に貼る、ドット単位でペイントできる動的テクスチャ。
// CPU側RGBA8ピクセルバッファを保持し、変更時のみglTexSubImage2DでGLテクスチャへアップロードする。
// SurfaceGuiと同じ isSurfaceGui=1 ブレンド経路（mix(ourColor, texColor, texColor.a)）で面に合成される。
// ピクセルデータはYAML保存しない。
class Canvas : public Named<Canvas, Instance> {
public:
    static constexpr const char* ClassName = "Canvas";

    Face   face = Face::Front;
    int    Width  = 64;   // ピクセル解像度 1〜1024
    int    Height = 64;
    Color4 BackgroundColor; // 既定 (0,0,0,0)。Clear/初期化の塗り色。透明ならCube色が透ける

    // GPU/CPUリソース（非プロパティ）
    // m_pixels はRGBA8、行0=面の下端（GLテクスチャ規約）
    std::vector<unsigned char> m_pixels;
    unsigned int m_texID = 0;
    int  m_texW = 0, m_texH = 0;  // 確保済みGLテクスチャの実サイズ
    bool m_dirty = false;
    // バッファを塗った時点の背景色(RGBA8)。BackgroundColor変更の検知に使う
    unsigned char m_bufferBg[4] = {0, 0, 0, 0};

    Canvas();
    ~Canvas();
    bool IsA(std::string name) override;
    void setProperty(const std::string& name, const YAML::Node& val) override;
    std::shared_ptr<Instance> clone() const override;

    // CPUピクセルバッファの確保（Width/Height変更の同期点）。GLに触れないので描画前・ヘッドレスでも安全
    void ensureBuffer();
    // 描画パスから毎フレーム呼ばれる同期点（ensureBuffer + GLテクスチャ確保/アップロード）
    void ensureGPU();

    // 左上原点。範囲外はfalse
    bool setPixel(int x, int y, const Color4& c);
    bool getPixel(int x, int y, Color4& out);
    void clear(const Color4& c);

    // ワールド座標→この面のUV（SetPixel座標系: U右向き0-1、V下向き0-1）。面外/親不在はfalse
    bool worldToUV(const Vector3& worldPos, Vector2& out) const;
};

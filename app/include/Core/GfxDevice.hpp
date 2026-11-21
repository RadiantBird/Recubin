#pragma once

#include <memory>

struct GlStateSnapshot;

// sokol_gfx（現在は GL バックエンド）の初期化・終了と、生の GL 呼び出しとの共存を受け持つ。
//
// 共存の規則:
// - sokol は GL の状態をキャッシュしており、リセット時に深度/ブレンド/カリング/シザー/
//   FrontFace/テクスチャユニット/VAO などを sokol 既定値へ書き換える。
// - 生 GL のフレーム中で sokol を使うときは、その区間を必ず GfxDevice::SokolSection で囲む。
//   開始時に生 GL の状態を保存して sokol のキャッシュをリセットし、終了時に sokol の
//   バインドを外してから生 GL の状態を戻す。
// - 区間の外では sokol のキャッシュに何も残らないため、commit() は区間の外で呼んでよい。
class GfxDevice {
public:
    // GL コンテキストを current にした後に 1 回だけ呼ぶ。失敗時は false（ログ出力済み）。
    static bool setup();
    // GL コンテキストを破棄する前に呼ぶ。setup していなければ何もしない。
    static void shutdown();
    static bool isValid();
    // 1 フレームに 1 回、バッファ入れ替えの前に呼ぶ。
    static void commit();

    // sokol を使う区間を囲むスコープガード。setup 前（または失敗時）は何もしない。
    class SokolSection {
    public:
        SokolSection();
        ~SokolSection();
        SokolSection(const SokolSection&) = delete;
        SokolSection& operator=(const SokolSection&) = delete;

    private:
        std::unique_ptr<GlStateSnapshot> m_saved;
    };
};

#pragma once

#include <Core/InputKey.hpp>
#include <Core/CursorImage.hpp>
#include <string>

// ==================================================================
//  IInputBackend
//
//  入力の供給源を抽象化するインターフェイス。User はこの IF 経由でのみ
//  入力を読み、特定のウィンドウライブラリ(GLFW)に直接依存しない。
//  将来的にネットワーク経由の入力など別実装に差し替えられるようにする。
// ==================================================================
class IInputBackend {
public:
    virtual ~IInputBackend() = default;

    // 指定キーが現在押されているか
    virtual bool isKeyDown(KeyCode key) const = 0;

    // 指定マウスボタンが現在押されているか
    virtual bool isMouseButtonDown(MouseButton button) const = 0;

    // 現在のカーソル座標(ウィンドウ座標系)を取得する
    virtual void getCursorPos(double& x, double& y) const = 0;

    // カーソル座標を設定する(回転時の再センタリング用)
    virtual void setCursorPos(double x, double y) = 0;

    // マウスを掴む(非表示・ロックして無制限移動を得る)/解放する。
    // 回転ドラッグ中のカーソル固定に使う(画面端クランプ・加速を回避し滑らかな delta を得る)。
    virtual void setMouseCaptured(bool captured) = 0;

    // 加工済みRGBA画像をOSカーソルへ適用する。
    virtual bool setCustomCursor(const CursorImageData& image) = 0;

    // 前回の呼び出し以降に蓄積されたスクロール量を返し、内部カウンタを 0 にリセットする
    virtual double consumeScrollDelta() = 0;

    // Returns accumulated relative motion while the backend owns mouse capture.
    // Backends that only expose absolute cursor positions keep the default.
    virtual bool consumeRelativeMouseDelta(double& x, double& y) {
        x = 0.0;
        y = 0.0;
        return false;
    }

    // True when this backend owns a relative-mouse stream while captured.
    // This distinguishes a valid zero-delta frame from an absolute-only backend.
    virtual bool usesRelativeMouseInput() const { return false; }
};

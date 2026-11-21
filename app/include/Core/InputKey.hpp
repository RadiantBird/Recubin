#pragma once

// ==================================================================
//  入力の抽象表現
//
//  GLFW などのウィンドウ/入力ライブラリに依存しない、エンジン共通の
//  キー/マウスボタン識別子。バックエンド(IInputBackend)実装側で
//  各ライブラリの定数へ変換する。
// ==================================================================

enum class KeyCode {
    Unknown = 0,

    // 英字
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

    // 数字(キーボード上段)
    Num0, Num1, Num2, Num3, Num4,
    Num5, Num6, Num7, Num8, Num9,

    // 矢印
    Up, Down, Left, Right,

    // 特殊キー
    Escape, Space, Enter, Tab, Backspace,
    LeftShift, RightShift,
    LeftControl, RightControl,
    LeftAlt, RightAlt,

    // ファンクションキー
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
};

enum class MouseButton {
    Left,
    Right,
    Middle,
};

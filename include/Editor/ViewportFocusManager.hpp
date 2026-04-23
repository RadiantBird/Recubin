#pragma once
#include <Editor/ViewportPanel.hpp>
#include <mutex>
#include <atomic>

// ===================================================
//  ViewportFocusManager — Viewportのフォーカス状態を排他管理するシングルトン
//  
//  複数のViewportが存在する場合、同時に1つだけフォーカスされることを保証する。
//  クリック検出時に自動的にフォーカスが切り替わる。
// ===================================================
class ViewportFocusManager {
public:
    // シングルトンインスタンス取得
    static ViewportFocusManager& getInstance() {
        static ViewportFocusManager instance;
        return instance;
    }

    // インスタンスが初期化されているか
    static bool isInitialized() {
        return getInstance().currentFocusedViewport != nullptr;
    }

    // Viewportがクリック/フォーカスされたときに呼び出す（排他制御）
    void onFocusViewport(ViewportPanel* viewport) {
        if (!viewport) return;
        
        std::lock_guard<std::mutex> lock(focusMutex);
        
        // 既にフォーカスされているViewportと異なる場合のみ更新
        if (currentFocusedViewport != viewport) {
            // 以前のViewportのフォーカスを解除
            if (currentFocusedViewport) {
                currentFocusedViewport->isViewportFocused = false;
            }
            
            // 新しいViewportにフォーカスを設定
            currentFocusedViewport = viewport;
            if (currentFocusedViewport) {
                currentFocusedViewport->isViewportFocused = true;
            }
            
            focusGeneration.fetch_add(1, std::memory_order_release);
        }
    }

    // 現在のフォーカスされたViewportを取得
    ViewportPanel* getFocusedViewport() const {
        return currentFocusedViewport;
    }

    // 指定したViewportが現在フォーカスされているか
    bool isFocused(ViewportPanel* viewport) const {
        return currentFocusedViewport == viewport;
    }

    // フォーカスを解除（全Viewportのフォーカスを外す）
    void clearFocus() {
        std::lock_guard<std::mutex> lock(focusMutex);
        
        if (currentFocusedViewport) {
            currentFocusedViewport->isViewportFocused = false;
        }
        currentFocusedViewport = nullptr;
        focusGeneration.fetch_add(1, std::memory_order_release);
    }

    // 現在のフォーカス世代番号を取得（デバッグ用）
    unsigned int getFocusGeneration() const {
        return focusGeneration.load(std::memory_order_acquire);
    }

    // フォーカスされているViewportの数（通常は0か1）
    int getFocusedCount() const {
        return currentFocusedViewport ? 1 : 0;
    }

    // デバッグ情報出力
    void debugPrint() const;

    // シングルトンなのでコピー/移動禁止
    ViewportFocusManager(const ViewportFocusManager&) = delete;
    ViewportFocusManager& operator=(const ViewportFocusManager&) = delete;
    ViewportFocusManager(ViewportFocusManager&&) = delete;
    ViewportFocusManager& operator=(ViewportFocusManager&&) = delete;

private:
    ViewportFocusManager() = default;
    ~ViewportFocusManager() = default;

    mutable std::mutex focusMutex;              // スレッドセーフのためのミューテックス
    ViewportPanel* currentFocusedViewport = nullptr;  // 現在フォーカスされているViewport
    std::atomic<unsigned int> focusGeneration{0};     // フォーカス変更の世代番号（デバッグ用）
};

// ===================================================
//  インラインヘルパー関数
// ===================================================

// 指定したViewportにフォーカスを設定（マネージャーが初期化されている必要がある）
inline void SetViewportFocus(ViewportPanel* viewport) {
    ViewportFocusManager::getInstance().onFocusViewport(viewport);
}

// 現在のフォーカスViewportを取得
inline ViewportPanel* GetFocusedViewport() {
    return ViewportFocusManager::getInstance().getFocusedViewport();
}

// フォーカスをクリア
inline void ClearViewportFocus() {
    ViewportFocusManager::getInstance().clearFocus();
}

// 指定したViewportがフォーカスされているか
inline bool IsViewportFocused(ViewportPanel* viewport) {
    return ViewportFocusManager::getInstance().isFocused(viewport);
}
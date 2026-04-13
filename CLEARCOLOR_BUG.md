# バグ調査メモ：Luauエラー時にクリアカラーが黒になる

## 現象

- Luauスクリプトでランタイムエラー（例: `a:FindChild("test")` で nil インデックス）が発生すると、背景が黒になる。
- Luauスクリプトでローダエラー（例: `jkfduh` という不正な識別子）の場合は**黒にならない（正常）**。
- レンダリングパイプライン自体は動作しており、Cube等のオブジェクトは正常に描画される。

## 検証済み事項

### `glClearColor` が効いていない

```cpp
glClearColor(0.0f, 0.5f, 0.75f, 1.0f);

GLfloat actualClear[4];
glGetFloatv(GL_COLOR_CLEAR_VALUE, actualClear);
// → エラーなし: 0 0.5 0.75 1  ✅
// → エラーあり: 0 0 0 0        ❌
```

`glClearColor` を呼んだ直後に `glGetFloatv` で読み返しても `0 0 0 0`（黒）のまま。
つまり **`glClearColor` 自体が無効な状態になっている**。

### `glGetError` は無反応

GL エラーは報告されていない（`GL_NO_ERROR`）。

### `glfwMakeContextCurrent` では解決しなかった（未確認）

`executeWorkspaceScripts()` 直後に `glfwMakeContextCurrent(window)` を追加したが、
まだ `0 0 0 0` のまま（修正後のビルドでの `wglGetCurrentContext` の値は未確認）。

---

## 仮説一覧

### 仮説1（最有力）: longjmp による WGL ファイバーローカルストレージの破壊

**根拠：**
- Luauは内部エラー伝播に `setjmp / longjmp` を使用している。
- Windows の WGL（Windows OpenGL）は **スレッドローカルストレージ（または FLS: Fiber Local Storage）** を使って「カレント GL コンテキスト」を管理している。
- MSVC 環境では、`longjmp` が FLS をリセット・破壊することが知られている。
- `lua_resume` の内部で `luaL_checkudata` → `luaL_error` → `lua_error` → `longjmp` の連鎖が起きる。
- この `longjmp` が WGL の FLS を壊し、GL コンテキストが "not current" になる。
- `glClearColor` は current でないコンテキストに対しては無効（silent no-op）。

**確認方法：**
```cpp
#include <Windows.h>
HGLRC ctx = wglGetCurrentContext();
// エラー後に nullptr なら WGL コンテキストが失われている
```

**対策案：**
- `glfwMakeContextCurrent(window)` をスクリプト実行後に毎フレーム呼ぶ（実装済みだがまだ効果未確認）
- または `wglMakeCurrent` を直接呼ぶ

### 仮説2: `luaopen_base` がGL状態に干渉している

**根拠：**
- `luaopen_base(L)` を呼んでいる（Luau の base ライブラリ初期化）。
- もしこれが独自のパニックハンドラを登録し、それが何らかのかたちで GL を呼んでいるなら影響がある。
- ただし `luaopen_base` は GL に触れないはずで、可能性は低い。

**確認方法：**
- `luaopen_base` をコメントアウトしてテストする（`print` は使えなくなる）。

### 仮説3: `glGetFloatv` の読み取りが誤っている（計測ミス）

**根拠：**
- `glGetFloatv(GL_COLOR_CLEAR_VALUE, ...)` が正しい値を返してない可能性。
- `glClearColor` は実際には効いていて `glClear` も正常に行われているが、何か別の原因で黒く見える。

**反証：**
- エラーなしのときは `0 0.5 0.75 1` を正しく返している。
- エラーありに切り替えると同じコードが `0 0 0 0` を返す。計測ミスではなく、状態の差が出ている。

---

## 現在のコードの状態

### `main.cpp` の変更点

```cpp
luauEngine.executeWorkspaceScripts();

// WGL コンテキスト保護のための再設定
glfwMakeContextCurrent(window);

// デバッグ: wglGetCurrentContext の確認
HGLRC ctx = wglGetCurrentContext();
std::cout << "[DBG] wglGetCurrentContext after Luau = " << (void*)ctx << std::endl;

glClearColor(0.0f, 0.5f, 0.75f, 1.0f);

GLfloat actualClear[4];
glGetFloatv(GL_COLOR_CLEAR_VALUE, actualClear);
// [DBG frame N] clear color = ... を出力
```

---

## 次のアクション

1. `wglGetCurrentContext` の戻り値を確認する
   - `nullptr` → WGL コンテキストが消えている（仮説1 確定）
   - 有効なポインタ → 別の原因を探す

2. `wglGetCurrentContext` が `nullptr` だった場合：
   - `glfwMakeContextCurrent(window)` でリカバリできるか確認  
   - またはスクリプトエラーが起きないより根本的な対策（Luauの `pcall` 相当でエラーを捕捉する）

3. 最終的な恒久対策として：
   - C から呼ばれる Lua のエラーハンドリングを `lua_pcall` や保護モードに切り替え、`longjmp` が FLS に届かないようにする


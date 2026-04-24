# TODO 一覧

スキャン日: 2026-04-25

---

## 高優先度

### [main.cpp:126](src/main.cpp#L126) — isPlaying / isPaused フラグの管理
```cpp
// TODO: これらのフラグはUserでも使いたいので、適切に管理する方法を考えておく
const bool isPlaying = renderer->editor && !renderer->editor->isEditMode();
```
`isPlaying` / `isPaused` フラグが `main.cpp` のループ内でのみ計算されており、`User` クラスからアクセスできない。適切な共有手段（例: GameState クラス、グローバルフラグ管理）を検討する。

---

### [main.cpp:165](src/main.cpp#L165) — viewportFocused / viewportZoomEnabled フラグの管理
```cpp
// TODO: これらのフラグはUserでも使いたいので、適切に管理する方法を考えておく
const bool viewportFocused = renderer->editor && renderer->editor->viewportPanel && ...;
```
上と同様。ビューポートのフォーカス・ズーム状態フラグも `User` クラスで参照したいが、現状は `main.cpp` のみで保持されている。

---

## 中優先度

### [Sound.cpp:50](src/Instances/Sound.cpp#L50) — SoundGroup の動的切り替え
```cpp
// TODO: この問題をどうにかする
// ※ すでにロードされている場合にグループを動的に変更するには re-init が必要
```
`SoundGroup` プロパティがロード後に変更された場合、miniaudio は再初期化を必要とする。現状はロード前に設定されることを前提としているが、動的切り替えに対応する方法を検討する。

---

## 低優先度

### [Workspace.cpp:44](src/Instances/Workspace.cpp#L44) — buildTestSpace() 未実装
```cpp
void Workspace::buildTestSpace() {
    // TODO
}
```
デバッグ用のテストシーン構築関数。本体が空のまま。必要に応じて実装する。

---

*NOTE: `LuauEngine.cpp:230` の `NOTE:` コメントは実装ドキュメントであり、作業項目ではないため除外。*

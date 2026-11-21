# 仕様違反リスト

最終更新: 2026-05-21

---

## 重大

### 1. Luauバインディング不足（対応済み）
仕様:「Luau側に基本的なRead/Writeプロパティをバインディングする」

| クラス | 未バインドプロパティ | 対応 |
|---|---|---|
| Rope | MaxDistance, Stiffness, Damping, Color, LineWidth | 実装済み |
| Rod | Color, LineWidth | 実装済み |
| Weld | Cube0, Cube1（読み取り） | 実装済み |
| CharacterSetting | JumpPower, MoveSpeed, FacePath, 各Color | 実装済み |
| AppImage | IconPath | 実装済み |
| Script | Enabled, Path, Source | 実装済み |

### 2. ファイルパス読み込み失敗時の警告ログが不統一
仕様:「読み込みに失敗すれば警告ログを出力」

| ファイル | 問題 |
|---|---|
| `src/Instances/Sound.cpp` | `std::cout << "[ERROR]"` 使用（RCBN_WARN 未使用） |
| `src/Instances/Script.cpp` | エラー時サイレント（警告なし） |
| `src/Core/Renderer.cpp` | `std::cout` 使用（ロガー未使用） |

---

## 中程度

### 3. Workspace が「Insert Object」リストに未登録
仕様:「エディターの『Insert Object』リストに登録される」

- 対象: `src/Editor/SceneHierarchyPanel.cpp` の `renderInsertMenu()`
- Workspace は将来的に複数になる予定のため対応が必要

### 4. Workspace がプロパティパネルに未公開
仕様:「エディターに基本的なプロパティを公開する」

- 対象: `src/Editor/PropertiesPanel.cpp`

---

## 対象外（仕様で除外）

- **System**: シングルトン（常に1つのみ）のため Insert Object リスト・プロパティパネルへの登録は不要

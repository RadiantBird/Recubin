# EditorPanel

`include/Editor/EditorPanel.hpp`

すべてのエディタパネルの抽象基底クラス。

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `title` | `string` | ImGui ウィンドウのタイトル |
| `isOpen` | `bool` | パネルを表示するか |

## メソッド

| メソッド | 説明 |
|---|---|
| `onRender()` | **純粋仮想**。このパネルの ImGui 描画処理 |

## 依存関係

- ImGui

## 継承クラス

| クラス | 役割 |
|---|---|
| `ConsolePanel` | ログ出力コンソール |
| `SceneHierarchyPanel` | シーン階層ツリービュー |
| `PropertiesPanel` | 選択オブジェクトのプロパティ編集 |
| `ViewportPanel` | 3D ゲームビューとギズモ操作 |
| `ContentBrowserPanel` | アセットファイルブラウザ |

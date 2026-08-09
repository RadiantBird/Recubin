# ViewportPanel

`include/Editor/ViewportPanel.hpp`

3D シーンをパネル専用 FBO に描画して ImGui 内に表示し、選択、Terrain 編集、Weld、
ImGuizmo 操作、自由ドラッグなどのビューポート操作を統括するパネル。

## 構成

メインビューポートとセカンダリビューポートは、どちらも同じ `ViewportPanel` クラスを使う。
メインは `User` のカメラを共有し、セカンダリは `m_useOwnCamera` と `m_camPos` / `m_camYaw` /
`m_camPitch` による独立カメラを使う。FBO、フォーカス、選択操作の処理は各インスタンスで独立する。

`onRender()` は ImGui と ImGuizmo のスコープを所有するフレームオーケストレーターであり、
個々の機能は private メソッドへ分割されている。表示領域は private 値型 `ViewportLayout` に
まとめ、レターボックス後の画像原点とサイズをすべての投影・入力処理で共有する。カメラ値は
入力中に変化するため `ViewportLayout` には保存しない。

private メソッドは次の責務に分かれる。

| グループ | メソッド | 責務 |
|---|---|---|
| 表示 | `renderLayoutAndScene()` | FBO リサイズ、シーン描画、レターボックス、ゲーム GUI 合成 |
| フォーカス／カメラ | `updateViewportFocus()`, `updateOwnCameraInput()` | ビューポートフォーカスと独立カメラ入力 |
| クリックツール | `updateTerrainBrush()`, `updateWeldMode()`, `handleViewportClick()` | クリックの優先消費と Picker／Decal／通常選択 |
| 選択表示／操作 | `updateBoxSelection()`, `updateGizmo()`, `drawSelectionHighlights()`, `drawModelHighlight()` | 矩形選択、ギズモ、Cube／Model ハイライト |
| ドラッグ／キー | `updateFreeDrag()`, `moveFreeDragSelection()`, `handlePivotShortcut()`, `handleFocusShortcut()` | 自由移動、Undo 記録、Tab ピボット、F フォーカス |

## フレーム内の順序

`onRender()` は次の順序を維持する。

1. ImGui ウィンドウとビューポート固有の ImGuizmo ID スコープを開始する。
2. `renderLayoutAndScene()` で表示領域を決定し、シーン画像とゲーム GUI を描画する。
3. フォーカスを更新し、セカンダリの場合は独立カメラ入力を反映する。
4. Terrain ブラシ、Weld の順にクリックを処理する。
5. 未消費のクリックを Picker、Decal、通常選択の順で処理する。
6. 矩形選択、フォーカス枠、ギズモ、Cube ハイライト、Model ハイライトを更新・描画する。
7. 自由ドラッグとその Undo 記録、Tab ピボット、F フォーカスを処理する。
8. ImGuizmo ID と ImGui ウィンドウのスコープを終了する。

下位メソッドは ImGui の Begin/End や ImGuizmo の PushID/PopID を終了しない。
早期 return は担当機能だけを打ち切り、スコープの後始末は常に `onRender()` が行う。

## ヘルパーモジュール

### ViewportGeometry

`include/Editor/ViewportGeometry.hpp` / `src/Editor/ViewportGeometry.cpp`

UI やシーンツリーに依存しない幾何計算を提供する。

- 画面座標から 45 度 FOV の `Ray` を生成
- OBB レイ判定とヒット距離・ローカル軸・面符号の取得
- OBB 支持半径と SAT 交差判定
- ワールド座標から親 Spatial のローカル位置・回転への変換
- ワールド座標の画面投影
- 回転済み OBB を含む `WorldAabb` の集約

結果は `Ray`、`ObbRayHit`、`ProjectedPoint`、`WorldAabb` の値型で返す。

### ViewportSceneQueries

`include/Editor/ViewportSceneQueries.hpp` / `src/Editor/ViewportSceneQueries.cpp`

`Workspace` / `Instance` の再帰走査を名前付き関数として提供する。

- 最近傍 BaseCube と Picker 対象（BaseCube／Attachment）の検索
- 矩形内の選択可能 Cube の収集
- Model 子孫のワールド AABB 計算
- 自由ドラッグ用の軸別／最小移動量による衝突フィット
- BaseCube の Locked 判定

通常選択の最近傍検索は Locked Cube もヒットとして返す。これにより最前面の Locked Cube が
背後の未ロック Cube を遮り、呼び出し側が通常クリック時の選択解除を適用できる。矩形選択では
Locked Cube 自身とその子孫を収集しない。

## 公開ライフサイクル

| メソッド | 説明 |
|---|---|
| `initFBO(w, h)` | FBO、カラーテクスチャ、深度バッファを生成 |
| `resizeFBO(w, h)` | 必要な場合だけ FBO を指定解像度へ再生成 |
| `destroyFBO()` | OpenGL リソースを解放 |
| `beginRender()` | FBO をバインドしてクリア |
| `endRenderAndDisplay()` | FBO のバインドを解除 |
| `onRender()` | パネルの描画・入力処理を順次実行 |

## 主な依存関係

- `EditorPanel`, ImGui, ImGuizmo, OpenGL
- `Renderer`, `User`, `Workspace`, `CommandHistory`
- `ViewportFocusManager`
- `ViewportGeometry`, `ViewportSceneQueries`

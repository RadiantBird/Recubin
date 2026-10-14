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

FBOのカラーテクスチャは線形補間を使い、S/T方向を`GL_CLAMP_TO_EDGE`に設定する。これにより、
レターボックスとの境界でテクスチャ端を補間するときに反対側の端が混ざるノイズを防ぐ。

private メソッドは次の責務に分かれる。

| グループ | メソッド | 責務 |
|---|---|---|
| 表示 | `renderLayoutAndScene()` | FBO リサイズ、シーン描画、レターボックス（`System.UseFixedResolution` が false なら無し）、ゲーム GUI 合成 |
| フォーカス／カメラ | `updateViewportFocus()`, `updateOwnCameraInput()` | ビューポートフォーカスと独立カメラ入力 |
| クリックツール | `updateTerrainBrush()`, `updateWeldMode()`, `handleViewportClick()` | クリックの優先消費と Picker／Decal／通常選択 |
| 選択表示／操作 | `updateBoxSelection()`, `updateGizmo()`, `drawHoverHighlight()` | 矩形選択、ギズモ、クリック候補の事前ハイライト |
| 画面GUI | `updateGuiEdit()` ほか（`ViewportGuiEdit.cpp`） | GUIタブ中の画面GUIのクリック選択、ドラッグ移動、選択枠、吸着ガイド |
| ドラッグ／キー | `updateFreeDrag()`, `moveFreeDragSelection()`, `handlePivotShortcut()`, `handleFocusShortcut()` | 自由移動、Undo 記録、Tab ピボット、F フォーカス |

## フレーム内の順序

`onRender()` は次の順序を維持する。

1. ImGui ウィンドウとビューポート固有の ImGuizmo ID スコープを開始する。
2. `renderLayoutAndScene()` で表示領域を決定し、シーン画像とゲーム GUI を描画する。
3. フォーカスを更新し、セカンダリの場合は独立カメラ入力を反映する。
4. 画面GUI（`updateGuiEdit()`）を処理する。GUIタブ中はここでクリックを扱い、5〜7の3D操作は
   行わない（フォーカス枠とFフォーカスだけ行う）。GUIタブ以外では選択中GUIの枠だけを描く。
5. Terrain ブラシ、Weld、Picker、Decal、通常選択の順にクリックを処理する。
6. 矩形選択、フォーカス枠、ギズモ、クリック候補の実形状外枠を更新・描画する。
7. 自由ドラッグとその Undo 記録、Tab ピボット、F フォーカスを処理する。
8. ImGuizmo ID と ImGui ウィンドウのスコープを終了する。

下位メソッドは ImGui の Begin/End や ImGuizmo の PushID/PopID を終了しない。
早期 return は担当機能だけを打ち切り、スコープの後始末は常に `onRender()` が行う。

## 固定解像度

`System.UseFixedResolution` が true（既定）なら、`BaseResolution` の縦横比を保つ黒帯付きの領域に
描画する。false なら黒帯を付けず、パネルの実サイズをそのまま描画解像度（FBO）にする。このとき
縦横比が `BaseResolution` と一致しないため、`Norm::Pixel` の画面GUIは縦横同率で拡大縮小する
（`ViewportLayout::uniformGuiScale`／`GameGuiRenderContext::uniformPixelScale`）。

## 画面GUIの編集

ツールバーのGUIタブ表示中（`GuiEditState::active`、Play中を除く）だけ、画面GUI
（WorldGui配下を除く `ScreenGuiObject`）をビューポートで直接操作できる。このとき3Dの
クリック操作（Terrainブラシ・Weld・選択・矩形選択・ギズモ・自由ドラッグ・Tabピボット）は止める。
GUIタブ以外では画面GUIはクリックを素通りし、全画面GUIがあっても3Dを編集できる。

- クリックで最前面（ZIndex最大）の要素を選択する。Ctrl+クリックで選択へ追加・解除する。
  GUIの無い場所のクリックは選択を解除する。
- `Locked` の要素はクリックを素通りし（背後の要素を選べる）、Explorerから選んでも
  ドラッグ・リサイズでは動かない。
- Shift を押している間だけ、プライマリ選択の要素に8つのリサイズハンドル（四隅と四辺の中点）を
  出す。ハンドルのドラッグで動かす辺だけを移動し、反対側の辺は固定する（Position と Size を
  まとめて Undo 1回分）。動かす辺も吸着の対象になる。Shift を離すとハンドルは消え、精密な移動の
  邪魔をしない（リサイズ中は Shift を離しても続く）。計算は `GuiSnap::resizeRect`。
- GUIタブ表示中（Play中を除く）は `User::setAltLookAllowed(false)` で Alt のフリールック
  （マウスロック）を止め、Alt を吸着の一時オフだけに使う。
- 選択済み要素を5px以上ドラッグすると、選択中のGUI全体の `Position` を動かす
  （Pixel は BaseResolution 比率、Scale はビューポート比率で逆算）。離した時点で
  `SetPropertyCommand` の `CompositeCommand` を Undo 1回分として記録する。
- ドラッグ中は選択全体のAABBの左/中央/右・上/中央/下を、他のGUI要素とビューポートの
  同じ線へ6px以内で吸着させ、ガイド線を描く（`GuiSnap`）。左辺⇔右辺でぴったり隣接し、
  そのまま滑らせると中央線で揃う。押し返す衝突はしない。吸着はGUIタブの「吸着」で
ON/OFFでき（`GuiEditState::snap`）、Alt押下中は一時的に無効になる。
- 選択中のGUIには要素の矩形ぴったりの枠（プライマリは太線）、ホバー中の要素には薄い枠を描く。
- 矩形計算は描画と共通の `ScreenGuiLayout`（`include/Core/ScreenGuiLayout.hpp`）を使う。

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
- ImGuizmoの係数差をワールド単位へ変換する加算Resize

結果は `Ray`、`ObbRayHit`、`ProjectedPoint`、`WorldAabb` の値型で返す。

### ViewportSceneQueries

`include/Editor/ViewportSceneQueries.hpp` / `src/Editor/ViewportSceneQueries.cpp`

`Workspace` / `Instance` の再帰走査を名前付き関数として提供する。

- 最近傍 BaseCube と Picker 対象（BaseCube／Attachment）の検索
- 最前面Cubeから最上位Modelへ昇格する通常選択／ホバー問い合わせ
- 矩形内の選択可能 Cube の収集
- Model 子孫のワールド AABB と、Cube／Model共通の移動境界計算
- 自由ドラッグとギズモ用の軸別／最小移動量による衝突フィット
- BaseCube／Model子孫の実形状ハイライト対象収集
- BaseCube の Locked 判定

通常選択の最近傍検索は Locked Cube もヒットとして返す。これにより最前面の Locked Cube が
背後の未ロック Cube を遮り、呼び出し側が通常クリック時の選択解除を適用できる。矩形選択では
Locked Cube 自身とその子孫を収集しない。

非Locked CubeがModelに属する場合、クリック単位はWorkspace直下に最も近い最上位Modelへ昇格する。
ただしレイの距離が先に評価されるため、手前の単独Cubeが奥のModelより優先され、Model AABB内の
空白だけでは選択されない。Select／Moveの表面ドラッグは5px閾値後に開始し、Modelは子孫Cubeの
ワールドAABBを使って配置・衝突フィットする。

Select／Move／Resize／Rotateの全ツールで、クリックによる選択は同じ規則に従う
（Move／Resize／Rotateではギズモのハンドル上の通常クリックだけはギズモ操作に任せる）。

- 修飾キーなしのクリックは、クリック対象だけを選択する。
- Ctrl+クリックはクリック対象を複数選択へ追加し、選択済みなら解除する。
- 複数選択中の対象を押した場合は、表面ドラッグに備えて複数選択を保つ。ドラッグせずに離したら、
  その1つだけの選択にする（`m_pendingSingleSelect`／`collapseToPendingSingleSelect`）。
- 何にもヒットしない空間、または最前面がLocked Cubeの位置を修飾キーなしでクリックすると、
  選択を全解除する。
- 表面ドラッグはSelect／Moveだけ、矩形選択はSelectだけで行う（ギズモ操作と競合するため）。

Editor選択・未選択候補ホバー・Weld候補ホバーは、Renderer所有の同一Selection Maskへ可視geometryを描画し、
screen-space outlineとして合成する。選択はcyan 2px、通常ホバーは白半透明1.5px、Weld候補は緑3pxであり、
mesh edgeの直接描画は使用しない。mask再描画時だけ小さなdepth offsetを使うため、同一表面の深度量子化差による
内部の穴やZ-fightingを防ぐ。Highlightインスタンス自身のFillColor設定には影響しない。

Resizeは初期Sizeへの倍率ではなく、単位スケール行列から得た係数差をワールド単位として加算する。
複数のBaseCubeを選択したResizeでは、Resize横の常設▼メニューからIndividual（同じstud差分を
各Cubeへ適用）とGroup Scale（集合AABB中心を固定ピボットにしたワールド軸スケール）を切り替える。
Group Scaleの倍率スナップは倍率の差分を指定刻みへ丸め、各Cubeの回転を維持する。
両モードともドラッグ開始時の集合AABB中心をResize中の固定ピボットとして使用する。
Individualでは各Cubeへ同じサイズ差分を適用し、反対面固定で位置を補正するため、
サイズ変更後に集合中心が変化してもImGuizmoの基準点は移動しない。
`User::gizmoSize`は既定0.20で、シーンではなく`editor_settings.yaml`へ保存される。Primaryカメラ入力は
キーボードズームをフォーカス、ホイールズームを画像ホバーで個別に許可する。

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

# CPU Rendering / Workspaceツリー走査 調査報告

調査日: 2026-09-21

## 結論

CastShadowの有効・無効でFPSが大きく変わらず、ProfilerでGPU Main Geometryが約0.20 ms、CPU Renderingが約11 msという観測が正しいなら、GPU描画そのものよりCPU側の描画準備が有力である。

ソース上、1フレームのビューポート描画でWorkspace全体を再帰走査する経路が複数存在する。特に影、メイン描画、SurfaceMark収集はそれぞれ独立して全ツリーを走査している。さらにEditorではGUIフォント準備、ScreenGui収集、WorldGuiホスト収集が同じフレームに追加される。

したがって「エンジンのツリー処理が問題」という仮説は、ソース構造上は十分成立する。ただし、現時点では各走査の実測時間が分離されていないため、11 msの全てがツリー走査だとはまだ断定できない。

## Profiler値の解釈

`render` はGPU時間ではなく、`Renderer::render()`が開始してから終了するまでのCPU区間である。Editorのシーン描画は `ViewportPanel::renderLayoutAndScene()` から `renderViewport()` を呼び、同じ `render` 区間内で実行される。

`main` もGPUタイマークエリの値ではなく、`renderViewport()`内のCPU計測区間である。したがって `main` のCPU値には、OpenGL draw callだけでなく、描画対象の判定、CFrame取得、uniform設定、個別描画前の準備処理が含まれる。

一方、ProfilerのGPU欄にある `gpuMain` はOpenGL timestamp queryによるGPU側の計測であり、CPU `main` とは別の値である。GPU Main Geometryが短くても、CPU描画準備が重い可能性は矛盾しない。

## 1フレーム内の主な全体走査

### `renderViewport()` 内

1. Lighting探索: `findLightingInTree()` がWorkspaceから再帰探索する。
2. インスタンス描画リスト作成: `collectInstCubes()` が全子孫を再帰し、各BaseCubeについて型判定、形状判定、ワールドCFrame取得、フラスタム判定を行う。
3. Shadow pass: cascadeごとに `shadowRender()` が全子孫を再帰する。現在は3 cascadeなので、インスタンス化できない対象については最大3回の全体走査になる。インスタンス化済み対象も、各cascadeの可視判定とdraw準備は行われる。
4. Main pass: `renderInst()` が全子孫を再帰する。インスタンス化済み対象を収集時に処理済みとしても、ツリー自体は再度辿る。
5. SurfaceMark収集: `collectSurface()` が全子孫を再帰し、SurfaceMarkと描画対象BaseCubeを別ベクターへ収集する。
6. Extras: Highlight、Constraint、Physics debug、Particle、Terrain、Weather、PostEffectなども、それぞれ内部でWorkspaceまたは子孫を探索する経路を持つ。

このため、単純なシーンでも「全ノード数に比例する再帰走査」が複数回発生する。子孫数が多いModel、Folder、GUI階層、Character階層では、実際のdraw call数が少なくてもCPU時間が増える構造である。

### GUI経路

Editorの `renderUI()` は毎フレーム `prepareGuiFonts(workspace)` を呼ぶ。この処理は `collectFontFileUsers()` でWorkspace全体を再帰し、ScreenGuiObjectを収集する。フォントキャッシュが有効でも、収集走査そのものは毎フレーム残る。

ViewportのGUI描画では `renderGameGui()` から次の処理が呼ばれる。

- `renderScreenGui()`: `collectScreenGui()` でWorkspace全体を再帰する。
- `renderWorldGui()`: `collectWorldGuiHosts()` でWorkspace全体を再帰し、BaseCubeを収集する。

つまりEditorの通常フレームでは、シーン描画に加えてGUIだけでも少なくともフォント収集、ScreenGui収集、WorldGuiホスト収集の全体走査が入る。

## SurfaceGui関連で確認できた重複

`renderWorldGui()` の `guiHosts` は毎フレーム作り直される。その後、各BaseCubeの直接子を複数回調べる。

- 全SurfaceGuiのベイク判定とベイク呼び出し。
- マウスクリック時の全SurfaceGui hit test。クリックしていないフレームはこの部分を通らない。
- BillboardGui / ProximityPrompt等のワールドGUI描画。

さらに、`Renderer::renderViewport()` の `collectInstCubes()` 内で `instanceableShapeIndex()` が各BaseCubeの直接子を確認する。SurfaceGuiが描画上書きを行う場合は `contributesBakedVisualOverride()` を呼び、その内部で `hasRenderableDirectChild()` がSurfaceGuiの直接子を走査する。

SurfaceGui自身も、ベイクが必要な場合に `computeRenderContentSignature()` を実行し、直接子を少なくとも二度走査する。したがってSurfaceGuiの数と各SurfaceGuiの子要素数が増えると、全Workspace走査とは別に、GUI子要素の反復コストも増える。

## 現時点で断定できること / できないこと

### 断定できること

- CPU `render` / `main` とGPU `gpuMain` は別の計測値である。
- `renderViewport()` はWorkspaceの同じ子孫を複数の目的で再帰走査する。
- EditorのGUI経路は、シーン描画とは別にWorkspace全体を再帰走査する。
- CastShadowを無効にしても、インスタンス収集、Main pass、SurfaceMark、GUI収集などのCPU処理は残る。

### まだ断定できないこと

- CPU 11 msのうち、再帰走査だけが何 msを占めるか。
- 3 cascadeのshadow走査が実際に支配的か。CastShadow無効時にshadow drawは減っても、`shadowRender()`のツリー走査自体は残るため、CPU差が小さい可能性はある。
- SurfaceGuiが主因か、Model/Folder/Characterを含む一般的なツリー走査が主因か。
- `getWorldCFrame()`、`IsA()`、`getFullPath()`、OpenGL API呼び出し、GUI描画のどれが走査中の主要コストか。

## 最も安い判別方法

実装を最適化せず、以下のカウンタを既存のFrameProfilerへ追加した。

- `treeLightingNodes`、`treeInstancesNodes`、`treeShadowNodes`、`treeMainNodes`、`treeSurfaceMarkNodes`: 各経路で訪問した非nullノード数。
- `treeGuiNodes`: GUI収集経路で訪問した非nullノード数。フォント、ScreenGui、WorldGuiホスト収集の合計。
- `baseCubesVisited` と `surfaceGuiChildrenVisited`。
- 各経路専用のCPU時間: `treeLighting`、`treeInstances`、`treeShadow`、`treeMain`、`treeSurfaceMarks`、`treeGui`、`treeGuiFonts`。

これらはProfilerの `Draw Counters` と `Tree Traversal` 表で確認できる。`treeShadow` はShadow pass全体、`treeGui` はGUI処理全体、`treeGuiFonts` はフォント準備全体のCPU時間であり、再帰呼び出しだけの時間ではない。`baseCubesVisited` は各走査経路での訪問を合算するため、同じBaseCubeが複数回数えられる。

CastShadowのON/OFF、SurfaceGuiあり/なし、同じシーンで各1分ずつ記録する。判定は次のとおり。

- CastShadow ON/OFFで `treeShadow` だけが大きく変わるなら、shadow passのCPU処理が候補。
- CastShadow ON/OFFでCPU `render`がほぼ変わらず、`treeShadow`の訪問ノード数も同じなら、shadow描画より全体走査の固定費が支配的。
- SurfaceGuiあり/なしで `treeGui`または`surfaceGuiChildrenVisited`とCPU `render`が同時に大きく変わるなら、SurfaceGui経路が候補。
- どの専用時間も小さく、`main`だけが大きい場合は、`getWorldCFrame()`、形状判定、個別GL状態設定、draw call発行の内訳を次に分ける。

## 次の最適化候補

計測で再帰走査が支配的と確認できた場合は、毎フレームの全ツリー探索をいきなり共通化するより、まずWorkspace側に変更時更新の描画レジストリを持たせるのが安全である。候補はBaseCube、SurfaceMark、LightSource、ParticleEmitter、ScreenGuiObject、BaseCube直下のWorldGuiである。

静的な登録リストを使えば、毎フレームの全子孫走査を候補リストの反復へ置き換えられる。ただし、親変更、clone、削除、Sceneロード、Stage/Commit、EditorのUndo/Redoで登録・解除を漏らさないことが前提になる。先にカウンタで支配経路を確定し、最も効果の大きい1経路だけを変更するべきである。

## 根拠ファイル

- `src/Core/Renderer.cpp`: `renderViewport()`、`collectInstCubes()`、`shadowRender()`、`renderInst()`、`collectSurface()`、`Renderer::render()`
- `src/Core/Renderer_GUI.cpp`: `prepareGuiFonts()`、`collectScreenGui()`、`collectWorldGuiHosts()`、`renderWorldGui()`、`renderGameGui()`
- `src/Editor/ViewportPanel.cpp`: Editorビューポートからの `renderViewport()` と `renderGameGui()` 呼び出し
- `src/Editor/EditorManager.cpp`: 毎フレームの `prepareGuiFonts()` 呼び出し
- `src/Editor/ProfilerPanel.cpp`: CPU `render` / `main` とGPU `gpuMain`の表示

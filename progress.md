# 開発進捗ログ

セッションごとの作業記録。新しいセッションを開始する際はまず一番下（最新）のセッションを読むこと。

---

## 2026-07-12 Canvasインスタンス実装 + PhysXレイキャストのヒット選択バグ修正 + GetMouseRay座標系修正

### 何をしたか

**1. Canvasインスタンス新規実装**（readme.md TODO消化）
- `include/Instances/Canvas.hpp` / `src/Instances/Canvas.cpp`（新規）: `Named<Canvas, Instance>`。プロパティ`Face`/`Width`/`Height`/`BackgroundColor`をPropertyRegistryに登録。`ensureBuffer()`（CPU RGBA8バッファ確保、GL非依存）、`ensureGPU()`（GLテクスチャ生成・アップロード、描画パスから毎フレーム呼ぶ）、`setPixel`/`getPixel`/`clear`（左上原点、内部は行反転してGL規約(行0=下端)に合わせる）、`worldToUV()`（親BaseCubeのCFrame逆変換で面判定しUVを返す）を実装。
- `src/Instances/Cube.cpp`: `draw()`の子収集ループにCanvas分岐を追加。SurfaceGuiと同じ`isSurfaceGui=1`ブレンド経路（`mix(ourColor, texColor, texColor.a)`）を再利用し、シェーダーは無変更。
- `src/Core/SceneLoader.cpp`: `createInstance`にCanvas追加、保存処理、`hasProps`条件にCanvas追加（実装エージェントが最初これを漏らし、プロパティが永久にYAML保存されないバグになりかけた）。
- `src/Core/LuauEngine.cpp` / `LuauEngine_Dispatch.cpp`: `SetPixel`/`GetPixel`/`Clear`/`WorldToUV`のLuauバインディング、`User:GetMouseRay()`（画面マウス位置→3Dレイ、45°FOV固定）、`workspace:Raycast`第4引数（除外Instance）、`User.Character`読取専用公開。
- `include/Core/User.hpp` / `src/Core/Renderer_GUI.cpp`: `renderGameGui()`（エディタ・ランタイム両方が通る唯一の合流点）でゲームビューポート矩形をUserに記録する仕組みを追加。
- エディタ統合（PropertiesPanel/SceneHierarchyPanel）、デモシーン`assets/scenes/canvas_test.yaml`+`scripts/CanvasPaint.luau`（Tool装備→マウスレイ→表面UVにペイント）を新規作成。

**2. PhysXレイキャストのヒット選択バグ修正**（`src/Core/Physics.cpp`）
- ユーザー実機テストで「レイの方向によって壁を素通りして奥のオブジェクトに当たる」現象を発見。原因は`PxRaycastBuffer`をtouchバッファ付きでフィルタコールバック無しで使うと、**全ヒットが`hasBlock=0`のtouchesに入り、しかもBVH走査順（方向依存の任意順）で距離順ではない**こと。旧コードは`touches[0]`を採用しており、たまたま近い順に並んだ時だけ正しく見えていた。
- 修正: block+touchesを全走査し、`ignoreActor`以外で最小距離のヒットを採用するよう変更。touchバッファも4→16に拡張。
- 詳細は memory: `physx-raycast-touch-order.md` を参照。

**3. Canvas.BackgroundColorのライブ反映バグ修正**
- 「BackgroundColorの変更が次回起動時まで反映されない」報告を受け調査。`ensureBuffer()`がバッファサイズ変更時にしか塗り直しをしていなかったのが原因。
- 修正: `m_bufferBg[4]`（直前に塗った背景色）を保持し、BackgroundColor変更を検知したら**旧背景色のままのピクセルだけ**新背景色に塗り替える（`SetPixel`で描いた内容は保持）方式に変更。

**4. `User:GetMouseRay()`のカメラドラッグ中対応 + 座標系バグ修正**
- ユーザー指示「マウスドラッグ中は仮想マウス位置をそのまま渡すようにしてください」を受け、`src/Core/LuauEngine.cpp`の`user_get_mouse_ray_closure`を修正。従来はカメラ回転ドラッグ中（右クリック/Altフリールック）は問答無用で`nil`を返していたが、`User::getRotationAnchor()`（ドラッグ開始時に固定されるアンカー座標＝画面に描画される擬似カーソルの位置）を使うように変更。
- 直後にユーザーから「仮想マウスの場合、マウス位置より若干上にレイキャストが通る」という追加報告。原因は座標系の変換漏れ：このアプリは`Renderer.cpp`で`ImGuiConfigFlags_ViewportsEnable`を常時有効にしており、`User::m_gameVpX/Y`（レイ計算で使うビューポート矩形）は**デスクトップ絶対座標**なのに対し、`getRotationAnchor()`が返す座標は**GLFWウィンドウのクライアント座標**で、ウィンドウの画面上の位置分だけズレていた。同じ問題は既に`Renderer_GUI.cpp`の`drawCameraRotationCursor`（擬似カーソル描画）側で`glfwGetWindowPos`を使って補正済みだったが、`user_get_mouse_ray_closure`側は同じ補正が抜けていた。
- 修正: `ImGui::GetMainViewport()->Pos`（マルチビューポート時はOSウィンドウの実位置と一致、非マルチビューポート時は(0,0)）をアンカー座標に加算。`glfwGetWindowPos`と同じ効果をGLFWwindowハンドルを持たないLuauEngine.cpp側から得るための選択。

### なぜそうしたか

- **CanvasはSurfaceGuiと同じisSurfaceGui描画経路を再利用**: 新規シェーダーパスを増やさず、既存の透過ブレンド・座標変換ロジックをそのまま流用できるため。Renderer.cpp本体・fragment.glslは無変更で済んだ。
- **UV向き（V下向き、行0=下端）は静的解析で確定してから実装**: `createCubeVertices`のUV割当てと`hitTestSurfaceGui`の規約を先に読み、実装前に「texV=1が面の上端」という結論を出してから`SetPixel`座標系との変換（V反転＋行反転の2箇所）を設計した。後から直すより先に確定させた方が安い設計判断だった。
- **PhysXのヒット選択は「距離で全走査」に統一**: フィルタコールバックで`eBLOCK`分類を作る方式も検討したが、既存呼び出し箇所への影響が大きいため、buf側で選び直す方式（呼び出し側は無変更）を採用。
- **GetMouseRayの座標系修正はImGui::GetMainViewport()->Pos採用**: `glfwGetWindowPos`と等価だが、`LuauEngine.cpp`はGLFWwindowへの参照を持っておらず、ImGui経由の方が対象関数のシグネチャ変更（GLFWwindow*追加）を避けられるため。既存の`drawCameraRotationCursor`と挙動を完全に一致させることを優先した。

### どういう経緯か

1. readme.md TODOのCanvas実装を計画モードで設計→ユーザー承認（ピクセル非永続化/GetMouseRay一発型/最小描画メソッド/Cube6面のみ）。
2. Phase1(本体+描画+エディタ)→Phase2(Luauバインディング)→Phase3(デモシーン)の順でimplementerに委譲、各Phase後にメインセッションが`git diff`で照合レビュー。
3. ユーザー実機テストで「BackgroundColor反映されず、ペイントもできない」と報告→ヘッドレステスト(RecubinTest)で描画パス・プロパティ・WorldToUVは正常と確認→ログ計装でテクスチャ生成自体は成功と判明→**Lighting.Direction**（壁の正面が無灯状態だった）と**CameraDistance**（保存されていた開発時カメラ距離259のせいで壁が画面の2%しかない）という**シーン設定側の問題**と判明、修正。
4. 「あたるときと当たらない時がある」と報告→当初はマウス/UI側の問題を疑ったが、詳細ログでレイの方向に応じて壁を貫通しBaseplateに当たるパターンを確認→**PhysX側の問題**と特定し、上記のtouches順序バグを発見・修正。
5. 「BackgroundColorが次回起動時にしか反映されない」報告→ensureBufferの塗り直し条件不足と特定・修正。
6. 「マウスドラッグ中は仮想マウス位置を渡してほしい」→GetMouseRayのnil分岐をアンカー座標使用に変更。
7. 「仮想マウスの場合、位置より若干上にレイが通る」→ウィンドウ位置オフセットの補正漏れと特定・修正。

**試して失敗した方法（教訓）**:
- 当初、レイの貫通バグを「マウス座標かレイキャストの計算式の問題」と決め打ちしそうになったが、方向依存で壁ごと素通りするという症状はUV/マウス座標の問題では説明がつかない。ログでヒットバッファの中身（`hasBlock`/`nbTouches`/各距離）を直接見るまで気づけなかった。**「貫通」報告を受けたら先にヒット選択ロジックを疑う**教訓としてmemoryに記録済み（`physx-raycast-touch-order.md`）。

### 未解決・保留

- 実機での最終確認待ち: GetMouseRayの座標系修正（ドラッグ中のレイのズレ解消）はビルドのみ確認済みで、ユーザーによる実機検証はまだ。
- `scripts/CanvasPaint.luau`に残る`[PAINT]`診断print（動作確認用の一時コード）は、ユーザー確認が完全に取れてから削除する約束がまだ果たされていない。次セッション冒頭で確認すること。
- `hitTestSurfaceGui`（Renderer_GUI.cpp）のFront/Back平面判定が実際の描画（createCubeVertices）と逆になっている**既知の未修正バグ**（Canvas.worldToUVは正しい向きに実装済みだが、hitTestSurfaceGui側は追随していない）。今回のスコープ外として意図的に温存。memory: `face-front-convention.md`参照。

### 暗黙仕様の発見

- **PhysXレイキャストのtouchバッファはBVH走査順であり距離順ではない**（spec.mdに記載なし）。フィルタコールバック無しで複数ヒットを扱う既存コード・将来コードは全て「距離で全走査」を前提にする必要がある。
- **マルチビューポート常時有効（`ImGuiConfigFlags_ViewportsEnable`）により、ImGuiのスクリーン座標は常にデスクトップ絶対座標である**（spec.mdに記載なし）。GLFW生座標（`glfwGetCursorPos`/`glfwGetWindowPos`が絡む値）とImGui座標を混在させるコードは、ウィンドウ位置オフセットの加算/減算を必ず意識する必要がある。今後同種のバグ（ImGui座標とGLFW生座標の混在）に注意。

---

## 2026-07-12 Highlightインスタンス新規実装 + 選択ハイライト統合 + 線描画のリボンジオメトリ化

### 何をしたか

**1. Highlightインスタンス新規実装**（readme.md TODO消化）
- `include/Instances/Highlight.hpp` / `src/Instances/Highlight.cpp`（新規）: `Named<Highlight, Instance>`。プロパティ`FillColor`/`OutlineColor`/`OutlineThickness`/`Enabled`をPropertyRegistryに登録。`ParticleEmitter`と同じ規約で自身は空間プロパティを持たず、親（`BaseCube`単体、または`Model`なら配下の全`BaseCube`を再帰対象化・入れ子Model含む）を対象にする。
- `include/Instances/BaseCube.hpp`と5派生クラス（Cube/Cylinder/Sphere/TriangularPrism/MeshCube）: `getHighlightVAO()`/`getHighlightIndexCount()`virtualを追加（ユーザー指示により、既存のIsA分岐チェーン複製ではなくvirtual経由に統一）。
- `src/Core/Renderer.cpp`: `Renderer::drawBaseCubeHighlight()`（塗り+輪郭の共有描画関数、深度テスト無効化）と`Renderer::renderInstanceHighlights()`（ワークスペース全体からHighlightインスタンスを収集して描画）を新規追加。`ViewportRenderDesc::renderInstanceHighlights`フラグ追加（デフォルトtrue、エディタ有無に関わらず常時描画）。
- 既存の選択ワイヤーフレーム（`Renderer.cpp`内、旧コメント「選択インスタンスの黄色ワイヤーフレームハイライト」）を`drawBaseCubeHighlight`共有ロジックにリファクタし、`Model`選択にも対応（配下の全`BaseCube`にアウトライン）。半透明の黄色塗りも追加。
- `src/Core/SceneLoader.cpp` / `src/Editor/SceneHierarchyPanel.cpp`（挿入メニュー） / `src/Editor/PropertiesPanel.cpp`（プロパティ表示）への配線。すべて`Canvas`の実装パターンを踏襲。

**2. 線描画のリボンジオメトリ化**（ユーザー報告「OutlineThicknessを変更しても太さが変わらない」への対応）
- 原因はOpenGL 4.1 Core Profile（本エンジンがWindows/main.cpp・game_main.cppで使用）の既知の制約：`glLineWidth()`は1.0px以外ほとんどのGPUドライバで無視される。`Rope`/`Rod`の`LineWidth`プロパティ、物理デバッグ、雷柱、地形ブラシガイドも同じ問題を抱えていたと判明。
- `include/Util/MeshEdges.hpp` / `src/Util/MeshEdges.cpp`（新規）: 三角形メッシュから「硬いエッジ」（本来の稜線、三角形分割の内部対角線を除く）を抽出するユーティリティ。頂点インデックスではなく量子化座標でエッジを正規化（面ごとに頂点が複製されている手続き型ジオメトリのため、インデックスベースだと境界判定が壊れる）。面法線は頂点属性ではなく三角形の実座標から幾何学的に計算。
- `Cube`/`Cylinder`/`TriangularPrism`/`MeshCube`はこのユーティリティで硬いエッジを一度だけ抽出・キャッシュ。`Sphere`は数値検証の結果ダイヒドラル抽出が機能しないと判明（後述）したため、3方向の直交する大円を解析的に生成する特別扱いにした。
- `Renderer::drawBaseCubeHighlight`の輪郭パスを、「1.02倍スケール+`glPolygonMode(GL_LINE)`+`glLineWidth`」から「硬いエッジをワールド座標変換→`buildSegmentRibbons()`で画面空間の常に一定ピクセル幅のリボン（三角形）に変換→`m_lineShader`で`GL_TRIANGLES`描画」に置き換え。
- `Rope`/`Rod`（`renderConstraints`）・物理デバッグ（`renderPhysicsDebug`）・雷柱（`renderLightning`）・地形ブラシガイド（`renderBrushMarker`）は`buildRibbonStrip()`（ワールド空間幅、連結ポリライン用）でリボン化。頂点生成ロジック自体（ベジェ曲線・円・弧・矢印）は無変更。

### なぜそうしたか

- **BaseCube仮想関数（getHighlightVAO等）を採用、IsA分岐チェーンの複製は却下**: メインパス・シャドウパス・選択ハイライトで同じ`IsA("Cylinder")/IsA("Sphere")/...`分岐が既に3箇所複製されていた。当初は「4箇所目の複製を避ける」目的で同パターンを踏襲する設計を提案したが、ユーザーから「virtualを追加したほうがシンプル」と指摘を受け、BaseCubeにvirtualを追加する方針に変更。結果的にコードが大幅に簡潔になった。
- **選択ハイライトの共有リファクタも同時に実施**: readme.mdのTODOに「エディターで選択したときに出てくるワイヤーフレームもこれに変更していく」と明記されていたため、Highlightインスタイン実装と同じセッションで一体的にリファクタした。副作用として選択ハイライトも深度無効化（壁越しに見える）になったが、これは意図した帰結としてユーザーに事前確認済み。
- **Sphereだけダイヒドラル抽出を使わない**: `SPH_RES=16`のcube-sphereで実際に角度計算をしたところ、6面継ぎ目の二面角（約3.2〜4.0°）より面内部の分割角（約4.1〜7.0°）の方が大きく、閾値でどちらかだけを残すことが原理的に不可能と判明（継ぎ目を拾う閾値では内部も全部残り、通常の15-20°閾値ではエッジが0本になる）。Blender/Unity等の球選択ギズモと同じ「3方向の直交大円」表現に変更した。
- **Highlight輪郭は画面空間幅、Rope/Rod等の物理線はワールド空間幅**: ユーザーとの相談の結果決定。Highlight/選択ハイライトはUI的な目印なので壁越し・遠距離でも視認性を優先し常にNpxに見えるようにし、Rope/Rod等の物理的な線は実際のケーブルのように距離に応じて細く見える方が自然、という判断。画面空間幅は頂点ごとの距離・FOV・ビューポート高さから逆算する必要があり実装コストが高いため、UI的目印だけに限定した。
- **`m_lineShader`を輪郭線描画にも流用**: 新規シェーダーを増やさず、既存のposition-only・flat color シェーダー（Rope/Rod等が既に使っている）をそのまま再利用できたため。`drawBaseCubeHighlight`内で塗りパス用のメインシェーダーと輪郭パス用の`m_lineShader`を都度切り替え、関数を抜ける前に必ずメインシェーダーへ戻すよう徹底した。

### どういう経緯か

1. readme.mdのTODO「Highlightインスタンスを追加」を計画モードで設計。Explore 3エージェント（Instance系/Rendering系/Editor統合）を並列起動して既存パターン（Canvas/ParticleEmitter/選択ハイライト）を調査→Plan agentで詳細設計→ユーザーに3点確認（対象範囲はBaseCube+Model両対応、選択ハイライトの共有リファクタも含める、プロパティ構成）→承認を得て実装。
2. 実装はPhase A（Highlightクラス+PropertyRegistry+SceneLoader配線+BaseCube系virtual）→Phase B（Renderer共有ヘルパー+新規パス+選択ハイライトリファクタ）→Phase C（エディター統合）の3フェーズでimplementerに委譲。各フェーズ後に`git diff`で照合レビュー。
3. **Phase A完了時、implementerが指示外の`src/Core/User.cpp`（RCBN_TRACEコメントアウト）と`src/Editor/ViewportPanel.cpp`（printfコメントアウト）を勝手に変更していたのを発見し、revertして対処**（教訓：フェーズごとのdiffレビューは`git status`での全体差分確認も必須。指定ファイル以外に変更がないか毎回チェックする）。
4. ビルド確認後、ユーザーから「OutlineThicknessを変更しても太さが変わらない。選択ハイライトも塗られるといい」と報告。選択ハイライトの塗りは1〜2行の軽微な変更としてメインセッションが直接修正（`kSelectionFillColor`のアルファを0→0.15）。
5. OutlineThicknessの件は調査の結果OpenGL Core Profileの`glLineWidth`制約と判明。ユーザーに「今回は現状維持か、リボンジオメトリへの作り直しか」を尋ねたところ「既存の線描画系を洗い出してリボンジオメトリ方式にリファクタしましょう」との指示を受け、再度計画モードへ。
6. Explore 1エージェントで全線描画箇所（`m_lineShader`系5箇所 + ソリッドメッシュワイヤーフレーム系1箇所）を調査した結果、構造が全く異なる2種類（Part A: 連結ポリライン、Part B: 独立エッジ群）と判明。ユーザーに「スコープは両方一気に」「Rope/Rod等はワールド空間幅、Highlight/選択ハイライトは画面空間幅」の2点を確認。
7. Part B（エッジ抽出+screen-space ribbon）はPlan agentで詳細設計を委託。**Plan agentが実際にPython で数値検証し、当初の想定（インデックスベースのエッジキー・Cylinderは閾値次第で縦ストラットが残る）が誤りだったことを発見**（正しくは座標ベースのキーが必要、Cylinderは正しい閾値で縦ストラットなしの綺麗な2円になる、Sphereはダイヒドラル抽出が原理的に機能しない）。この結果を踏まえて最終設計を確定。
8. 実装をPhase 1（MeshEdgesユーティリティ+BaseCube virtual+各形状のエッジキャッシュ）→Phase 2（drawBaseCubeHighlightの輪郭パス書き換え）→Phase 3（Part A: Rope/Rod/物理デバッグ/雷柱/ブラシマーカーのリボン化）の3フェーズでimplementerに委譲。
9. **Phase 1完了時も、implementerが指示外の`readme.md`にTODO項目を1行追記していたのを発見し、revertして対処**（3回目の同種インシデント。教訓としてmemoryに記録推奨）。
10. 各フェーズともビルド成功を確認し、最終的に`python build.py build`で全体ビルドを再確認して完了。

**試して失敗した方法（教訓）**:
- 当初Part Bのエッジ抽出は「インデックスベースでエッジをキー化し、閾値15-20°で二面角判定する」設計で進めようとしたが、Plan agentの数値検証で「手続き型ジオメトリは面ごとに頂点を複製しているため、インデックスベースのキーだと境界エッジが常に『隣接三角形1つ』と誤判定され、二面角判定が機能しない」ことが発覚。座標を量子化してキー化する方式に変更して解決。**手続き型メッシュ（面ごとに頂点複製）のエッジ隣接判定は、インデックスではなく座標ベースで行う**という教訓。
- Sphereも同様に「適切な閾値を選べばcube-seamだけ拾えるはず」という前提で進めようとしたが、実際に角度を計算すると継ぎ目（3.2-4.0°）より内部分割（4.1-7.0°）の方が角度が大きく、閾値では原理的に分離不可能と判明。**「たぶんこうだろう」で進めず、疑わしい箇所は実際に数値検証する**（CLAUDE.mdの方針通り）。

### 未解決・保留

- 実機での最終確認待ち: Highlight/選択ハイライトの輪郭がカメラ距離によらず一定ピクセル幅に見えること、Rope/Rodの`LineWidth`プロパティが実際に効くこと、Cube/Cylinder/Sphere/TriangularPrism/MeshCube各形状で輪郭が正しい形（内部対角線や過剰なケージ状の線が出ない）で表示されることは、ビルド確認のみでユーザー未検証。
- ワールド空間幅の定数（雷柱0.15/ブラシマーカー0.1/物理デバッグ0.05）とHighlightのデフォルト色（FillColor/OutlineColorともシアン系）は美的判断のデフォルト値。実機で見た目を確認後、調整が必要になる可能性が高い。
- `MeshCube`が滑らかシェーディングの組織的なGLBをインポートした場合、硬いエッジが疎または皆無になる可能性がある（真のシルエット追従ではないため、既知の限界として許容）。

### 暗黙仕様の発見

- **OpenGL 4.1 Core Profile（本エンジンがWindows含む全プラットフォームで使用）では`glLineWidth()`は1.0px以外ほとんどのGPUドライバで無視される**（spec.mdに記載なし）。既存の`Rope::LineWidth`/`Rod::LineWidth`のような「太さプロパティ」は実際には見た目に反映されておらず、この制約に気づかず新しい線描画コードを書くと同じ罠にはまる。太さを持たせたい線描画は全てリボン（三角形）ジオメトリ化が必要、というのが今後の標準パターンになった（`buildRibbonStrip`=ワールド空間幅、`buildSegmentRibbons`=画面空間幅、共に`Renderer.cpp`のstatic関数）。
- **手続き型ジオメトリ生成コード（Cube/Cylinder/Sphere/TriangularPrism）は面ごとに頂点を複製している**（法線/UVを面ごとに変えるため）。これは既存の描画には影響しないが、「エッジの隣接三角形を調べる」ようなメッシュ処理コードを新たに書く場合は、頂点インデックスではなく実座標（量子化）でエッジの同一性を判定しないと正しく動作しない。
- **`Cube::s_VAO`だけ他の4形状と異なり、`Cube.cpp`内に`initGeometry()`を持たず、`Renderer::init()`内で構築されている**（`Cylinder`/`Sphere`/`TriangularPrism`/`MeshCube`は自クラス内に`initGeometry()`を持つ）。Cube関連の初期化処理を追加する際はこの非対称性に注意が必要。

---

## 2026-07-13 ツールバー再設計（Basic/Cubes/Terrain/Physics/Character 2段化）

### 何をしたか

**1. ツールバーを2段構成に再設計**（readme.md「ツールバー拡張」TODO消化）
- `src/Editor/EditorManager.cpp` / `include/Editor/EditorManager.hpp`: `renderToolbar()`を「上段: Basic/Cubes/Terrain/Physics/Characterのタブ選択行(`renderToolbarTabs()`)」「下段: 選択中タブのアイコン+ラベルボタン群(`renderToolbarBasic/Cubes/Terrain/Physics/Character()`)」に全面書き換え。新規`enum class ToolbarCategory`と`m_toolbarCategory`メンバを追加。
- 旧「Add Objectドロップダウン」(`Cube/Cylinder/TriangularPrism/Truss/Seat/Sphere`のみ6種)を削除し、新規Cubesタブ(`Cube/Cylinder/TriangularPrism/Truss/Seat/Sphere/MeshCube/LiquidCube`の8種)に完全移行。
- 新規Physicsタブ: `Weld/Motor/Rod/Rope/Attachment/Force`の物理制約6種をアイコンボタンで追加可能に(親は選択中インスタンス優先、無ければWorkspace直下)。
- 新規Characterタブ: Humanoid追加ボタン(インスタンス選択時のみ`ImGui::BeginDisabled`で有効化)と、新規「リグビルダー」ボタン(デフォルトキャラクター一式をModelとして編集時に直接配置)。
- Terrainタブ: `PropertiesPanel.cpp`にあったブラシUI(Active/半径スライダー/Lower・Smooth・Raiseモード)をツールバーへ移設し、Comboを3つの独立トグルボタンに変更。Terrainインスタンスを選択しなくても常時操作可能になった(元々消費側の`ViewportPanel.cpp`のブラシ適用ロジックはworkspace内をスキャンして自動でTerrainを見つける設計だったため、UI移設のみで機能追加は不要だった)。

**2. リグ生成ロジックの共有化**（新規実装、readme.md「リグビルダーボタン」対応）
- `include/Core/CharacterRig.hpp` / `src/Core/CharacterRig.cpp`(新規): `CharacterRig::buildDefaultRigParts(parent)`として、Humanoid+Root/Head/Torso/LeftArm/RightArm/LeftLeg/RightLegの7パーツ生成ロジックを抽出。
- `src/Core/User.cpp`: 従来`createDefaultStarterCharacter()`内にハードコードされていた同じロジックを、このヘルパー呼び出しに短縮(戻り値型・シグネチャ・Play開始時フォールバック挙動は不変)。エディターのリグビルダーボタンとPlay時のStarterCharacterフォールバック生成が同じ実装を共有する形になった。

**3. 新規ツールバーボタン用ヘルパー2種**
- `EditorManager::tryAddObjectButton<T>`(新規テンプレート、`EditorManager.hpp`宣言/`EditorManager.cpp`定義): 旧`tryAddObject<T>`(`ImGui::MenuItem`版、親は`m_workspace`固定)を置き換え。`ImGui::Button`版、任意コンストラクタ引数、任意親(`std::shared_ptr<Instance>`)に対応。命名重複回避は`SceneHierarchyPanel::uniqueName`を再利用(自前whileループを再実装しなかった)。
- `EditorManager::drawIconButton`(新規、ユーザーからの追加依頼で実装): ラベル(アイコン+テキスト)がボタン幅/高さに収まらない場合、`ImGui::SetWindowFontScale`で自動的にフォントを縮小して収める(下限0.55倍)。実機確認で「キャラクター」「TriangularPrism」「リグビルダー」等の長いラベルが枠からはみ出る問題が見つかり、追加対応。全ボタン呼び出し(タブ行含む)をこのヘルパー経由に統一し、ボタンサイズも拡大(タブ90×28→110×30、コンテンツボタン64×56→78×58)。

**4. `SceneHierarchyPanel::requestNewScript`追加**
- `include/Editor/SceneHierarchyPanel.hpp` / `.cpp`: ツールバーの「New Script」ボタンから呼べるpublicメソッドを追加。中身は既存のInsert Objectメニューの「Script」項目クリック時と同じ処理(`m_pendingScriptParent`/`m_openScriptDialog`/`m_pendingScriptClass`設定)。`ScriptInsertClass::Script`固定(Local/ModuleはInsert Objectメニューのまま)。

**5. アイコン・ローカライズ**
- `include/Editor/IconsDef.hpp`: 空プレースホルダーだった`ICON_CUBE`/`ICON_MESHCUBE`/`ICON_SCRIPT`を実装し、新規24個のFont Awesome Solidグリフマクロを追加(Play/Pause/Stop/Select/Move/Resize/Rotate/Save/Load/各Cube系形状/各物理制約/Terrainブラシモード等)。
- `include/Editor/Localization.hpp` / `src/Editor/Localization.cpp`: `ToolbarTabBasic/Cubes/Terrain/Physics/Character`、`NewScriptButton`、`AddHumanoidButton`、`RigBuilderButton`の8キーを追加。Cube/Cylinder等の形状名やWeld/Motor等の制約名は、既存のSceneHierarchyPanelの慣習(クラス名は非Loc化)に倣いLoc化しなかった。

### なぜそうしたか

- **`tryAddObjectButton`を`SceneHierarchyPanel::tryInsertInstance`の設計に寄せた**: 旧`tryAddObject`は親が`m_workspace`固定で、Physics/Characterタブ(親=選択中インスタンス)には使えなかった。`SceneHierarchyPanel::tryInsertInstance`(任意親、perfect forwarding)の設計を踏襲しつつ、`ImGui::MenuItem`を`ImGui::Button`に置き換えることで、常時表示のツールバーボタンとして機能するようにした。
- **リグ生成ロジックを`CharacterRig`として抽出**: readme.mdの「デフォルトキャラクター」が明らかに`User.cpp`の`createDefaultStarterCharacter()`と同じリグを指しており、Play時フォールバックとリグビルダーボタンで数値・色・フラグ・命名を完全に一致させる必要があった。二重実装を避けるため、`starter->addChild`を`parent->addChild`に変えるだけの機械的な抽出とした(ロジック自体は一切変更していない)。
- **フォント自動縮小は`SetWindowFontScale`方式を採用**: ボタンサイズを大きくするだけでは「TriangularPrism」のような長い英字クラス名や日本語タブラベルを全て収めきれない。個別にラベル文言を短縮する案もあったが、クラス名(Cube/Cylinder等)は非Loc化の既存慣習と一致させたいため文言側を変えたくなく、代わりに表示側で自動調整する方式にした。縮小下限0.55倍を設けたのは、際限なく縮小すると可読性がゼロになるため(下限に達した場合はImGuiの標準クリッピングで見た目上は収まる)。
- **Terrainブラシの`mode`はintのまま(enum化しなかった)**: 元の`TerrainBrushState.mode`(-1/0/+1)は`TerrainStreamer::applyBrush(..., int mode)`という消費側シグネチャと直結しており、今回のスコープはUI移設(Combo→3トグルボタン)のみだったため、データ型の変更は行わなかった。

### どういう経緯か

1. ユーザーがreadme.mdの「ツールバー拡張」TODO(37-44行目)と手描きモックアップ2枚(Basic/Cubes/Terrain/Physics/Characterタブ+Physicsタブの中身、及びBasicタブの中身)を提示。計画モードでExplore 3エージェント(アイコン/font-awesome基盤、Terrainブラシ現状、Physics制約+Humanoid/リグビルダー現状)を並列起動して調査。
2. 調査の結果、スコープが「UI再構成」から「Character関係の新規ロジック(リグビルダー)」まで広いことが判明したため、AskUserQuestionで3点確認: (1)Characterタブを今回に含めるか→含める、(2)Physicsタブの制約は全6種か画像通り3種か→全6種、(3)旧Add Objectドロップダウンを削除するか→削除する。
3. Plan agentに詳細設計を委託(ファイル一覧・実装順序・ヘルパー設計・アイコンコードポイント表・Loc key一覧まで)。結果をレビューし、最終計画を`jazzy-meandering-kernighan.md`として作成、ExitPlanModeで承認を得た。
4. 実装をPhase1(アイコン+Loc追加)→Phase2(CharacterRig抽出+requestNewScript)→Phase3(EditorManager本体書き換え)→Phase4(PropertiesPanelのブラシUI削除)の4フェーズでimplementerに委譲。各フェーズ後に`git diff`で照合レビューし、指示外のファイル変更が無いことを確認。
5. Phase1の初回実行はAPIセッション上限で失敗(ファイルは無編集のまま終了)。ユーザーから「時間がたったので再試行して」と指示を受け、再実行して成功。
6. 全フェーズ完了後`python build.py build`でビルド成功を確認し、ユーザーに実機確認を依頼。
7. ユーザーが実機スクリーンショット3枚を提示、「キャラクター」「TriangularPrism」「リグビルダー」等のラベルがボタン幅からはみ出て途切れる問題を報告。「フォントサイズを自動調整しながら、ボタンの大きさも再検討したい」との要望を受け、`drawIconButton`ヘルパーを追加設計し実装・ビルド確認。

**試して失敗した方法（教訓）**:
- 特になし。ただしPhase1のAPIセッション上限による初回失敗は、`git status --porcelain`で対象ファイルが未変更であることを確認してから再実行する、という手順を踏んだ(ファイルが部分的に書き換わった状態で再実行して壊れることを避けるため)。

### 未解決・保留

- 実機での最終確認待ち: `drawIconButton`のフォント自動縮小適用後の見た目(特に「TriangularPrism」は縮小下限0.55倍近くまで縮む可能性が高く、読みにくい場合はラベル短縮などの追加対応が必要になるかもしれない)。
- アイコングリフの選定(`ICON_CYLINDER`=database代替、`ICON_TRIANGULARPRISM`=shapes代替、`ICON_TRUSS`=grip-lines-vertical代替、`ICON_ROD`=grip-lines代替、`ICON_ROPE`=wave-square代替)は、直訳できる専用グリフが無いため近い意味の代替グリフを割り当てた暫定案。実機で見た目が不適切なら`IconsDef.hpp`の該当1行を差し替える想定。
- Basicタブの「New Cube」ボタンの親は`m_workspace`固定(Cubesタブ本体と同じ挙動)とした。選択中インスタンスの子として追加する案もあり得たが、今回は確認せず`m_workspace`固定と決め打ちした。
- スナップ設定(Move/Rotate/Resize Snap・Collision Fit)はBasicタブにそのまま残置し、アイコン化・タブ移動は行っていない(readme.mdのタブ定義に対応が無いため意図的にスコープ外とした)。

### 暗黙仕様の発見

- **`TerrainBrushState`(radius/mode/active)は`EditorManager`が実体を持ち、`PropertiesPanel`/`ViewportPanel`はポインタとしてのみ共有する設計**（spec.mdに記載なし）。UIの描画元をどのパネルに移しても、ポインタ配線を変える必要がなく、消費側(`ViewportPanel.cpp`のブラシ適用処理)はworkspace走査で自動的にTerrainインスタンスを見つけるため選択状態にも依存しない。同種の「エディター全体で共有したい一時状態」を追加する際は、この`EditorManager`所有+ポインタ配線パターンが既定の流儀になっている。
- **`SceneHierarchyPanel::tryInsertInstance`と`EditorManager::tryAddObject`(旧)は似て非なる設計だった**: 前者は任意親+perfect forwarding、後者は`m_workspace`固定+`(Pos,Size,...)`固定シグネチャ。両者を統合した汎用ヘルパーは今回まで存在しなかった(`spec.md`に記載なし、コードを読んで初めて判明した設計の非対称性)。

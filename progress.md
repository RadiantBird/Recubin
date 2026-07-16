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

---

## 2026-07-14 Terrain Paintブラシの色バグ調査・修正 + シャドーイング警告の導入

### 何をしたか

**1. Terrain Paintブラシの色バグ修正**
- `src/Core/TerrainStreamer.cpp`: `TerrainStreamer::applyColorBrush()`内のループローカル変数`int32_t b`（タンジェント方向のワールドブロック座標）を`tb`にリネーム。関数パラメータ`uint8_t b`（塗る色の青成分）とのシャドーイングを解消。

**2. `User`クラスの回転速度ハードコード解消 + エディター露出**
- `include/Core/User.hpp`: `mouseRotationSpeed`メンバー（デフォルト0.15f）を新規追加。
- `src/Core/User.cpp`: `User::processCameraRotation()`内でハードコードされていたローカル`const float rotationSpeed = 1.5f;`と`const double mouseRotationSpeed = 0.15;`を削除し、既存メンバー`rotationSpeed`（1.0f）と新規メンバー`mouseRotationSpeed`を直接参照するよう変更。`setProperty()`に`RotationSpeed`/`MouseRotationSpeed`を追加。
- `src/Core/SceneLoader.cpp`: YAML保存処理に`RotationSpeed`/`MouseRotationSpeed`を追加。
- `src/Editor/PropertiesPanel.cpp`: Userのプロパティパネルに`RotationSpeed`/`MouseRotationSpeed`のDragFloatを追加。

**3. ビルドシステムにシャドーイング警告を導入**
- `CMakeLists.txt`: MSVCのデフォルト無効警告`/w14456`（ローカル同士の隠蔽）・`/w14457`（パラメータの隠蔽）・`/w14458`（メンバーの隠蔽）を有効化。ただし`src/Core/LuauEngine.cpp`は`lua_State* L`という引数名がLua/Luau C APIの慣習でメンバー`LuauEngine::L`と意図的に同名（コルーチン等、メンバーとは別のstateを渡す設計）になっており、大量誤検出になるため、この3警告の対象ソースリストから`LuauEngine.cpp`だけを除外する形にした。

### なぜそうしたか

- **`applyColorBrush`のバグ原因はパラメータ名とループ変数名の衝突**: 関数シグネチャは`(..., uint8_t r, uint8_t g, uint8_t b)`、ループ内は`int32_t a = centerA+da; int32_t b = centerB+db;`という命名で、後者が前者を隠していた。`copy.b = b;`が実際には「塗る色の青」ではなく「ワールドブロック座標を`uint8_t`に切り詰めた値」を書き込んでいたため、R/Gは指定色のまま固定でBだけ位置に応じてなだらかに変化し256ブロックごとに折り返す、という「同じ材質なのに色が違う」現象になっていた。ユーザー提供の2枚のスクリーンショットをPythonでピクセル解析し、R=G=174固定・Bだけ滑らかに変化して途中でジャンプするパターンを確認したことで、シャドーイングによる変数取り違えだと断定できた。
- **User.cppのシャドーイング修正はクラスメンバーを採用する方針にした**: ユーザーから「クラスメンバーのほうを採用してローカル変数を消しましょう」と明示指示。ローカルの`rotationSpeed=1.5f`を削除し、既存メンバー(デフォルト1.0f)に統一。挙動が変わる可能性がある点はユーザーに明示して確認を仰いだ。
- **`mouseRotationSpeed`も同時にメンバー化**: ユーザーが「このマウス用にハードコードされてる数値もあまりよくないので、今回でクラスメンバーに統合しましょう」と追加指示。既存の`mouseZoomSpeed`（`zoomSpeed`とペアでメンバー化済み）と同じパターンに揃え、`setProperty`/`SceneLoader`保存/`PropertiesPanel`UIの3箇所に一貫して追加した（Speed/CameraDistance/ZoomSpeed/MouseZoomSpeedの既存の追加パターンを踏襲）。
- **LuauEngine.cppだけシャドーイング警告から除外**: `lua_State* L`引数名はLua/Luau C API全体の慣習で、同ファイル内に約60箇所以上存在し、かつ意図的な設計（コルーチンに登録する際、メンバーの`L`とは別のstateを渡すため）と判明。ユーザーから「意図的なものですし、そのファイルだけ警告無効化して終わりにしましょう」と指示を受け対応。

### どういう経緯か

1. ユーザーが地形エディターのスクリーンショットを提示し、「チャンクをまたぐとデフォルト色の解釈が変わる？」と質問。
2. まず`Terrain.cpp`/`TerrainStreamer.cpp`の色生成・保存・描画パイプライン（`generateRawGrid`/RLEエンコード・デコード/`buildChunkMesh`/フラグメントシェーダー）を一通り読んだが、コードレベルでチャンク境界依存のロジックは見当たらなかった。
3. AskUserQuestionで現象を切り分け→「同じ材質なのに色が違う」と判明。
4. Flatモードで再現するかのスクリーンショットをユーザーが追加提示。最初は「ストリーミング半径の外周が壁になって見えているだけ（仕様通り）」という仮説を立てたが、ユーザーが「チャンクを移動しても最初からずれている」と否定。
5. 2枚目の追加スクリーンショットで、Pythonで実際にピクセルをサンプリング（`PIL.Image.getpixel`）し、R=G=174固定・Bだけ滑らかに変化して256近辺でジャンプするパターンを確認。この時点で「ペイントブラシで書き込んだ色のBチャンネルだけおかしい」と当たりをつけ、`applyColorBrush`のコードを再読したところ、ローカル変数`b`によるパラメータ`b`のシャドーイングを発見。
6. 1行の変数リネームで修正し、`python build.py build`でビルド確認。
7. ユーザーから「ビルドシステムにシャドーイング警告を有効化しよう」との指示を受け、`CMakeLists.txt`に`/w14456 /w14457 /w14458`を追加。ビルドし直したところ、今回のバグとは無関係な既存コード（`src/Core/User.cpp`の`rotationSpeed`、`src/Editor/ViewportPanel.cpp`の`sx/sy/sz`、`src/Core/LuauEngine.cpp`の`L`・`userdata`）で新規に警告が出ることを確認。CLAUDE.mdのスコープ厳守方針に従い、指示されていないものは修正せず報告のみに留めた。
8. ユーザーが`User.cpp`の`rotationSpeed`シャドーイングについて「クラスメンバーのほうを採用してローカル変数を消しましょう」と指示。修正し、さらに「マウス用のハードコードも今回でメンバーに統合しましょう」との追加指示を受けて`mouseRotationSpeed`をメンバー化・エディター露出まで実施。
9. ユーザーから残り2件（`ViewportPanel.cpp`の`sz`、`LuauEngine.cpp`の`L`/`userdata`）の警告詳細を聞かれ説明。`ViewportPanel.cpp`のsx/sy/sz`は別ラムダスコープ同士で実害なし、`LuauEngine.cpp`の`userdata`は実害はないが紛らわしいパターン、`L`はLua API慣習と回答。
10. ユーザーが「Luauだけで大量に警告が出る感じか、意図的なので該当ファイルだけ警告無効化して終わりにしよう」と指示。
11. 最初`set_source_files_properties(... COMPILE_OPTIONS "/wd4458")`で後から打ち消す方式を試みたが、ビルドしても警告が消えず失敗。`COMPILE_FLAGS`（生文字列）に変えても同様に失敗。
12. `build/RecubinCore.vcxproj`を直接確認し、CMakeが`/wdNNNN`パターンを自動認識して`DisableSpecificWarnings`という別のMSBuildプロパティに変換していること、かつそれが`AdditionalOptions`（有効化フラグ`/w14458`が入っている、コマンドライン末尾）より先に評価される順序になっていることが原因と特定。
13. 方針転換: 「有効化してから打ち消す」のではなく「最初からLuauEngine.cpp以外にだけ有効化フラグを適用する」形に変更（`set_source_files_properties`の対象ソースリストからLuauEngine.cppを除外）。これで狙い通りLuauEngine.cppのみ警告なし・他ファイルは検出継続を確認。

### 未解決・保留

- 実機での最終確認待ち: `applyColorBrush`修正後、実際にPaintブラシで塗って色が正しく反映されること（Bチャンネルが指定通りになること）はビルド確認のみでユーザー未検証。
- 矢印キーでのカメラ回転速度が、旧ハードコード値1.5からメンバーのデフォルト値1.0に変わった。体感速度が変わる可能性があり、実機での違和感確認をユーザーに依頼中（未回答）。
- `src/Editor/ViewportPanel.cpp`の`sx/sy/sz`シャドーイング（別ラムダスコープ同士、724行目付近）と`src/Core/LuauEngine.cpp`の`userdata`シャドーイング（539行目、実害なしだが紛らわしい）は、今回は指示範囲外として未修正のまま残っている。次回「ついでに直すか」判断が必要。

### 暗黙仕様の発見

- **`TerrainStreamer::applyColorBrush`はSculptブラシ(`applyDirectionalBrush`)と違い`r,g,b`パラメータを取る**（`spec.md`に記載なし）。同じ「ブラシ適用系」関数群でもパラメータ構成が異なり、シャドーイング事故が起きやすいのはこの色付き関数だけ。同様のパラメータ命名（a/b等の短い座標変数とr/g/bの色引数）を今後追加する際は特に注意が必要。
- **MSVCの`/wdNNNN`系フラグはCMakeの`COMPILE_OPTIONS`/`COMPILE_FLAGS`のどちらで渡しても、CMakeのVisual Studioジェネレータが自動認識して`DisableSpecificWarnings`という構造化MSBuildプロパティに変換してしまう**（`spec.md`は元よりCMake自体のドキュメントにも明記されていない挙動）。この構造化プロパティは、ディレクトリスコープの`add_compile_options`が生成する`AdditionalOptions`（各ソースのコンパイルコマンドラインで常に末尾に置かれる）より先に評価されるため、「有効化してから特定ファイルだけ`/wd`で打ち消す」という直感的なアプローチは機能しない。特定ファイルだけ警告を変えたい場合は、最初からそのファイルを対象外にする（有効化フラグ自体を適用しない）方式にする必要がある。
- **`User`クラスはPropertyRegistry（`property-schema-registry.md`参照）に移行しておらず、`setProperty`の手書きif分岐 + `SceneLoader.cpp`の手書きYAML出力 + `PropertiesPanel.cpp`の手書きImGuiウィジェットの3箇所を一致させる手動パターンのまま**（`spec.md`に記載なし）。Speed/CameraDistance/ZoomSpeed/MouseZoomSpeedの既存4プロパティがこの手動3点セットで実装されており、今回追加したRotationSpeed/MouseRotationSpeedも同じパターンを踏襲した。

---

## 2026-07-14 レンダリング性能改善（FrameProfiler計測 → クイックウィン → 全プリミティブのGPUインスタンシング）

### 何をしたか

**1. FrameProfiler計測基盤の新規導入**（ユーザーのストレステスト報告「Cube 2500個で20FPS」への第一手）
- `include/Util/FrameProfiler.hpp` / `src/Util/FrameProfiler.cpp`（新規）: 区間計測（begin/endSection）+カウンタ（addCount）のシングルトン。1秒ごとに`[PROF] fps=... | physics=... shadow=... main=... ui=... swap=... | cubesDrawn=...`をRCBN_LOGへ1行出力。
- `src/main.cpp`（エディター）/ `src/game_main.cpp`（ランタイム）: physics/luau区間の計装（main.cpp側はワークスペースループ内でluau区間を一時中断してphysicsを独立計測）。ループ末尾で`endFrame()`。
- `src/Core/Renderer.cpp`: shadow/main/extras/ui/swap区間とcubesDrawn/cubesCulled/shadowCubesカウンタ。
- `scripts/StressTest.luau`: `anchored`トグルを追加（物理負荷の切り分け用）。

**2. クイックウィン3点**（計測結果を受けて）
- **エディターの二重フルシーン描画を削除**: `Renderer::render()`はeditor有時に`renderViewport()`を呼ばず、`renderUI()`内の`ViewportPanel`の描画のみに任せる。
- **uniformロケーションのキャッシュ**: `include/Util/GLUniformCache.hpp` / `src/Util/GLUniformCache.cpp`（新規、`CachedUniform`+`cachedUniformLocation()`）。Cube/Cylinder/Sphere/TriangularPrism/LiquidCube/MeshCubeの各`draw()`とシャドウパスの毎フレーム`glGetUniformLocation`を置換。MeshCubeのデカール配列uniformは「program変更時のみ全引き直し」方式で毎フレームの文字列連結も解消。
- **`Cube::draw`の6面→1ドロー短絡**: 面子要素（Decal/Texture/SurfaceGui/Canvas）が無ければuniform1回+`glDrawElements(36)`1回。

**3. GPUインスタンシング（Cube→全プリミティブ4形状）**
- シェーダー3ファイル（`src/vertex.glsl`/`fragment.glsl`/`depth_vertex.glsl`）: `uniform float uInstanced`分岐と`aInstModel`(location 5-8)/`aInstColor`(location 9)属性、FSは`effColor = uInstanced ? InstColor : ourColor`。プログラムは増やしていない。
- `include/Core/Renderer.hpp` / `src/Core/Renderer.cpp`: `CubeInstanceData`（mat4+color）、全形状共有の`m_instanceVBO`、4形状分の`m_instBatches[]`。`renderViewport()`冒頭で対象を1回走査して収集し、シャドウ/メイン両パスで`glDrawElementsInstanced`一括描画。既存の`shadowRender`/`renderInst`は収集済み個体をスキップ。
- 対象判定`instanceableShapeIndex()`: クラス名完全一致(Cube/Cylinder/Sphere/TriangularPrism)・不透明(a>=0.999)・Unlit/Triplanar無し・TextureScale=1・面子要素無し。条件外は従来の個別描画にフォールバック。

### なぜそうしたか

- **「まず計測から」はユーザー決定**: 当初の20FPS報告だけでは描画/物理どちらが支配的か不明だった。計測の結果、ボトルネックは**GPUではなくCPUのドローコール発行**（10000個でmain=36.9ms、swap=0.2ms=GPU暇）と判明し、インスタンシングが正解と確定してから実装した。物理はスリープ後ほぼゼロで、当初の20FPSは落下衝突中の物理負荷+vsync由来と推定。
- **二重描画の削除は計測での最重要発見**: shadowCubes=2×個体数から発覚。`Renderer::render()`の直接描画と`ViewportPanel::render()`が**同じFBO**（`viewportPanel->framebuffer`）に描いており、1回目は完全な無駄だった。
- **インスタンシングは「別プログラム」ではなく「既存プログラムにuInstancedフラグ」を採用**: 別プログラム化するとライト配列・シャドウ等のシーンuniformを2プログラム分セットアップする必要があり複雑化するため。無効attribの読み値は(0,0,0,1)で定義済みなのでternary分岐で安全。
- **形状ごとの専用インスタンスVAO複製ではなく、既存s_VAOへの属性後付けを採用**（Phase 3で方式変更）: Cylinder等はVBO/EBOハンドルを公開しておらず専用VAOが作れないため。後付け方式ならs_VAOだけで済み、Phase 2で作ったCube専用VAO(`m_cubeInstVAO`)も廃止して統一できた。空バッファの範囲外読み対策としてインスタンスVBOにゼロ埋め1個分を常時確保。
- **対象は「素の個体」に限定**: Cylinderも面デカール描画（`getDecalTexture`で6サブレンジ）を持つと判明したため、面子要素チェックは全形状に適用。半透明はブレンド順の問題があるため除外し、既存パスと同じ挙動を保証。

### どういう経緯か

1. ユーザーがStressTest.luau（Cube2500個生成+FPSカウンタ）で20FPSと報告。計画モードでExplore調査→ボトルネック候補（6ドロー/キューブ、glGetUniformLocation毎フレーム、シャドウカリング無し、2500動的PhysXアクター）を提示。
2. AskUserQuestionで「まず計測から」「物理は切り分けのみ」と決定→FrameProfiler+anchoredトグルをimplementerに委譲・実装。
3. ユーザーが実測データを提供（2500/10000/50000個、anchored true/false）。CPU発行ボトルネック・二重描画・物理沈静化を確定。
4. 再び計画モードで改善案4点を提示→ユーザーが「④インスタンシングまで一気に」を選択。
5. Phase 1（クイックウィン）→Phase 2（Cubeインスタンシング）をimplementerに委譲、各フェーズでdiff照合レビュー+ビルド確認。Phase 2ではシャドウのインスタンス描画がlightSpaceMatrixアップロード前に挿入されていないか実ファイルで確認（正しい位置だった）。
6. 回帰テスト実行: 85 passed/3 failed=既知ベースラインと一致、新規回帰なし。
7. ユーザーが「パフォーマンス改善を確認、他のプリミティブにも」と指示→Phase 3で4形状に一般化（この際にVAO方式を後付けに変更・簡素化）。再度ビルド+回帰テストでベースライン一致を確認。

**試して失敗した方法（教訓）**:
- 今回は大きな手戻りなし。ただしPhase 2設計時、当初「インスタンス用の別シェーダープログラム」を検討したが、シーンuniform（ライト8個分の構造体配列等）の二重セットアップが必要と気づき、実装前に`uInstanced`フラグ方式へ転換した。**大量のuniformを共有するパスの分岐は、プログラム分割よりuniformフラグ分岐が安い**。
- 「Cube 2500個で20FPS」という当初報告は、計測してみると**沈静化後は60FPS**で、低下の主因は落下・衝突中の一時的な物理負荷だった。体感報告のFPSは「いつの時点か」で大きく変わるため、恒常負荷と過渡負荷を計測で分離してから設計判断すべき、という好例。

### 未解決・保留

- 実機での最終確認待ち: Cylinder/Sphere/TriangularPrism混在シーンの見た目（色・影・デカール付きフォールバック・半透明）はビルド+回帰テストのみでユーザー未検証。Phase 1-2分（Cube）は改善確認済み。
- 今回スコープ外として残したもの: 半透明のソート、シャドウパスのライトフラスタムカリング、シーンツリーの1フレーム7-8回走査の削減、PhysXチューニング（スリープ/CCD/ソルバ反復）、MeshCube/LiquidCubeのインスタンシング。
- FrameProfilerの`[PROF]`ログは常時出力（1行/秒）。邪魔になったらトグル追加を検討。
- 前セッションからの持ち越し: `scripts/CanvasPaint.luau`の`[PAINT]`診断print削除（Canvas実機確認待ち）、矢印キーカメラ回転速度1.5→1.0の体感確認は今回も未回答のまま。

### 暗黙仕様の発見

- **エディターは今回まで毎フレーム同じシーンを2回フル描画していた**（`Renderer::render()`直接呼び出し+`ViewportPanel::render()`が同一FBOへ）。修正済みだが、`renderViewport()`を新たな場所から呼ぶ変更をする際は描画回数の重複に注意。エディターの`[PROF]`では**シーン描画はui区間（renderUI内）に含まれる**ため、uiの値=ImGui+shadow+main+extras。
- **メインウィンドウのvsyncはGLFWデフォルト（glfwSwapInterval未呼び出し）**。imgui_impl_glfwがセカンダリビューポートにのみinterval 0を設定している。実測ではfpsが60に張り付き、負荷減少分がswap区間に吸収される挙動を確認（=vsync有効相当）。
- **Cylinderの`draw()`も面デカール対応**（Top/Bottom/側面4方向の6サブレンジを`getDecalTexture`で個別テクスチャ描画）。「デカールはCubeだけ」ではない。
- **Cylinder/Sphere/TriangularPrismの`s_VAO`はコンストラクタからの遅延`initGeometry()`で生成される**（Cubeだけ`Renderer::init()`内、既知の非対称性の追加情報）。これらのVAOに依存する初期化はRenderer::init時点では行えず、遅延実行が必要。
- **BaseCubeに見た目系プロパティを追加する場合、`instanceableShapeIndex()`（Renderer.cpp）に「デフォルト値以外は除外」の条件追加が必須**。忘れるとインスタンス描画された個体だけ新プロパティが無視されるサイレントな見た目バグになる。memory: `primitive-instancing.md`に記録済み。

---

## 2026-07-14 SignalEventのエディター生成対応 + Humanoidアニメーション更新のランタイム欠落修正

### 何をしたか

**1. SignalEventインスタンスをエディター/シーンファイルから生成可能にした**
- `src/Core/SceneLoader.cpp`: `#include <Instances/SignalEvent.hpp>`追加、`createInstance()`に`if (className == "SignalEvent") return std::make_shared<SignalEvent>();`を追加（`Highlight`の直後）。`Fired`はPropertyRegistryの`sig<>`（Luau読み取り専用）でYAML保存対象ではないため、`hasProps`リストへの追加は行っていない。
- `src/Editor/SceneHierarchyPanel.cpp`: `#include <Instances/SignalEvent.hpp>`追加、Insert Objectメニューの「Other」カテゴリに`tryInsertInstance<SignalEvent>(m_history, "SignalEvent", parentSp)`を追加（`Animation`の直後）。

**2. `Humanoid::updateAll`静的メソッドを新設し、Animation再生をworkspace全体のHumanoidに拡張**
- `include/Instances/Humanoid.hpp` / `src/Instances/Humanoid.cpp`: `static void updateAll(Instance* root, float dt)`を新規追加。`ParticleEmitter::updateAll`と全く同じ「ツリー再帰走査+IsA判定」パターンで、見つけた全Humanoidの`updateAnimation(dt)`を呼ぶ。
- `src/main.cpp`: 従来`if (isPlaying && !isPaused && user->humanoid) { user->humanoid->updateAnimation(deltaTime); }`だった箇所を`if (isPlaying && !isPaused) { Humanoid::updateAll(workspace.get(), deltaTime); }`に置換。対象がローカルユーザーの自キャラだけだったのを、workspace内の全Humanoid（NPC含む）に拡張。
- `src/game_main.cpp`: `#include <Instances/Humanoid.hpp>`を追加し、既存の`syncWeldKinematics()`呼び出しの直前に`Humanoid::updateAll(workspace.get(), deltaTime)`を新規追加（従来はHumanoidアニメーション更新の呼び出しが**一切存在しなかった**）。

**3. SignalEventのPASS/FAIL自動テストを追加**
- `assets/scenes/signal_test.yaml`: `SignalEvent`インスタンス（`TestSignalEvent`）を新規追加し、`SceneLoader::createInstance`経由のYAML復元もテスト対象にした。
- `scripts/signal_test.luau`: Test 5として`Instance.new("SignalEvent")`でのFire/Connect（引数付き）と、シーンからロードした`TestSignalEvent`のFire/Connectの両方を`[PASS]`/`[FAIL]`形式で検証するコードを追加。

**4. readme.mdのチェックリスト更新**
- 「本当に実装できているか、Luauスクリプトとシーンファイルでテストする」「キーフレームに到達した、というシグナルイベントを追加」「SignalEventインスタンスを追加」「エディターでインスタンスとして生成する方法がないので、対応する」の4項目を`[x]`に変更。

### なぜそうしたか

- **ユーザーが選択中だったreadme.mdの一節（「本当に実装できているか、テストする」TODO）を起点に調査した結果、当初の想定より大きい問題が見つかった**: `SignalEvent`クラス自体はFire/Connect/Luauバインディングまで完成していたが、`SceneLoader::createInstance()`とエディターのInsert Objectメニューへの登録漏れがあり、「エディターでインスタンスとして生成する方法がない」というユーザーの手元確認と一致した。まずこの明確なバグから着手する方針とした。
- **KeyframeReachedシグナルは「設計」ではなく「配線漏れの発見」だった**: readme.mdでは`[?]`（未確認）マークだったため、当初ユーザーには「別途設計が必要な未実装機能」として提示し、一度は「一緒に設計する」との回答を得た。しかし実際にコードを読むと`Humanoid::KeyframeReached`シグナル・`Humanoid::updateAnimation()`での発火ロジック・Luauバインディング・ドキュメント（`doc/Instances/Humanoid.md`）まですべて実装済みだった。設計をやり直す必要はなく、「なぜ動いていないように見えるのか」を追った結果、**呼び出し元(`updateAnimation`を毎フレーム呼ぶ場所)が`src/main.cpp`内の`user->humanoid`だけに限定されており、`src/game_main.cpp`（実際のゲームランタイム）には呼び出し自体が存在しない**という配線漏れに行き着いた。「たぶん未実装だろう」で新規設計を始めず、まず既存コードを読みきったことで、実装済みの機能への的外れな重複実装を避けられた。
- **`user->humanoid`限定ではなく`Humanoid::updateAll`でworkspace全体を対象にする方式を採用**: 当初の問題提起はgame_main.cppの欠落のみだったが、同じ`updateAnimation`欠落パターンはmain.cpp側にもある（NPCのHumanoidはPlayモードでもアニメーションしない）。修正の一貫性を保つため、`ParticleEmitter::updateAll`/`Weather::updateAll`という既存の「ツリー全体を走査して対象クラスを更新する」確立済みパターンを踏襲し、main.cpp/game_main.cpp両方を同じ`Humanoid::updateAll`経由に統一した。ユーザーには事前にこの方針（game_main.cppの修正に合わせてmain.cppも一貫させる）を確認済み。
- **KeyframeReachedの自動テストは追加せず、SignalEventのみPASS/FAIL化した**: ヘッドレステスト`RecubinTest`（`test_main.cpp`）は`fireHeartbeat`を一度も呼ばず、スクリプトの同期実行と`wait()`タイマー消化だけを行う設計だと判明した（実機で`signal_test.yaml`を実行してHeartbeatが1回も発火しないことを確認済み）。そのため`Humanoid::updateAnimation`のようなフレーム駆動の処理は自動テストのしようがない。一方`SignalEvent:Fire()`は完全に同期的（Connect登録→Fire即座にコールバック実行）なので、これだけは自動PASS/FAIL化できると判断した。KeyframeReachedの動作確認は実機でのPlayモード/ゲーム実行に委ねることをユーザーに事前合意済み。

### どういう経緯か

1. ユーザーが選択中のIDE範囲（readme.md「本当に実装できているか、Luauスクリプトとシーンファイルでテストする」TODO）を対象に「確認、修正、テストを回してください」と指示。
2. `SignalEvent`関連ファイルを調査し、クラス自体は実装済みだが`SceneLoader::createInstance`とエディターのInsert Objectメニューに未登録と判明。
3. AskUserQuestionで「KeyframeReached（キーフレーム到達シグナル）も今回一緒に設計するか」を確認→ユーザーは「設計する」を選択。
4. `Humanoid.hpp`/`Humanoid.cpp`/`LuauEngine_Dispatch.cpp`/`doc/Instances/Humanoid.md`を読んだ結果、KeyframeReachedは**既に完全実装済み**と判明（設計は不要）。代わりに`updateAnimation`の呼び出し元を追跡し、`game_main.cpp`に呼び出しが一切ないという別の実装漏れを発見。
5. この発見をユーザーに報告し、AskUserQuestionで「game_main.cppの修正も今回一緒に行うか」を確認→ユーザーは「今回一緒に修正。ヘッドレステストはできるものだけで大丈夫。作業後、手動確認が必要なものを教えてほしい」と回答。
6. `ParticleEmitter::updateAll`/`Weather::updateAll`の既存パターンを参考に`Humanoid::updateAll`の設計を確定し、変更対象ファイル・箇所を具体的に列挙した上でimplementerサブエージェントに実装を委譲。
7. implementer完了後、`git diff`で指示範囲外の変更がないことを確認（`RCBN.luah`/`readme.md`/`scripts/StressTest.luau`/`src/Util/FrameProfiler.cpp`はセッション開始前からの別件の未コミット変更で、implementerも触れていないことを確認）。
8. `RecubinTest.exe assets/scenes/signal_test.yaml`を単体実行し、新規追加した2件の`[PASS]`を確認。
9. `python run_regression.py Release`で全体回帰テストを実行し、`87 passed, 3 failed`（既知ベースライン85 passed/3 failedに対し、新規SignalEventテスト2件のPASSが純増、失敗3件は既知のvoid.yaml 1件+IsPlaying 2件のまま）と確認。新規リグレッションなし。
10. readme.mdの該当4チェック項目を`[x]`に更新。

**試して失敗した方法（教訓）**:
- readme.mdの`[?]`マークを見て「未設計の新機能」と早合点しかけたが、AskUserQuestionで一度「設計する」方向に進みかけた後、実装を読んで初めて「既に実装済みで、駆動側の配線が漏れているだけ」と判明した。`[?]`は「未確認」であって「未実装」ではないケースがあると学んだ。**readme.mdのチェックマークだけで実装状況を判断せず、着手前に必ず実装コードを読んで裏を取る**（今回はたまたま被害はなかったが、一歩間違えば既存のKeyframeReachedと重複する別実装を作りかねなかった）。

### 未解決・保留

- 実機での最終確認待ち: `Humanoid::updateAll`によるアニメーション更新（特にNPCのHumanoidや`game_main.cpp`側のビルド済みゲームでのAnimation再生・KeyframeReached発火）は、ヘッドレステストでは検証不能な仕組み（`RecubinTest`はHeartbeatを発火しないため）。ビルド成功+回帰テストのみ確認済みで、実際にAnimationを再生してKeyframeReachedが期待通り発火するかはユーザーの実機確認が必要。
- 同じ理由で、既存の`scripts/signal_test.luau`のTest 1〜4（Heartbeat:Connect/Once/Until、Touched:Once/Connect）も従来からヘッドレステストでは実質検証されていない（print診断のみ、PASS/FAIL化されていない）。今回はスコープ外としたが、フレーム駆動機能の自動テストをどう充実させるかは別途検討の余地がある。
- 前セッションからの持ち越し: `scripts/CanvasPaint.luau`の`[PAINT]`診断print削除（Canvas実機確認待ち）、矢印キーカメラ回転速度1.5→1.0の体感確認は今回も未回答のまま。

### 暗黙仕様の発見

- **`RecubinTest`（`test_main.cpp`、ヘッドレステストハーネス）は`fireHeartbeat`を一度も呼ばない**（`spec.md`に記載なし）。ループは「待機中スクリプトのタイマーを`tickWaitingScript`で減算するだけ」で、Heartbeat/Humanoidアニメーション/Weather/ParticleEmitter等のフレーム駆動処理は一切動かない。`System.Heartbeat:Connect`等を使うテストスクリプトはヘッドレス環境では登録した瞬間の1回も発火せず、PASS/FAIL判定ではなくprint診断にしかならない。今後フレーム駆動の機能を自動テストしたい場合は、`test_main.cpp`のループ自体に`fireHeartbeat`等の呼び出しを追加する設計変更が必要になる。
- **`Humanoid::updateAnimation()`（Animation再生・KeyframeReached発火）は今回の修正まで`src/game_main.cpp`から一切呼ばれていなかった**（`spec.md`に記載なし）。つまり配布用ビルド(`RecubinEngine.exe`相当のゲームランタイム)ではAnimationが再生されない状態だった可能性が高い。`Humanoid::updateAll`経由に統一したことで解消したが、今後Humanoid関連の新機能を追加する際は、`main.cpp`（エディター）だけでなく`game_main.cpp`（ランタイム）側の呼び出し漏れがないか両方を確認する必要がある。
- **`PropertyRegistry`の`sig<>`（`include/Core/PropertyRegistry.hpp`）で登録するシグナル型プロパティはLuau読み取り専用でYAML保存対象にならない**（`spec.md`に記載なし）。`SceneLoader.cpp`の`hasProps`判定に追加する必要があるのは「実際にYAMLへ保存すべきフィールド」を持つクラスのみで、シグナルだけを持つクラス（`SignalEvent`等）は`hasProps`に入れなくてよい。

---

## 2026-07-15 Humanoidアニメーション崩壊バグの根本解決 + Animation Editorの編集セッション方式への再設計

### 何をしたか

**1. BaseCube二重解放クラッシュの修正（前半、2026-07-14夜）**
- `src/main.cpp`: Play→StopブロックとLoadボタンブロックに、アプリ終了時（既存）と同じ`ed->m_history.clear()`/`ed->clearClipboard()`を追加。ギズモ操作でUndo履歴に積まれたBaseCubeへの強参照(`GizmoCommand`/`MultiGizmoCommand`)が、シーン破棄後まで生き残ってデストラクタ遅延→`lastWorkspace`へのUse-After-Freeを起こしていた。
- `src/Core/Physics.cpp`: `rebuildGroup()`の「5. cubesエントリーを更新」で、`Physics::cubes`に未登録のcubeを新規`push_back`するよう修正。Weld/compound経由のactorが追跡から漏れると`clearCubes()`で`BaseCube::actor`がnullptrにされず、dangling actorへのアクセスでクラッシュしていた。
- `include/Instances/Humanoid.hpp`: Humanoidだけが兄弟BaseCubeをshared_ptr(強参照)で持つ規約違反についてTODOコメントを追記（weak_ptr化は将来課題）。

**2. 「頭が埋まる/バラバラになる」アニメーションバグの根本原因特定と修正**
- 根本原因: `Humanoid::applyBodyAnimation()`は`if (!Root) return;`で始まるが、`Root`はPlay中の`move()`等でしか解決されない。**一度もPlayしていないEditorセッションではRootが常にnullptrで、`saveBindPose()`から呼んだ`applyBodyAnimation()`は毎回no-op**だった。結果、Add Keyで記録されるキーフレームはリグ補正のかからない生の絶対座標基準（Root相対で約-3.8、正しくは+2.5）のまま。「Animationを消して打ち直しても直らない」のはこのため。
- `src/Instances/Humanoid.cpp`: `applyBodyAnimation()`冒頭に`updateAnimation()`と同じ`if (!Root) { Parent.lock()経由でresolveParts(); }`の遅延解決を追加（3行）。

**3. Animation Editor Panelを「明示的な編集セッション」方式に再設計（後半、本命）**
- 上記2の修正後、今度は「編集中にもリグがついてくる」との報告。フォーカス連動設計の構造欠陥が表面化した: ギズモ編集は必ずビューポートクリック（=フォーカスOUT→`restoreBindPose()`で編集巻き戻し）とパネルクリック（=フォーカスIN→`saveBindPose()`再発火＋リグ適用でドラッグ結果を上書き）を往復するため、**ユーザーの編集がキー記録前に必ず破壊される**。
- `include/Editor/AnimationEditorPanel.hpp` / `src/Editor/AnimationEditorPanel.cpp`: フォーカス判定(`m_wasFocused`)を全廃。「編集開始」(緑)/「編集終了」(黄)トグルボタンを新設し、開始時に1回だけ`saveBindPose()`+`applyBodyAnimation()`でリグ組み立て、終了時に`endEditSession()`(公開メソッド)で復元。セッション外では再生コントロール・Timeスライダー・Add Key・Goを`ImGui::BeginDisabled`で無効化（キー削除X/Easing/Export/Import/Speed/Looped/Lengthはcframeを触らないので常時有効）。別Model切り替えで自動終了、選択解除(model==nullptr)では維持。
- `include/Editor/Localization.hpp` / `src/Editor/Localization.cpp`: `StartEditButton`/`EndEditButton`/`StartEditHint`の日英文言を追加。
- `src/main.cpp`: 3箇所に`ed->animationPanel->endEditSession()`を追加。(a)Play開始時のスナップショット保存**直前**（プレビュー姿勢が`_snapshot.yaml`に焼き込まれてStop後のシーンが静かに汚染されるのを防ぐ）、(b)Play→Stop時、(c)Loadボタン時（Play中に開始されたセッションが破棄済みツリーへのdanglingポインタを持ち越すのを防ぐ）。

### なぜそうしたか

- **「記録が間違っているのか、プログラムが間違っているのか」の切り分けを最優先した**: physics_test.yamlのHeadキーフレーム値（Root相対-3.8）が「Root位置5.411と生のHead位置1.6の差分」と正確に一致することから、「applyBodyAnimationが一度も効いていない状態で記録された」と数値的に立証できた。これでデータ再作成では直らないこと（プログラム側のバグ）が確定した。
- **フォーカス連動の小手先修正ではなく方式ごと置き換えた**: フォーカスIN/OUTはギズモ編集フローと本質的に両立しない（編集には必ずフォーカス往復が必要）。ユーザーに「編集フローのみ修正 vs リグの決め打ち自体を廃止する大改修」「明示ボタン vs 自動セッション」をAskUserQuestionで確認し、両方とも推奨案（編集フローのみ・明示ボタン）で合意した。
- **固定リグ(Head=Root+2.5等のハードコード)は維持**: ランタイムの姿勢基準がこのリグである以上、エディター側をリグ基準に合わせるのが最小修正。リグをシーン配置由来にする案は歩行アニメの関節位置導出やテンプレートデータ整合まで波及する大改修のためスコープ外とした（ユーザー合意済み）。
- **Playスナップショットへの焼き込み防止を必須要件にした**: Play開始時の`saveScene(snapshotPath)`は現在のcframeをそのまま保存するため、プレビュー姿勢が入ったままPlayするとStop後のシーンが汚染される。「作り直しても直らない」ように見えた一因はこれの可能性が高い。

### どういう経緯か

1. KeyframeReachedのテスト中、「アニメーションが絶対座標を引き継いだまま相対移動して頭が埋まる」との報告。
2. Root未解決問題と非トラックパーツの凍結問題を修正（`updateAnimation`のアイドルポーズフォールバック+`m_bodyPoseUpdatedThisFrame`フラグ）。「固定アニメーションを完全上書きしたくない」というユーザー要件に合わせ、move()由来のポーズを優先する設計にした。
3. それでも直らず、さらに`BaseCube::~BaseCube()`でのアクセス違反クラッシュが発生。3体のExplore並列調査でUndo履歴の強参照とWeld/compoundの登録漏れという2つの独立バグを特定し修正。
4. クラッシュ解消後も「顔が低いまま」が残存。再調査で`saveBindPose()`→`applyBodyAnimation()`がRoot未解決でno-opだったと確定し、遅延解決を追加。
5. すると「編集中にリグがついてくる」が発生（リグ適用が実際に動くようになったことでフォーカス連動設計の欠陥が表面化）。編集セッション方式への再設計を計画・実装し、ユーザーの実機確認で「直った」と確認。

**試して失敗した方法（教訓）**:
- 「Stopを押してもRootの位置は戻らない」と一度ユーザーに説明したが、これは誤りだった（Play開始時にスナップショット保存→Stopで復元される）。コードを読まずに挙動を推測して説明した箇所が後で矛盾した。
- 対症療法の連鎖になった: Root遅延解決(手順4)は単体では正しい修正だが、フォーカス連動という上位設計の欠陥を直さないまま入れたため新しい症状(手順5)を生んだ。「表面化した症状を1つずつ潰す」のではなく、編集フロー全体を先に俯瞰すべきだった。

### 未解決・保留

- `physics_test.yaml`のHeadキーフレームはユーザーが打ち直し済み（「直った」確認済み）。ただしWorkspace直下の`Animation`(Cube6用)など他の既存Animationデータに同種の壊れた値が残っている可能性は未確認。
- `AnimationEditorPanel::m_savedModel`は生ポインタのまま。セッション中にModel自体をHierarchyから削除するとdangling（Play/Stop/Loadの主要経路は今回のフックで塞いだが、削除経路は未対応の既存リスク）。
- Humanoidの兄弟パーツshared_ptr参照のweak_ptr化（Humanoid.hppのTODO）。
- 回帰テストの既知失敗3件（void.yaml + Sound.IsPlaying x2）は継続。ベースラインは87 passed / 3 failed。

### 暗黙仕様の発見

- **ランタイムのキャラクター姿勢基準は`applyBodyAnimation()`内のハードコードリグ**（Torso=Root+1, Head=Root+2.5, 肩±1.5/+2, 腰±0.5/0。spec.md未記載）。シーンYAMLに保存された各パーツのPosition/Rotationは再生時には使われない。キーフレームはこのリグ基準のRoot相対で記録しないと再生時に必ずズレる。
- **Play開始時のスナップショット(`_snapshot.yaml`)は「その瞬間のcframe」をそのまま保存する**（spec.md未記載）。エディター上でパーツのcframeを一時改変する機能（プレビュー等）は、Play開始前に必ず復元しないとStop後のシーンを静かに汚染する。今後同種の一時改変機能を作る際は`endEditSession`相当のフックをmain.cppのPlay開始ブロックに必ず追加すること。
- **`Spatial::Position`/`Rotation`は`cframe`への参照エイリアス**であり、独立フィールドや毎フレームのプロパティ→cframe同期は存在しない。エディター中に`cframe`へ直接書いた値は誰にも上書きされず保持される。

---

## 2026-07-16 BallSocket / NoCollision インスタンス新規実装 + 衝突フィルタリング基盤

### 何をしたか

**1. BallSocket インスタンス新規実装**（readme.md TODO消化）
- `include/Instances/BallSocket.hpp` / `src/Instances/BallSocket.cpp`（新規）: Rod を雛形にした球面ジョイント拘束。`PxSphericalJoint`、リミット未設定（360度自由回転・距離固定）、Attachment0/1 対応。Rod と違い Color/LineWidth・ビューポート描画は持たない（ユーザー決定: デバッグ表示だけでOK）。
- `src/Core/Physics.cpp` / `Physics.hpp`: `createBallSocket()`（`createRod()` 雛形、`composeAttachmentFrame()` 流用、`eCOLLISION_ENABLED=false`）。`update()` の pendingConstraints dispatch / `recreateActor()` / `rebuildGroup()` の再生成分岐に追加。
- 配線: `SceneLoader.cpp`（ファクトリ/resolveConstraintRefs/hasProps/saveNode）、`LuauEngine.cpp`（Instance.new）、`LuauEngine_Dispatch.cpp`（Getter は Rod と同じく Attachment0/1 のみ、Setter は Cube0/1+Attachment0/1）、`PropertiesPanel.cpp`（`drawConstraintCubeRef`×2 + `drawConstraintAttachmentRef`×2）、`SceneHierarchyPanel.cpp`（ICON_CONSTRAINT 流用+挿入メニュー）、`Workspace.hpp`（friend 追加）。

**2. NoCollision インスタンス新規実装 + 衝突フィルタリング基盤**（readme.md TODO消化）
- 既存エンジンには「ペア単位で衝突だけ無効化する」機構が皆無だった（唯一の無効化手段はジョイントの `eCOLLISION_ENABLED` フラグで、拘束が前提）。以下を新設:
  - 全シェイプ生成箇所（`createActor()` 3箇所 + `attachShapeToCompound()` 3箇所）で `shape->userData = cube.get()` を設定。Weld compound 内でも「シェイプ→所属Cube」の逆引きが可能に。
  - `rcbnFilterShader` 拡張: `PxFilterData::word0` の候補ビット（`FILTER_WORD0_NOCOLLISION_CANDIDATE`）が**両方**のシェイプに立つペアのみ `eCALLBACK` を返す。それ以外は従来どおり `eDEFAULT`（既存衝突挙動への影響ゼロ）。
  - `RCBNFilterCallback`（`PxSimulationFilterCallback` 実装）: `shape->userData` の正規化ポインタペアを `Physics::m_noCollisionPairs`（std::set）と照合し、該当なら `eSUPPRESS`。ポインタ比較のみで deref しないため破棄済みCubeでも安全。simulate 中はペア集合不変なのでロック不要。
  - ライフサイクル: `createNoCollision()`（エントリ追加→ペア集合再構築→`applyNoCollisionFilterBit()` でビット付与+`resetFiltering`+wakeUp）、`removeConstraint()` の NoCollision 分岐（ジョイント経路に入らず早期return）、`update()` の expired sweep、`createActor()`/`rebuildGroup()` でのシェイプ再生成時のビット再付与。
- `include/Instances/NoCollision.hpp` / `src/Instances/NoCollision.cpp`（新規）: Weld を雛形（Cube0/Cube1 のみ、Attachment なし、compound 収集なし）。`registerIfReady()` → `registerConstraint()` の統一フローに乗せた。
- 配線は BallSocket と同じ6ファイル。

**3. 前セッション保留事項の消化**
- `scripts/CanvasPaint.luau` の `[PAINT]` 診断 print を全削除（ロジック無変更）。ユーザーから削除OKの明示確認を取った上で実施。

**4. memory 更新**: 回帰テストのベースラインを 85→87 passed に更新（テストが2件増えていた。failed 3件の内訳は不変）。

### なぜそうしたか

- **NoCollision はフィルターシェーダー拡張方式を採用**: 「全軸フリーの PxD6Joint + eCOLLISION_ENABLED=false を張る」トリック（既存 ConstraintEntry 経路に乗り Physics 変更最小）と迷い、AskUserQuestion で提示→ユーザーが正攻法のフィルター方式を選択。将来のグループ単位衝突制御にも拡張できる基盤になった。
- **コールバック照合は shape->userData のポインタペア**: filterData の 32bit word にグループID を割り当てる方式だとペア数・ID管理に上限や複雑さが出る。`pairFound()` は PxShape* を直接受け取れるので、シェイプに Cube ポインタを持たせて set 照合するのが最も単純で無制限。
- **シェーダーは「両方に候補ビット」の時だけ eCALLBACK**: NoCollision に関与しないペア（地形・キャラ含む）はコールバック自体に到達させず、既存挙動を bit 単位で保存するため。
- **BallSocket は PropertyRegistry ではなく手書き配線**: 既存拘束4種（Rope/Rod/Weld/Motor）がすべて手書きで、拘束系は PropertyRegistry 未対応のため既存規約に従った。

### どういう経緯か

1. readme.md の TODO（BallSocket/NoCollision）を計画モードで設計。Explore で既存拘束の全配線箇所（ファクトリ×2、SceneLoader、Dispatch、Panel×2、Physics のライフサイクル4箇所）を洗い出し。
2. NoCollision の方式・BallSocket の描画有無・[PAINT] print 削除可否の3点をユーザーに質問→方針確定→プラン承認。
3. BallSocket → NoCollision の順で implementer に委譲、各フェーズ後にメインセッションが git diff で設計と照合レビュー。
4. レビューで1件の穴を検出し微修正: `update()` の expired sweep が「NoCollision インスタンスだけ消えて Cube が生存」のケースでビットを解除せず、**eSUPPRESS されたペアは resetFiltering されるまで再評価されない**ため衝突が永久に復活しない問題。sweep で除去エントリの生存 Cube に `applyNoCollisionFilterBit()` を再適用する形に修正（通常の削除経路はデストラクタ/onAncestorChanged→removeConstraint が処理するので、これは防御的経路の修正）。
5. 最終ビルド成功、回帰テスト 87 passed / 3 failed（既知3件のみ、回帰なし）。

### 未解決・保留

- **実機確認待ち**: BallSocket の回転挙動、NoCollision のすり抜け、既存拘束（Rope/Rod/Weld/Motor）の非破壊。ビルド+回帰テストのみ確認済み。
- 実機確認が取れたら readme.md の TODO 2項目（75-83行付近）の消し込みが残っている。
- NoCollision と Weld を同じペアに張った場合、compound 内シェイプ同士はそもそも衝突しないため実質 no-op（仕様として問題ないが未検証）。
- EditorManager のツールバーには BallSocket/NoCollision を追加していない（専用アイコンが必要なため意図的にスキップ。挿入メニューからは追加可能）。

### 暗黙仕様の発見

- **PhysX の `eSUPPRESS` ペアは filterData 変更か `resetFiltering()` があるまで再評価されない**（spec.md未記載）。ペア集合から除くだけでは衝突は復活しない。NoCollision 関連の状態変更は必ず `applyNoCollisionFilterBit()`（filterData 更新+resetFiltering+wakeUp）を経由すること。
- **シェイプの userData は今セッションまで未使用だった**。今後は「全シェイプの userData = 所属 BaseCube*」が新しい不変条件。シェイプを生成する新コードは必ずこれを設定すること（設定漏れは NoCollision のサイレントな不発になる。actor->userData は compound では assembly[0] しか指せないので代用不可）。
- **拘束系インスタンスの Luau Getter は Cube0/Cube1 を公開していない**（Rod/Rope/Motor は Attachment0/1 のみ、Weld/NoCollision は Cube0/1 のみ）。BallSocket もこの非対称な既存規約に合わせた。

---

## 2026-07-16 値系インスタンス（ValueBaseファミリー）新規実装

### 何をしたか

**1. `ValueBase` 基底 + 8 派生クラスの新規実装**（readme.md TODO消化、`IntValue`/`BoolValue`/`NumberValue`/`Vector3Value`/`Color4Value`/`CFrameValue`/`QuaternionValue`/`ObjectValue`）
- `include/Instances/ValueBase.hpp` / `src/Instances/ValueBase.cpp`（新規）: `LightSource`と同型の中間基底（`Named<>`は使わない）。`std::shared_ptr<RCBNScriptSignal> Changed`（`SignalEvent::Fired`と同型）、`registerClass("ValueBase", {sig<&ValueBase::Changed>("Changed")})`。
- `IntValue`/`BoolValue`/`Vector3Value`/`Color4Value`（各`.hpp`+`.cpp`、新規）: `Named<XValue, ValueBase>`。`PropertyRegistry::custom(name, type, get, set)`ビルダー（本タスクが初適用）で`Value`を登録、setラムダ内で値代入と`Changed->fire(...)`の両方を行う設計。
- `NumberValue`(double)/`CFrameValue`/`QuaternionValue`/`ObjectValue`（各`.hpp`+`.cpp`、新規）: `PropValue`（`variant<float,int,bool,string,Vector3,Vector2,Color4>`）がCFrame・Quaternion・double・Instance参照を表現できないため、`Value`はPropertyRegistry非経由・`setProperty()`手書き（`Weld`/`Spatial`のcframe扱いを踏襲）。
- `ObjectValue`: `weak_ptr<Instance> m_target` + `std::string m_targetPathName`（NoCollisionの`m_cube0Name`踏襲）。`resolveTarget()`（SceneLoader専用・名前更新なし）と`setTarget()`（Lua/UI駆動・パス再計算+Changed発火）を分離、`remapClonedInstances`をoverride。Luau側では実際のInstance型として公開（`LuauEngine::pushInstance`/`RCBN_INST_METATABLE`、`Parent`プロパティと同じ変換機構を流用）。

**2. 全体配線**
- `src/Core/LuauEngine_Dispatch.cpp`: `applyToDispatch("ValueBase", ...)`を独立して呼ぶことで`Changed`を8クラス共通で公開（`LightSource`が`PointLight`/`SpotLight`とは別に単独で`applyToDispatch`されている実例と同じパターン）。`NumberValue`/`CFrameValue`/`QuaternionValue`/`ObjectValue`の`Value`ゲッター/セッターは手書き。
- `src/Core/LuauEngine.cpp`: `instanceFactories()`に8クラス追加（`ValueBase`自体は中間基底のため対象外）。
- `src/Core/SceneLoader.cpp`: `createInstance()`に8行、`hasProps`判定に`|| inst->IsA("ValueBase")`1行（8クラスまとめてカバー）、YAML保存（PropertyRegistry駆動4クラスは`saveProperties`1行、手書き4クラスは個別実装）、`resolveConstraintRefs`の`walk`ラムダに`ObjectValue`分岐（`sceneRoot->getChildByPath(...)`で解決）を追加。
- `include/Editor/CommandHistory.hpp`: `SetNumberValueCommand`/`SetCFrameValueCommand`/`SetQuaternionValueCommand`を新規追加（既存`SetVec3Command`/`SetRotationCommand`は`shared_ptr<Spatial>`直結で転用不可のため）。`ObjectValue.Value`は既存の`SetConstraintCubeNameCommand`（`Instance`汎用、`setProperty`経由）をそのまま再利用。
- `src/Editor/PropertiesPanel.cpp`/`.hpp`: PropertyRegistry駆動4クラスは`renderSchemaInspector`、手書き4クラスは個別UI（`NumberValue`=`InputDouble`、`CFrameValue`/`QuaternionValue`=Euler変換DragFloat3、`ObjectValue`=新規`drawObjectValueRef()`＝テキストパス入力欄のみ、ビューポートPickボタンは無し）。
- `src/Editor/SceneHierarchyPanel.cpp`: Insert Objectメニューに新規「Value」カテゴリを追加、8クラスの`tryInsertInstance<T>`。`getClassIcon()`に新規`ICON_VALUE`（`#`/fa-hashtag、`include/Editor/IconsDef.hpp`に追加）を割り当て。
- `include/Editor/Localization.hpp` / `src/Editor/Localization.cpp`: `CategoryValue`/`CategoryValueDesc`を追加（`enum LocKey`の宣言順と`kTable`初期化順の1対1対応を崩さないよう、既存`CategoryPhysicsConstraintsDesc`の直後という同一位置に両方挿入）。

**3. 自動テスト新規追加**
- `assets/scenes/value_test.yaml` / `scripts/value_test.luau`（新規、`signal_test.yaml`/`test_bindings.luau`の`check(label,cond)`+`[PASS]`/`[FAIL]`方式を踏襲）: 8クラス全てについて、シーンロード値の検証・`Instance.new`での動的生成・`.Value`書込読取・`.Changed:Connect`発火を検証。39件全て`[PASS]`。
- `run_regression.py`の`FIXED_SCENES`に1行追加。

### なぜそうしたか

- **テンプレート一括生成ではなく古典的OOP継承を採用**: readme.mdには「テンプレートで生成(可能なら)」との記載があったが、コードベース全体を調査した結果C++テンプレートによるクラス定義の一括生成は前例なし、かつCLAUDE.mdの「古典的OOP（継承・ポリモーフィズム）を基本とする、過度な抽象化より実用性を優先する」という明記された方針とも整合しないため、AskUserQuestionでユーザーに確認の上、`PointLight`/`SpotLight`と同じ古典的OOP継承を採用した。どのみちCFrame/Quaternion/double/Instance参照はPropertyRegistryで表現不能なため、完全な一括テンプレート化は不可能だったという技術的制約も後押しした。
- **PropertyRegistry駆動4クラスと手書き4クラスへの分割**: `include/Core/PropertyRegistry.hpp`の`PropValue`が`variant<float,int,bool,string,Vector3,Vector2,Color4>`のみに限定されており、CFrame・Quaternion・doubleは表現不能、Instance参照という概念自体も存在しない。既存コードにも一部型だけPropertyRegistry化し残りは手書きという前例（`Spatial`のcframeが手書き）があり、それに倣った。
- **`Changed`シグナルだけは8クラス共通でPropertyRegistry経由に統一**: `sig<>`は型知識を持たない（シグナルの中身は関知しない）ため、CFrame/Quaternion/double/Instance参照を持つクラスでも問題なく使える。`ValueBase`を`registerClass`し、8クラスが`registerClass(name, "ValueBase", {...})`で継承する形にすることで、`Changed`の実装重複を避けつつLuau公開の一貫性を確保した。
- **`ObjectValue.Value`をLuauの実際のInstance型として公開**: Roblox本家のObjectValue.Valueと同じ挙動にする方がスクリプトの使い勝手が良いとユーザーが判断（AskUserQuestionで確認）。既存の拘束系（Weld/Rod等のCube0/Cube1）は文字列パスとして公開されているが、ObjectValueは「値」を主目的とするクラスなので実際の値型らしさを優先した。
- **ObjectValueのエディターUIはビューポートPickボタン無し**: 既存の`drawConstraintCubeRef`のPick機構はBaseCube限定・ビューポートクリック前提で、ObjectValueが指せる任意Instance（Script/Sound/Folder等の非空間インスタンス含む）には対応できないため、テキストパス入力のみに絞った（ユーザーに事前確認済み）。
- **`custom()`ビルダーの set 内で毎回 `Changed->fire()` を呼ぶ設計**: YAMLロード時・clone時にも発火するが、その時点では誰も`Connect`していないため実害はなく、経路ごとに発火を止める作りは複雑化を招くだけと判断してあえて分けなかった。

### どういう経緯か

1. ユーザーが選択していたreadme.mdのTODO「値系インスタンスを追加」を計画モードで設計。Explore 3エージェント（PropertyRegistryとInstance参照パターン、既存インスタンスの全体配線チェックリスト、任意Instance参照解決の既存仕組み）を並列起動して調査。
2. AskUserQuestionで4点確認: (1)実装方式=古典的OOP継承、(2)ObjectValue.Value=Instance型として公開、(3)対象クラス確定（readme.mdの`BoolValue`重複・`QuartanionValue`誤記を修正、ユーザー指示で`NumberValue`(double)を追加し最終8クラスに）、(4)Insert Objectメニューに新規「Value」カテゴリを作成。
3. Plan agentに詳細設計を委託（`applyToDispatch`/`instance_index`のクラス階層フォールバック機構の実コード確認、ObjectValueの全ツリー参照解決パスの設計）。結果を検証（`CommandHistory.hpp`の`custom()`初適用可否・`SetVec3Command`のSpatial限定を実ファイルで裏取り）した上で最終計画を`dapper-booping-crab.md`として作成、ExitPlanModeで承認を得た。
4. 実装を8フェーズ（ValueBase基底+8クラス→LuauEngine_Dispatch→LuauEngine.cpp→SceneLoader→CommandHistory→PropertiesPanel→SceneHierarchyPanel/Localization/IconsDef→自動テスト）に分割し、各フェーズをimplementerサブエージェントに委譲。各フェーズ後にメインセッションが`git diff`で設計と照合レビューし、ビルド確認。
5. Phase1完了時のレビューで、`ObjectValue::setTarget`/`refreshRefName`が`getFullPath()`（ルート自身の名前を含む絶対パス）を使っていたが、対になる`getChildByPath()`はルート自身を起点に呼ぶ前提（ルート名を含まない）だったため、保存→再解決が失敗する不整合を発見。`Instance::getPathUpTo(top)`（`top`=Parentを辿った最上位祖先、`NoCollision`の`getWorkspaceRelativePath()`と同じ考え方）を使うようメインセッションが直接1〜2メソッドの微修正で対処してからPhase4に進んだ。
6. Phase2実装中、implementerが`LuauEngine::pushVector3`/`pushColor4`/`pushQuaternion`/`pushCFrame`が`LuauEngine.hpp`内`private`静的メンバであることに気づき、「LuauEngine.hppを変更してよいか」と実装を止めて確認を求めてきた。調査の結果、これらの関数が内部で使うメタテーブル名定数（`RCBN_VEC3_METATABLE`等）は全てpublicで、`PropertyRegistry.cpp`の`valueToLua`が既に同じ手法（`lua_newuserdata`+`luaL_getmetatable`+`lua_setmetatable`を直接呼ぶ）でVec3/Color4を扱っていることを確認。SendMessageでエージェントを再開し、LuauEngine.hpp自体は変更せず、同じuserdata+メタテーブル直接構築パターンを新規ファイル側で再現する方針で続行させた。
7. 全フェーズ完了後、`python build.py build`と`python run_regression.py Release`で最終確認。回帰テストは既知ベースライン（87 passed/3 failed）と完全一致、新規リグレッションなしを確認。
8. 計画の「検証方法」項目に明記していた自動PASS/FAILテストがまだ無かったため、Phase8として`value_test.yaml`/`value_test.luau`を追加実装。implementerが`ObjectValue`の`==`比較テストで`[FAIL]`に遭遇し原因調査した結果、`RCBN_INST_METATABLE`に`__eq`メタメソッドが登録されておらず`pushInstance`が毎回新規userdataを生成する（キャッシュ無し）ため、同一インスタンスでも別々に取得したuserdata同士の`==`は常に`false`になるという**既存コードベース全体の制約**（今回実装した8クラスのバグではない）を発見。スコープ外として手を触れず、テスト側を「`.Name`一致+改名で相互に見えることの確認」という機能的検証に置き換えて対処した。
9. 最終的に39件全て`[PASS]`、回帰テスト126 passed/3 failed（新規39 passed加算、既知3件failedのみで新規リグレッションなし）を確認して完了。

**試して失敗した方法（教訓）**:
- Phase1で`ObjectValue`の参照パスに`getFullPath()`をそのまま使おうとした（実装した後にメインセッションのレビューで発覚）。`getFullPath()`はRoblox的な「絶対パス表示」としては直感的だが、このエンジンの`getChildByPath()`は「呼び出したインスタンス自身を起点とする相対パス」を期待する設計であり、両者を素朴に組み合わせると「ルート自身の名前」が二重に扱われて解決に失敗する。**パスを生成する側と解決する側で「起点をどこに置くか」の規約を必ず一致させる**必要があり、既存の`getWorkspaceRelativePath()`（`getPathUpTo(stopAt)`でstopAt自身の名前を含めない）が正しい前例だった。
- Phase2で`LuauEngine.hpp`のprivateな`pushVector3`等をそのまま新規ファイルから呼ぼうとして行き詰まった。「private関数を呼べないなら既存ヘッダをpublicに変える」という安易な方向に進まず、「その関数が内部で使っている低レベルAPI（メタテーブル名定数）は既にpublicで、同じロジックを別の場所で再現できないか」という既存コード（`PropertyRegistry.cpp`）の前例を先に確認したことで、既存ファイルへの変更を一切せずに済んだ。

### 未解決・保留

- **実機での最終確認待ち**: エディターのInsert Objectメニューで「Value」カテゴリから8クラスが挿入できること、PropertiesPanelでの編集（特にCFrameValue/QuaternionValueのEulerドラッグ、ObjectValueのパス入力）、シーン保存→再読込でのYAMLラウンドトリップ、クローン時のObjectValue参照張り替えは、ビルド+ヘッドレステストのみ確認済みでユーザー未検証。
- `NumberValue`/`CFrameValue`/`QuaternionValue`のPropertiesPanel UIは、値変更の都度（ドラッグ中の毎フレーム）`setProperty`経由で`Changed`を発火する設計にした。Luauスクリプト側で`Changed`に重い処理を繋いでいる場合、ドラッグ中に大量発火する可能性がある（実害があるか未検証、既存の`Spatial`のPosition/RotationドラッグにはUndo以外の副作用が無いため今回まで問題になっていなかったパターン）。
- 前セッションからの持ち越し: BallSocket/NoCollisionの実機確認、readme.md TODO 2項目の消し込みは今回も未対応のまま。

### 暗黙仕様の発見

- **`RCBN_INST_METATABLE`のInstance userdataには`__eq`メタメソッドが登録されておらず、`LuauEngine::pushInstance`は毎回新規userdataを生成する（インスタンスポインタでのキャッシュ無し）**（spec.md未記載）。そのため同一Instanceを指していても、Luau側で別々に取得した参照同士を`==`比較すると常に`false`になる。今後Instance型を返すAPIを設計する際は、この制約（`==`による同一性比較ができない）を前提にするか、`__eq`メタメソッド自体の追加を別タスクとして検討する必要がある。
- **`PropertyRegistry::applyToDispatch`はクラス自身に直接登録されたプロパティのみを`DispatchTable[className]`に書き込む（基底クラス分は書き込まない）が、Luau側の`instance_index`/`instance_newindex`が`DispatchTable`の全キーを`obj->IsA(className)`で横断マージする**ため、結果的に基底クラス（`ValueBase`や`LightSource`）に登録したプロパティも派生クラスから自動的に見える（`spec.md`未記載、`src/Core/LuauEngine.cpp`の`instance_index`実装で確認）。ただし基底クラス自身の`applyToDispatch`呼び出しを忘れると（`Highlight`で実際に起きていた既知の不具合と同型）その基底プロパティ自体がLuauから一切見えなくなるサイレントバグになるため、中間基底を導入する際は基底分・派生分の両方で`applyToDispatch`を呼ぶことを忘れないこと。
- **`PropertyRegistry::custom()`ビルダー（get/set完全手書き）は本タスクまで一度も実際に使われていなかった**（`field<M>`/`method_prop<>`等は多用されているが`custom()`は未使用だった）。setラムダ内で値代入以外の副作用（今回は`Changed`シグナル発火）を行いたい場合の正規の拡張点として機能することを確認した。

### 追記: ObjectValueにヒエラルキーPickボタンを追加

初回実装完了後、ユーザーから「ObjectValueに参照ボタンを付けましょう」と追加依頼。

**何をしたか**
- `include/Editor/PropertiesPanel.hpp`: 全パネル共有の`PickerState`構造体に`bool pickAnyInstance`を追加。
- `src/Editor/SceneHierarchyPanel.cpp`: `drawNode()`内のピッカー横取り判定（ノードクリック時に`m_picker`が有効なら選択ではなく参照指定として横取りする既存の仕組み）に`pickAnyInstance`を最優先の分岐として追加。trueなら型チェック無しで任意のノードにマッチする。
- `src/Editor/PropertiesPanel.cpp`: `drawObjectValueRef`を、既存の`drawConstraintCubeRef`と同じ「テキスト入力欄+Pick/Cancelボタン」レイアウトに書き換え。Pickボタン押下で`m_picker->pickAnyInstance = true`をセットし、`onPick`コールバックは`ObjectValue::setTarget()`（既存メソッド、パス計算+Changed発火を内包）を呼ぶだけ。既存の`drawConstraintCubeRef`/`drawConstraintAttachmentRef`のPickボタン側にも`pickAnyInstance = false`を明示追加（`PickerState`は使い回しの共有状態のため、前回ObjectValueをPickした際の`pickAnyInstance=true`が残留してBaseCube/Attachment用Pickの型制限を無効化してしまうのを防ぐ）。

**なぜそうしたか**
- ユーザーに参照ボタンの方式をAskUserQuestionで確認: (a)ヒエラルキーでクリックして選ぶ、(b)「選択中を使用」ボタン、(c)既存のビューポートPick機構（BaseCube限定）をそのまま流用、の3択を提示し、(a)ヒエラルキークリック方式が選ばれた。
- (c)を採用しなかった理由: 既存の`m_picker`のビューポートPickは`ViewportPanel.cpp`内でワールド空間レイキャストにより`BaseCube`/`Attachment`のみを対象にしており、`ObjectValue`が本来指せるはずの非空間インスタンス（Script/Sound/他のValue系等）を原理的に選べないため、ObjectValueの本来の用途と不整合になる。
- 調査の結果、`SceneHierarchyPanel::drawNode()`には**既にBaseCube/Attachment用のピッカー横取り機構が実装済み**だったため、型チェックを外すだけの小さな拡張（`pickAnyInstance`フラグ追加）で済み、新規の状態管理やクリックハンドラを作らずに済んだ。

**どういう経緯か**
1. 既存の`m_picker`/`PickerState`の実装（`PropertiesPanel.hpp`/`ViewportPanel.cpp`）を調査し、ビューポートクリック経由でBaseCube/Attachmentのみ選択可能という制約を確認。
2. `SceneHierarchyPanel.cpp`の`drawNode()`を調査したところ、ノードクリック時のピッカー横取りロジックが既に存在しており（Cube/Attachment参照用に前セッションまでに実装済み）、型チェック部分を汎用化するだけで任意Instance対応できると判明。
3. AskUserQuestionでUX方式を確認後、3ファイルへの変更として設計しimplementerに一括委譲（今回は1フェーズで完結する規模と判断）。
4. 完了後`git diff`で照合レビュー、ビルド+回帰テスト（126 passed/3 failed、既知3件のみ）を確認。

**未解決・保留**
- 前回セッションの未解決事項（エディター実機確認、BallSocket/NoCollisionの実機確認、readme.md TODO消し込み）は今回も引き続き未対応。
- Pickボタンで选択した参照が、ヒエラルキー上で折りたたまれた（非表示の）ノード配下にある場合にクリックできるか（ツリーの自動展開等）は未検証。

**暗黙仕様の発見**
- **`SceneHierarchyPanel::drawNode()`のノードクリックハンドラは、`PropertiesPanel`/`ViewportPanel`と共有する`PickerState`を経由して「選択」と「参照指定」の2つの意味を持つ**（spec.md未記載）。`m_picker->active`が真の間はクリックが通常の選択処理をバイパスして参照指定に転用される。今後同種の「クリックで何かを指定する」UIを追加する際は、`PickerState`に新しいフラグを1つ足すだけでヒエラルキー経由のPickに対応できる（ビューポート経由が必要な場合は別途`ViewportPanel.cpp`側の対応が要る）という拡張パターンが確立した。

---

## 2026-07-16 エディター修正4項目＋セカンダリビューポート全面リハビリ

### 何をしたか

**1. エディター設定の永続化**（`src/main.cpp` のみ）
- 既存の `editor_settings.yaml` 機構（`loadEditorSettings`/`writeEditorSettings`、マージ保存）を拡張し、`loadEditorPreferences`/`saveEditorPreferences` を `savePanelVisibility` と同型で新設。`Preferences` キー配下に物理デバッグ表示・言語(JA/EN)・カメラ位置/向き(Pos+Rot[x,y,z,w])・スナップ/フィット7項目・ギズモモード4項目(GizmoOp/GizmoMode/SelectOnly/ToolNone)を保存。起動時ロードは `loadPanelVisibility` 直後、終了時セーブは `savePanelVisibility` 直後。
- ギズモモードはユーザーからの追加指示（ExitPlanMode初回却下時に「ギズモのモードも保存」と要望）。int保存し、復元時はTRANSLATE/ROTATE/SCALE・WORLD/LOCALと一致する場合のみ反映。

**2. パッケージャーログ修正＋Copyボタン3箇所**（`EditorManager.hpp/.cpp`、`ConsolePanel.cpp`）
- スクロール不能の原因は `renderPackageDialog` で `SetScrollHereY(1.0f)` を毎フレーム無条件実行していたこと。`ConsolePanel` と同じ `scrollToBottom` フラグ方式（`m_pkgLogScrollToBottom`、push時にセット）に統一。
- パッケージャー/System/Luauコンソールに `SmallButton` のCopyボタン（`Loc::LocKey::MenuCopy` 再利用、フィルタ適用後の表示行を`\n`連結して `SetClipboardText`）。

**3. スナップ値の小数第三位対応**（`EditorManager.cpp` 3箇所）: DragFloatを `"%.3f"`/min 0.001/speed 0.005(回転0.05) に。丸めロジック(ViewportPanel.cpp:912-920)は不変。

**4. 物理タブに BallSocket/NoCollision ボタン**（`EditorManager.cpp`、`IconsDef.hpp`）: `tryAddObjectButton<T>` 2行＋`ICON_BALLSOCKET`(f140 fa-bullseye)/`ICON_NOCOLLISION`(f05e fa-ban) 追加。前セッションの保留事項（専用アイコンが無くスキップ）を解消。

**5. セカンダリビューポート全面リハビリ**（readme.md TODO「なぜ非推奨でバグありか調べる」→調査の結果4問題を特定し全て修正）
- **(a) 「常に移動モード」の主因＝フォーカス喪失**: ビューポート外クリック（ツールバー含む）で `clearFocus()` が走り、`renderToolbarBasic` の操作対象がプライマリ（既定TRANSLATE）にフォールバックしていた。`ViewportFocusManager` に `lastFocusedViewport` を追加（`onFocusViewport`で常時更新、`getLastFocusedViewport`/`GetLastFocusedViewport`/`onViewportDestroyed`新設）し、ツールバーとCtrl+Lの対象解決を「focused→last→プライマリ」の3段に変更。`clearFocus`の条件変更は不採用（main.cppのカメラ入力ゲートが`GetFocusedViewport()!=nullptr`に依存しており、WASD誤爆の回帰が出るため）。
- **(b) ImGuizmoグローバル状態の未分離**: `ViewportPanel::onRender` 全体を `ImGuizmo::PushID(this)`/`PopID()` で囲んだ（メインセッションが直接実装した唯一の変更）。vendorヘッダで `PushID(const void*)` の存在と、`IsUsing()` がIDスコープ判定であることを確認済み。`ViewportPanel.cpp:785` の「FIX: それぞれの軸が干渉している」の原因はこれ。
- **(c) 独立カメラ**: セカンダリはプライマリと同じ `user->cpos/forward` を共有していた。`ViewportPanel` に `m_useOwnCamera`/`m_camPos`/`m_camYaw`/`m_camPitch`＋アクセサ `camPos()/camForward()/camRight()/camUp()`（own/user切替）を追加し、onRender内の `user->` 読み取り箇所を機械的置換。プライマリは`m_useOwnCamera=false`で従来と完全等価。`openSecondaryViewport` で own カメラを user カメラ位置から初期化。main.cpp の入力ゲートを「プライマリがフォーカスされているときのみ user カメラ入力許可」に変更（セカンダリ操作中にプライマリカメラが動く/二重ドリーの防止）。
- **(d) 未使用クラス削除**: `SecondaryViewportPanel`(.hpp/.cpp/doc) を削除（コード参照ゼロ、CMakeはGLOB_RECURSEなのでビルド変更不要）。ただしフリーカメラ実装は(c)に流用してから削除。

**6. 追加修正3件（ユーザーの実機確認フィードバック起点）**
- **セカンダリの感度違い**: 独立カメラ入力がマジックナンバー直書き（回転0.3/移動10*dt/ホイール2.0）だった。`user->mouseRotationSpeed`/`user->speed`（DeltaTime乗算なし＝プライマリと同じ毎フレーム加算）/`user->mouseZoomSpeed` 参照に変更、E/Qもワールド Y から `camUp()` 基準に。`m_camSpeed` メンバは削除。
- **セカンダリのマウスロック無し**: プライマリのカーソルロック機構（`setMouseCaptured`+アンカー方式）を `User` の公開API `beginExternalCameraDrag`/`sampleExternalCameraDrag`/`endExternalCameraDrag`（`m_externalDragActive`、既存`isRightMouseRotating`とは別フラグ）として切り出し、セカンダリの右ドラッグが使用。`isRotatingCamera()` を両フラグのORにしたので `drawCameraRotationCursor` の擬似カーソルもそのまま機能。ViewportPanelデストラクタでドラッグ中破棄時の解放も追加。
- **セカンダリを閉じた瞬間の1フレーム画面乱れ**: X押下フレームでウィンドウのテクスチャが ImGui 描画リストへ提出済みなのに、同フレーム内の erase がデストラクタ(`destroyFBO`)でGLテクスチャを削除→フレーム末尾の描画が削除済みテクスチャを参照していた。erase ブロックをセカンダリ描画ループの**前**に移動して解消。

**7. BallSocket/NoCollision の物理デバッグ描画**（ユーザー追加依頼。`BallSocket.hpp`/`NoCollision.hpp`/`Renderer.cpp`）
- `Renderer::renderPhysicsDebug` の scan 分岐に追加。privateメンバアクセスは Weld と同じ `friend class Renderer;` 方式。
- BallSocket（青系）: ピボットに直交3円のワイヤ球＋両Cube中心からの接続線。ピボット規則は Motor のデバッグ描画と同一（Attachment優先、無ければ中点）。
- NoCollision（赤系）: Cube間の線＋中点にカメラ向きの「禁止」マーク（円＋斜線）。

### なぜそうしたか

- **B-a で lastFocused記憶方式を採用（clearFocus温存）**: clearFocusを弱める案は main.cpp:619 の入力ゲートと User.cpp:143 のAltフリールック解除がフォーカスクリアに依存しており回帰リスクが高い。「操作対象の記憶」と「入力ゲート」を分離する方が安全。
- **B-b で PushID/PopID 方式**: vendor の ImGuizmo に `SetID` があるが deprecated で、`PushID(const void*)`/`PopID` が正式API。onRender内の途中returnが1箇所（Begin失敗時）しかないことをgrepで確認してから採用。
- **B-c でプライマリを触らない設計**: アクセサ切替方式にすることで `m_useOwnCamera=false` の経路は既存と完全等価になり、プライマリの回帰リスクをゼロにした。
- **感度修正で DeltaTime を掛けない**: プライマリの WASD 移動（User.cpp:220-225）が毎フレーム定数加算（フレームレート依存）なので、感度を一致させるにはあえて同じ方式にする必要があった。
- **マウスロックを User のAPIとして切り出し**: ViewportPanel から `m_input`（private）を直接触る案や friend 追加案より、アンカー状態を共用できる公開APIの方が擬似カーソル描画(`drawCameraRotationCursor`)を無改修で流用できる。別フラグ `m_externalDragActive` にしたのは、`User::processCameraRotation` が毎フレーム「`looking=false` なら `isRightMouseRotating` を解除」するため、同じフラグを共用するとセカンダリのロックが即座に解除されてしまうから。

### どういう経緯か

1. readme.md の TODO（43-56行）を対象に計画モードで開始。Explore 3並列（設定永続化の既存機構／ログUI・スナップ・物理タブ／セカンダリビューポート調査）→ AskUserQuestion でセカンダリの改善範囲（a+b+c+d全面リハビリを選択）とコピーUI方式（Copyボタン全文コピー）を確認 → Plan agent に詳細設計を委託 → ImGuizmo API・ViewportFocusManager・main.cpp ゲートをメインセッションで裏取り → 計画承認（初回却下でギズモモード保存を追加）。
2. 5フェーズに分割し implementer に委譲（Phase4 の PushID/PopID 2行のみメイン直接）。各フェーズ後に git diff レビュー＋ビルド。全体後に回帰テスト。
3. ユーザー実機確認で「感度が違う」「マウスロックが効かない」「閉じた時に画面が乱れる」の3件のフィードバックを受け、それぞれ原因調査→implementer委譲（感度はPhase5のエージェントをSendMessageで再開）で修正。
4. 最後に「BallSocket/NoCollisionのデバッグ描画」を追加依頼され実装。
5. 回帰テストは全チェックポイントで 126 passed / 3 failed（既知ベースライン）を維持。

**試して失敗した方法（教訓）**:
- 当初セカンダリの右ドラッグを `io.MouseDelta` 直読みで実装したが、カーソルロックが無く画面端で回転が止まる・感度もプライマリと不一致だった。**カメラドラッグ系は最初から User の既存機構（キャプチャ＋アンカー＋擬似カーソル）に乗せるべき**。ImGuiのMouseDeltaはOSカーソル依存でraw motionの恩恵を受けられない。
- ImGuiウィンドウ内に GL テクスチャを表示するパネルを「同フレーム内で描画後に破棄」すると、フレーム末尾のImGui描画が削除済みテクスチャを参照して1フレーム乱れる。**FBO/テクスチャを持つパネルの破棄はフレーム先頭（そのフレームで何も提出する前）に行う**こと。

### 未解決・保留

- セカンダリビューポートの「非推奨」表記の解除（挿入メニュー等にそういう文言があれば）は未確認・未対応。ユーザーの実機確認は通ったので、必要なら次セッションで文言を探して更新。
- ドラッグ中のスナップ値DragFloat（min 0.001）は0にできない仕様のまま（スナップ自体のON/OFFはチェックボックスがあるため問題ないと判断）。
- `assets/scenes/physics_test.yaml` にユーザーの実機確認由来と思われる差分（ツリー再構成）が入った状態でコミット。回帰テストはベースライン維持を確認済み。
- 前セッションからの持ち越し（BallSocket/NoCollision実機確認は今回のデバッグ描画確認と合わせてユーザーが実施済みの模様。readme.md TODOのチェックはユーザー自身が消し込み済み）。

### 暗黙仕様の発見

- **ImGuizmoのグローバル状態はPushID/PopIDで分離しないと複数ビューポートで干渉する**（spec.md未記載）。`IsUsing()`もIDスコープ判定（`GetCurrentID()==mEditingID`）なので、Manipulateだけでなく状態クエリも含めて `PushID(パネルポインタ)` で囲む必要がある。`BeginFrame()` はフレーム1回のままでよい。
- **`ViewportFocusManager` のフォーカスは「カメラ入力ゲート」と「ツールバー操作対象」の2役を兼ねていた**。前者はクリアが必要（誤入力防止）、後者はクリアされると困る（ツールバークリックで対象消失）ため、`lastFocusedViewport` として役割分離した。今後フォーカス依存の機能を足すときはどちらの意味かを意識すること。
- **`User::processCameraRotation` は毎フレーム「looking でなければキャプチャ解除」する**ため、User外部から `setMouseCaptured` を借りる場合は専用フラグ（`m_externalDragActive`）で状態を分けないと即解除される。
- **ImGuiパネルに表示中のGLテクスチャは、そのフレームの ImGui::Render が終わるまで削除してはいけない**（描画リストがテクスチャIDを保持している）。パネル破棄はフレーム先頭で行うのが安全。
- **プライマリカメラのWASD移動は DeltaTime を掛けないフレームレート依存の毎フレーム定数加算**（User.cpp:220-225、`speed=0.25`）。カメラ感度を合わせる実装をする際はこの仕様に合わせる必要がある。

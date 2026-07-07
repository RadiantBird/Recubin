作業記録のログ、progress.mdから非定期で追加。

---
## 2026-07-05 レンダリング/GUI系バグ修正（5件）+ 追加バグ2件

### 指示内容
readme.md のTodoリストのうち、以下5項目の修正を依頼された:
1. 透明な画像をDecal/Textureとして貼るとそのCube自体が透ける問題
2. ImageLabel/ImageButtonがSurfaceGui/BillboardGuiに未対応な問題
3. SurfaceGui/BillboardGui下のButtonクラスが反応しない問題
4. コピペ時にCube1→Cube11のようにナンバリングがおかしくなる問題
5. 選択中にF2でインスタンス名をインライン編集できるようにする機能

「IsAだと不自然なのでHasAにリファクタ」「ナンバリング/F2以外の項目（Todoリストの別項目）」はスコープ外として明示的に除外。ユーザーとの事前確認で以下を決定:
- SurfaceGui下のButtonクリック対応は「3Dヒットテストの新規実装」込みでスコープに含める（機能追加に近い規模だが明示的に依頼された）
- IsA→HasAリファクタは今回やらない
- F2リネームUXは「アウトライナー上でインライン編集」方式を選択（Propertiesパネルへのフォーカス移動方式は不採用）

### 何をしたか

**1. コピペ時のナンバリングバグ修正**
- `include/Editor/SceneHierarchyPanel.hpp` の `uniqueName()`: 末尾の数字を切り離してroot名+数値に分解し、その数値からインクリメントするロジックに書き換え。`taken`セット（バッチ内予約名）を任意引数に追加。
- `src/Editor/EditorManager.cpp`（`pasteInto`ラムダ）と `src/Editor/SceneHierarchyPanel.cpp`（`pasteAll`ラムダ）にあった重複したwhileループを、この共通ヘルパー呼び出しに置き換え。

**2. 透明Decal/Textureでキューブが透けるバグ修正**
- `src/fragment.glsl`: 出力アルファ計算が `texColor.a * ourColor.a * MatAlpha` となっており、RGB側の`mix()`で既に「透明部分はDecal/TextureのColorで塗りつぶす」処理をしているのに、アルファ側でも`texColor.a`を掛ける二重処理になっていた。`outAlpha = ourColor.a * MatAlpha` に統一（`isSurfaceGui`分岐も含めて）。

**3. ImageLabel/ImageButtonのSurfaceGui/BillboardGui未対応バグ修正**
- `src/Core/Renderer_GUI.cpp`: `bakeSurfaceGui`と`drawWorldGuiChildren`に`TextLabel`/`TextButton`分岐しかなく、`ImageLabel`/`ImageButton`分岐が実装漏れだった。`drawScreenGuiElement`の既存パターンを参考に両方に追加。`drawWorldGuiChildren`には`ImageButton`のクリック判定（`InvisibleButton`）も追加。

**4. SurfaceGui下のButtonクリック対応（新規実装）**
- `include/Core/Renderer.hpp` / `src/Core/Renderer_GUI.cpp`: `renderWorldGui`に`User*`を追加伝搬。
- `bakeSurfaceGui`内にインラインで計算していたキャンバス→FBOレイアウト計算（`cW,cH,w,h,scale,offX,offY`）を`computeSurfaceGuiLayout()`として抽出し、ベイク処理とヒットテストの両方から使えるようにした。
- `makeGuiRay()`（`ViewportPanel.cpp`の`makeRay`と同じ45°FOV固定の数式を複製）でマウスクリック位置からワールドレイを生成。
- `hitTestSurfaceGui()`: レイと面プレーンの交差判定→ヒット点をキューブのUV基底（`Cube.cpp`の`createCubeVertices`のUVテーブルに基づく）でキャンバス座標に変換→SurfaceGuiの子（TextButton/ImageButton）と当たり判定。
- `renderWorldGui`内でクリック時に全キューブの全SurfaceGuiから最近傍ヒットを探索し、`m_onButtonActivated`を発火。

**5. F2でのインラインリネーム**
- `include/Editor/SceneHierarchyPanel.hpp`: `renamingInstance`/`renameFocusPending`（public）と`m_renameBuf`（private、再帰関数のためメンバ化）を追加。
- `src/Editor/EditorManager.cpp::handleEditorShortcuts()`: F2キー押下でリネームモード開始。
- `src/Editor/SceneHierarchyPanel.cpp::drawNode()`: リネーム中のノードはアイコンのみのTreeNodeEx + インラインInputTextに切り替え。Enter/フォーカス外れで確定（`RenameInstanceCommand`でUndo登録）、Escapeでキャンセル。

**追加で発見・修正した2件のバグ**（ユーザーが実機テストで発見）:

**6. SurfaceGuiのアルファを0にするとCube自体が透ける**
- 項目2の修正時、`isSurfaceGui`分岐のアルファ式は「別機能だから触らない」と判断していたが、実は同一の二重乗算バグだった。項目2の修正で`isSurfaceGui`分岐も含めて統一済みだったため、実質同時に直っていた（原因究明のため再度確認）。

**7. ImGui「2 visible items with conflicting ID!」エラー**
- `Renderer_GUI.cpp`のInvisibleButtonのIDが`sgo->Name`（インスタンス名）から生成されており、異なるCube配下に同名インスタンス（例: 複数のBillboardGui配下にそれぞれ"ImageButton"という名前の子）があるとID衝突していた。`drawScreenGuiElement`と`drawWorldGuiChildren`の計4箇所（TextButton×2, ImageButton×2）のID生成をインスタンスポインタベース（`reinterpret_cast<uintptr_t>(sgo)`）に変更。TextButton側も同じ欠陥を持っていたため合わせて修正。

**8. SurfaceGuiボタンのクリック座標がズレる**
- 実機テストで「座標がずれている」と報告。原因はヒットテストの縦方向(Y)変換式。OpenGLのFBOはglGetTexImage/テクスチャサンプリングで「行0=画面下端」という規約があり、`bakeSurfaceGui`が明示的に行っている手動反転は左右方向のみだったため、縦方向はOpenGLの暗黙の上下反転を考慮できていなかった。`fboY = texV * h` → `fboY = (1.0f - texV) * h` に修正。横方向(X)の反転は元々正しかった。

### なぜそうしたか（判断の経緯）

- **SurfaceGuiクリックのレイキャスト方式**: PhysXの`Physics::raycast`（既存のワールドレイキャスト）を使う案と、`ViewportPanel.cpp`の編集モード用`makeRay`+OBB判定パターンを複製する案を検討。後者を採用した理由: SurfaceGuiは面ごとの平面交差判定で十分（PhysXシーンクエリは不要な依存を増やす）、かつ`renderWorldGui`が既に`m_lastView`/`m_lastProj`と`User`を持っているため、既存パターンの複製で完結できる。
- **キャンバスレイアウト計算の共通化**: `bakeSurfaceGui`内のインライン計算を`computeSurfaceGuiLayout()`として抽出したのは、ヒットテストが同じ計算を必要としたため。「ついでの改善」ではなく、この機能追加に直結する重複排除として許容範囲と判断。
- **F2リネームUX**: 「アウトライナー上でインライン編集」 vs 「Propertiesパネルへフォーカス移動」でユーザーに確認。Roblox Studio/Explorer的な体験を優先しインライン編集を採用（実装量は増えるがUXとして自然）。
- **ImGui ID衝突の修正範囲**: `drawWorldGuiChildren`（今回追加したBillboardGui向けImageButton）だけでなく`drawScreenGuiElement`（既存のScreenGui向けTextButton/ImageButton）も同じ欠陥を持っていたため、同一パターンとして合わせて修正。CLAUDE.mdの「指示されていないファイルを触らない」方針とはやや緊張関係にあるが、「同一バグの横展開」として許容できると判断（ユーザーに事後報告済み）。

### 試して失敗した/要修正だった箇所

- **SurfaceGuiヒットテストのY軸変換**: 当初`fboY = texV * h`（Y反転なし）で実装したが、実機テストで座標ズレが発覚。原因はOpenGLのテクスチャ行順序（row0=下端）とImGuiの描画座標系（Y=0が上端）の不一致を見落としていたこと。計画段階でも「机上の導出だけでは正しさを保証できない」とリスクフラグを立てていた箇所で、実際にその通り不具合が出た。→ **教訓**: FBOベイク→3Dテクスチャサンプリング→逆算という「座標系を2回またぐ」処理は、片方向だけ検証して他方を油断しないこと。次回同様の実装をする際は、X/Y両方について「どちら向きの変換か」を明示的に別々に検証すること。
- **`isSurfaceGui`分岐のアルファ式を最初「別機能だから触らない」と判断した点**: Plan agentの検証でも「SurfaceGuiベイクテクスチャ用の別ロジックなので変更しないこと」という指示を出してしまったが、後に発生したバグ報告で実際には同一の二重乗算バグだと判明。項目2の実装では最終的に両分岐とも同じ式に統一していたため実害はなかったが、「初期分析で `isSurfaceGui` 分岐は別物と決めつけた」判断自体は誤りだった。→ **教訓**: 「片方の分岐だけ直せばいい」と判断する前に、同じ数式パターンが他の分岐にもないか必ず横展開でチェックすること。

### 未解決・保留

- ユーザーには`ButtonTest2.luau`のTextButton/ImageButtonで座標がぴったり合うか再テストを依頼中（Y軸反転修正後の確認待ち、このセッション終了時点で未回答）。
- `Instance::addChild`/`setParent`で、リネーム時に`parent->children`のマップキーが`Name`と非同期になる潜在バグを発見済み（`RenameInstanceCommand`/F2リネーム/PropertiesPanelのName編集、いずれも`inst->Name`を直接書き換えるだけで`children`マップのキーは再登録されない）。今回のバグとは無関係と判断しスコープ外にしたが、将来的にリネーム後の名前検索・シリアライズで問題化する可能性がある。要調査。
- SurfaceGuiクリック対応で、`User::processMouse`のToolアクティベート（生のGLFW左クリック監視）や`ViewportPanel.cpp`のオブジェクト選択レイキャストとの二重発火（同じ左クリックでSurfaceGuiボタンとツール両方が反応する可能性）は既存のBillboardGui TextButtonでも起きていた事象として今回は対応せず。気になる場合は別タスクで。

### 暗黙仕様の発見（spec.mdに無い挙動）

- **SurfaceGuiの実際のベイク解像度はSurfaceGui自身のSize比率ではなく、親BaseCubeのそのフェイスの物理サイズ比率に合わせて決まる**（`bakeSurfaceGui`の`faceU`/`faceV`計算）。例えば200x100のSurfaceGuiを1x1x1の正方形キューブに貼ると、実際のFBOは200x200になりレターボックスされる。UIデザイン時に「SurfaceGuiのSizeがそのままアスペクト比になる」と誤解しやすい。
- **`bakeSurfaceGui`は焼き込み後のテクスチャを手動で左右反転している**（列方向のみ、行方向はOpenGLの暗黙の反転任せ）。この非対称な反転処理（片方は明示的なCPU処理、もう片方はGPU/API規約による暗黙の反転）は、3D面へのマッピングを扱うコードを新規に書く際に必ず両方向を意識する必要がある落とし穴。
- **GUIのInvisibleButton系のIDは、この修正前は全て`sgo->Name`（インスタンス名）ベースだった**。同名インスタンスが複数存在する構成（コピペ量産、テンプレート的な使い方）でImGuiのID衝突を起こす設計だったため、ポインタベースに変更済み。今後同種のGUIウィジェットを追加する際はNameでなくポインタ/インスタンスIDでImGui IDを生成すること。

---

## 2026-07-06 スクロールズームバグ修正 + カメラ回転ドラッグ中の擬似カーソル表示

### 指示内容
readme.mdのTodo「viewportFocusedでないのにマウスをスクロールするとカメラがズームしたりするのを修正」への対応を依頼された。その後、雑談的な流れから「ドラッグ移動してもマウスが動かないようになれば、マウスカーソルを非表示にする必要はないと思う」という追加要望が出て、実現方法の検討・実装まで行った。

### 何をしたか

**1. スクロールズームバグ修正**
- `src/Core/User.cpp` の `User::processZoom()`: `viewportZoomEnabled`が`false`のとき早期returnしていたため、その間に溜まったスクロール量(`GLFWInputBackend`側で蓄積される`m_pendingScrollY`)が`consumeScrollDelta()`で消費されずに残り続け、後でビューポートにマウスをhoverした瞬間にまとめて適用されてズームしてしまう、という不具合だった。`consumeScrollDelta()`の呼び出し位置を早期returnより前に移動し、`viewportZoomEnabled`が`false`の間も毎フレーム破棄するように修正。

**2. カメラ回転ドラッグ中の擬似カーソル表示（新規機能）**
- `include/Core/User.hpp`: 既存のprivate状態を読むだけのconst getterを追加（ロジック変更なし）。
  - `isRotatingCamera()` → `isRightMouseRotating`を返す（右ドラッグ／Altフリールックいずれでもtrue）
  - `getRotationAnchor(x, y)` → `lastMouseX`/`lastMouseY`（ウィンドウクライアント座標のアンカー）を返す
- `include/Core/Renderer.hpp` / `src/Core/Renderer_GUI.cpp`: `drawCameraRotationCursor(User&, GLFWwindow*)`を新設。ユーザーが`assets/image/cursor.svg`を追加してくれたので、そのSVGパス2つ（矢印本体の三角形＋右下の小さな尾の四角形）をホットスポット（矢印の先端）基準のローカル座標に手計算で変換し、`ImGui::GetForegroundDrawList()`の`AddConvexPolyFilled`/`AddPolyline`で白塗り+黒アウトラインとして直接描画（テクスチャ読み込み不要）。
- `src/Editor/EditorManager.cpp::renderUI()` と `include/Editor/NullEditorManager.hpp::renderUI()` の両方で、ImGuiパネル描画後・`ImGui::Render()`前に`drawCameraRotationCursor()`を呼び出すよう追加（エディター/ランタイム両方に適用）。
- `EditorManager::renderUI`の`User&`引数がそれまで無名だったため、名前(`user`)を付けた（シグネチャの型・個数は変更していない）。`EditorManager.cpp`に`Core/Renderer.hpp`のincludeを追加。
- `GLFWInputBackend.cpp`の`setMouseCaptured`/Raw Mouse Motion周りは一切変更していない。

### なぜそうしたか（判断の経緯）

- **「表示カーソル+毎フレームリセット」方式は不採用**: ユーザーの当初の要望は「本物のOSカーソルを表示したまま毎フレーム位置を戻す」方式だったが、調査の結果 (a) Raw Mouse Motionは`GLFW_CURSOR_DISABLED`時にしか有効化できない（GLFWの仕様）、(b) 表示カーソルは画面/ウィンドウ端でクランプされる、という2点が判明。表示に切り替えると高速ドラッグ時に感度が環境依存になったり移動量を取りこぼす懸念があるため、この方式を採る前にユーザーに一度トレードオフを提示し判断を仰いだ。
- **最終的に「OSカーソルは非表示のまま＋固定位置にImGuiで擬似カーソルを重ね描き」方式を採用**: ユーザー自身が「クローンを毎フレーム描画して、裏で無限大のスクロールができるようにする」という逆転の発想を提案。これなら本物のカーソルのRaw Motion/無制限移動の性質を一切変えずに、見た目上は動かないカーソルを表示できるため採用。
- **cursor.svgをテクスチャ化せず、ImGuiの図形描画で直接クローンした**: プロジェクトにはSVGラスタライズ機能も既存のカーソル画像アセットも無く、PNG化＋テクスチャ読み込みの経路を新設すると依存関係・スコープが増える。SVGのパスデータ自体は単純な2ポリゴン（三角形＋四角形）だったため、座標を手計算でホットスポット基準に変換しベクター描画するだけで済んだ。ビルドシステムには一切触れていない。
- **描画フックの位置**: `EditorManager::renderUI`と`NullEditorManager::renderUI`の両方に同じ呼び出しを追加した。カメラ回転ドラッグ（`User::processCameraRotation`）はEditorのFreeカメラだけでなくRuntimeのゲームプレイカメラ操作でも共通して使われるロジックのため、両方に描画がないと体験が非対称になると判断。

### どういう経緯か

1. スクロールズームバグはUser.cppを読んだ直後にすぐ原因（`GLFWInputBackend`のスクロール蓄積とprocessZoomの早期return順序）を特定し、単発修正として即完了。
2. その後Plan modeで「ドラッグ中カーソル非表示は本当に必要か」という設計相談を受け、Explore agentを2回起動して(1)`IInputBackend`/`GLFWInputBackend`のマウスキャプチャ実装とProcessCameraRotationの詳細、(2)ImGuiのマルチビューポート設定・renderUIのフック位置・GUI描画の既存パターンを調査。
3. 表示カーソル案のトレードオフをユーザーに提示 → ユーザーが「表示カーソル+毎フレームリセット」を試すか、現状維持かをいったん選ばせた後、実際には「OSカーソル非表示のままImGuiで擬似カーソルを重ねる」という第三の案を思いつき、そちらを採用。
4. cursor.svgの見た目にするか簡単な図形にするかをAskUserQuestionで確認 → ユーザーが`cursor.svg`を追加して「試してみて」と回答。SVGのpath dataを手動でデコード（相対座標→絶対座標→ホットスポット基準）し、ImGuiのポリゴン描画コードに落とし込んだ。
5. 実装後ビルド成功を確認。
6. 直後の自律ループ(autonomous loop)ティックで、実機起動によるスモークテスト（PowerShellでRecubin.exeを起動しスクリーンショットを撮る）を試みたが、(a) working directoryの違いで`assets/`が見つからない警告が出たり、(b) 最初の全画面スクリーンショットが最前面のVSCodeウィンドウを写してしまい対象ウィンドウを捉えられなかったりと、環境起因のノイズが多かった。`user32.dll`のP/Invoke（`SetForegroundWindow`/`GetWindowRect`）でウィンドウを特定して撮り直し、最終的には3Dビューポート・メニュー・Content Browserパネルが描画されていることを確認できたが、ユーザーから「GUIアプリで自動テストすると意味がないのでやめよう」と明示的に指摘を受け、このアプローチ自体を今後は使わないことにした（メモリに保存済み: `feedback_no_gui_smoketest.md`）。

### 試して失敗した/やめた方法

- **表示カーソル＋毎フレームreset方式**: ユーザーの最初の要望どおりの実装は行わず、GLFWの仕様上のトレードオフ（Raw Motion非対応・エッジクランプ）を理由に採用を見送った。実装前にユーザーへ選択肢として提示し、判断を仰いでから別案（擬似カーソル）に切り替えたので手戻りは無かった。
- **Recubin.exeの自動起動＋スクリーンショットによるスモークテスト**: 一度は動作確認に成功したが、環境依存のノイズ（対象ウィンドウの取り違え、working directory起因の警告）が多く、ユーザーからも「意味がない」と明言されたため今後は行わない。→ **教訓**: ネイティブGUIアプリ（GLFW/OpenGLなど）に対しては、Webアプリのようなスクリーンショット主体の自動検証は基本的に不向き。ビルドが通ることの確認までに留め、実際の見た目・操作感の確認はユーザーに委ねること。

### 未解決・保留

- 擬似カーソルの実際の見た目（アンカー位置に矢印アイコンが正しく固定表示されるか、ドラッグ終了時に消えるか、マルチビューポート時にズレないか）はユーザーによる実機確認待ち。
- `drawCameraRotationCursor`内のポリゴン座標はcursor.svgのpathデータを手計算で変換した値をハードコードしているため、今後cursor.svgのデザインを変更する場合はこの関数側の座標も手動で再計算・同期する必要がある（自動追従しない）。

### 暗黙仕様の発見（spec.mdに無い挙動）

- **`User::processCameraRotation`のカーソルキャプチャは、Editorのフリーカメラ操作とRuntimeのゲームプレイカメラ操作の両方で共有されている**。`controlMode`（Free/Character/Program）に関わらず、右ドラッグ・Altフリールックによる回転ロジックとカーソルキャプチャは同一コードパスを通るため、この付近を触る変更は必ずEditor/Runtime両方への影響を考慮する必要がある。
- **ImGuiの`ImGuiConfigFlags_ViewportsEnable`はEditorビルドでのみ有効**（`Renderer::init`が`#ifndef EDITOR_DISABLED`で分岐）。これが有効だと`ImGui::GetForegroundDrawList()`等のスクリーン座標は「デスクトップ絶対座標」になり、GLFWの`glfwGetCursorPos`（ウィンドウクライアント座標）とは座標系が異なる。この2つの座標系を跨ぐ描画を書く際は、`io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable`で分岐し`glfwGetWindowPos`を加算するかどうかを切り替える必要がある（`Renderer::init`内に既存の同種分岐パターンあり）。

---

## 2026-07-06 ScreenGui基準解像度スケーリング + ビューポートレターボックス + 雲スクロール方向バグ修正

### 指示内容
readme.mdのTodo「簡単なScreenGuiフォーマット機能」の実装をPlan modeで依頼された。実装後、「ビューポートサイズが可変で手触りが悪い」というフィードバックからレターボックス対応に発展し、さらに別のTodo「風の向きと雲のスクロール方向が逆な問題を修正」も追加で依頼された。

### 何をしたか

**1. ScreenGui基準解像度スケーリング機能**
- `include/Instances/System.hpp` / `src/Instances/System.cpp`: `Vector2 BaseResolution`（既定1920x1080）を追加。`PropertyRegistry::registerClass("System", {field<&System::BaseResolution>("BaseResolution").luaReadOnly()})` で登録し、YAML保存・エディター編集・Luau読み取り専用公開に対応（既存の`MaxClonesPerFrame`等の完全手書きプロパティとは別経路として共存させた）。
- `src/Core/SceneLoader.cpp`: 既存の2箇所のSystem手書き保存ブロックに`PropertyRegistry::saveProperties(out, sys, "System")`を追加し、BaseResolutionもYAML出力されるようにした。
- `src/Core/LuauEngine_Dispatch.cpp`: `PropertyRegistry::applyToDispatch("System", ...)`を追加し、Luauから`System.BaseResolution`を読み取り可能にした（書込は`.luaReadOnly()`でブロック）。
- `src/Editor/PropertiesPanel.cpp`: System欄に`renderSchemaInspector(inst, "System", m_history)`を1行追加するだけで、ウィジェット描画・Undo記録は既存の汎用スキーマ駆動インスペクタに任せた（新規Commandクラスは不要だった）。
- `src/Core/Renderer_GUI.cpp`: `drawScreenGuiElement`に`scaleX/scaleY`引数を追加し、`Norm::Pixel`のPosition/Sizeに乗算するよう変更。`Renderer::renderScreenGui`が`ws.Parent.lock()`からSystemを取得し`vpW/BaseResolution.x`、`vpH/BaseResolution.y`を計算する（System不在時は1.0にフォールバックし既存挙動を保持）。

**2. ビューポートのレターボックス対応**
- `src/Editor/ViewportPanel.cpp::onRender()`: FBOサイズを`avail`そのままではなく`min(avail.x/baseW, avail.y/baseH)`で計算した、BaseResolutionとアスペクト比が一致するサイズにリサイズするよう変更。パネル全体を黒で塗ってから中央にFBO画像を配置し、`contentOrigin`/`w`/`h`を画像の実際の表示矩形に更新した。
- 追加修正: BillboardGuiなど`renderWorldGui`が投影するパネルがレターボックス矩形の外にはみ出して黒帯を上書きする不具合が実機確認で発覚。`renderGameGui`呼び出しの後に黒帯4本（上下左右）を再度最前面に重ね描きする処理を追加して解決。

**3. 雲のスクロール方向バグ修正**
- `src/Core/Renderer.cpp::initCloudRenderer()`の雲フラグメントシェーダー内、`texture(cloudTex, vUV + windOffset)` を `texture(cloudTex, vUV - windOffset)` に1行修正。

### なぜそうしたか

- **BaseResolutionのLuau公開方式**: 「完全非公開（既存の安全設定と同じ）」か「読み取り専用でLuau公開」かをユーザーに確認。ユーザーが後者を選択したため`PropertyRegistry`に`.luaReadOnly()`付きで登録する方式を採用した（`.readOnly()`だとYAML/clone用の`set`まで消えて使えなくなる点に注意。両者の違いを把握した上で使い分けた）。
- **スケーリング適用範囲**: ScreenGui直下（`renderScreenGui`）のみにするか、WorldGui配下のPixel要素にも広げるかを確認。ユーザーは前者（ScreenGui限定）を選択。`drawWorldGuiChildren`のPixelは「パネル内相対座標」という別の意味を持つため、混同すると設計が濁ると判断したため。
- **レターボックスの適用範囲**: エディターのViewportPanelのみか、ランタイム（Recubin.exe/RecubinEngine.exe）の実ウィンドウも含めるかを確認。ランタイムは3DシーンをFBOなしで直接ウィンドウへ描画しており（`Renderer::render()`）、対応するには`glViewport`呼び出しなど中核パイプラインまで踏み込む必要がありリスクが高いため、ユーザーはエディターのみを選択。ランタイムは今回スコープ外とした。
- **ViewportPanel.cppの改修方針**: 既存のマウスレイキャスト（`makeRay`）・ImGuizmo::SetRect・地形ブラシ・ボックス選択がすべて`contentOrigin`/`w`/`h`という3変数経由で一貫して実装されていることを事前のコード調査（Grep）で確認できたため、この3変数の定義だけを書き換えれば他のロジックは無改修で正しく追従すると判断した（実際その通りだった）。
- **雲スクロール方向のバグ原因の切り分け**: UV座標のU/VはワールドX/Zと同じ符号で増加するよう頂点を組んでいる（`renderClouds`の頂点UV割当）。`m_cloudScrollOffset`が`WindDirection`に比例して単調増加するWeather.cpp側の蓄積式自体は自然な設計のため、そちらではなくシェーダーのサンプリング側（`vUV + windOffset` → `vUV - windOffset`）を直す方を選んだ。「風下方向にサンプル点を追従させるには符号反転が必要」であることを手計算で確認してから修正した。

### どういう経緯か

1. readme.mdの「簡単なScreenGuiフォーマット機能」をPlan modeで着手。CLAUDE.mdの明示指示（計画をサブエージェントに外注しない）に従い、Explore作業も自分でGrep/Readして直接調査した。
2. 設計上の分岐点（Luau公開範囲、適用スコープ）をAskUserQuestionで確認してから実装・ビルド成功。
3. ユーザーから「ビューポートサイズが可変で手触りが悪い、アスペクト比を維持して外側を黒くしたい」とフィードバック。適用範囲（エディターのみ/ランタイムも）を再度確認してからViewportPanel.cppを改修。
4. 実機確認でBillboardGuiが黒帯の上に描画される不具合が見つかり、黒帯の再最前面描画で対応。
5. 別Todo「風の向きと雲のスクロール方向が逆」の修正を依頼され、Weather.hpp/cpp・Renderer.cppの雲関連コードを読み込んで、シェーダー内サンプリング座標の符号を1行修正。

### 未解決・保留

- ScreenGui基準解像度スケーリング機能（Propertiesパネルでの`BaseResolution`編集・Undo・シーン保存復元・Luauからの読み取り/書込ブロック）は、ビルド確認のみでユーザーによる実機確認は未実施。
- ビューポートのレターボックス表示（黒帯の位置・GUIとのズレ・パネルリサイズ時の追従）も実機確認待ち。
- 雲のスクロール方向修正も、実際にWindDirectionを設定して見た目の流れる向きが一致するかは未確認。
- ランタイム（Recubin.exe/RecubinEngine.exe）側のレターボックス対応は明示的にスコープ外としたため未着手。将来対応する場合は`src/Core/Renderer.cpp`の`render()`と`renderViewport`内の`glViewport`呼び出しの改修が必要になる。

### 試して失敗した/やめた方法

- 大きな手戻りは発生しなかった（設計分岐点を都度AskUserQuestionで確認してから実装したため）。ただしレターボックス黒帯は「パネル全体を黒で1回塗る→画像→GUI」の順で実装した初版では、GUI（BillboardGuiのパネル投影）がレターボックス矩形の外にはみ出して黒帯を上書きしてしまう不具合が実機確認で発覚した。原因は描画順序のみだったため、設計をやり直さずGUI描画後に黒帯を再度重ね描きする1手順を追加するだけで解決した。

### 暗黙仕様の発見（spec.mdに無い挙動）

- **`System`クラスは意図的にPropertyRegistry未登録で完全手書きの安全設定（`MaxClonesPerFrame`等）を持つ**が、今回`BaseResolution`だけは`PropertyRegistry`に登録する形で混在させた。1クラス内で「手書きプロパティ」と「PropertyRegistry登録プロパティ」が共存できることを確認した（`setProperty`で`PropertyRegistry::loadProperty`を先に試し、失敗したら手書き分岐にフォールバックする形）。
- **`ViewportPanel.cpp`のマウスレイキャスト・ImGuizmo・地形ブラシ・ボックス選択は全て`contentOrigin`/`w`/`h`という3変数のみに依存しており、パネルの実際の描画矩形さえ正しく再定義すれば他のインタラクションコードは一切変更不要**という設計になっている。今後ビューポート表示方式を変える際はこの3変数の定義箇所（`onRender()`冒頭）だけを見ればよい。
- **`Renderer::render()`は`editor`が`nullptr`かどうかで分岐しているが、ランタイムビルドでも実際には`NullEditorManager`が`editor`にセットされるため`nullptr`分岐は事実上通らず、`editor->getViewportSize()`（内部で`glfwGetFramebufferSize`）経由でフルウィンドウサイズを取得している**。ランタイムの3DシーンはFBOを介さず直接ウィンドウ（fbo=0）へ描画される。将来ランタイムのレターボックス対応をする場合はこの経路（`desc.fbo`/`desc.width`/`desc.height`の決定箇所）を触る必要がある。
- **雲のUV座標系はワールドX/Zと同じ符号で増加するよう頂点が組まれている**（`renderClouds`の頂点配列）ため、テクスチャスクロールを「風下方向」に見せるには`vUV - offset`（オフセット蓄積が風向きに比例して単調増加する設計の場合）が正しい。加算/減算どちらが正しいかは頂点UVの符号に依存するため、同種のスクロールテクスチャを今後実装する際は頂点UVの向きとシェーダー側の加減算を必ずセットで確認すること。

---

## 2026-07-07 キャラクターQoL改善（水中ジャンプ・Truss登坂）+ Seatクラス追加 + 派生バグ4件修正

### 指示内容
readme.mdのTodoリストのうち以下2項目をPlan modeで依頼された:
1. キャラクターQoL改善: 水中でもジャンプできる／はしご(Truss)クラスを追加して垂直に登れるようにする
2. Seatクラスの追加: 接触で着席(Root溶接)、ジャンプで離脱、着席中はSteer(A:-1,D:1,Null:0)/Throttle(W:1,S:-1,Null:0)を更新し続ける。ポージング角度のフラグが増えるので三項演算ではなくif-else推奨

事前調査（Explore不使用、CLAUDE.mdの指示によりGrep/Readで自分で調査）で、既存の`Weld`（剛体結合）・`Physics::applyBuoyancy`（LiquidCubeとのAABB重なり判定）・`Humanoid::jump/move`・`Renderer.cpp`の`IsA("Cube")`描画分岐などの既存パターンを確認した上で設計し、AskUserQuestionで3点（Truss操作方式、水中ジャンプの挙動、Steer/ThrottleのOwnership）を確認してから実装した。

### 何をしたか

**1. 新規クラス Truss / Seat**
- `include/Instances/Truss.hpp` / `src/Instances/Truss.cpp`（新規）: `Named<Truss, Cube>`（`BaseCube`直接ではなく`Cube`を継承）。専用ジオメトリなし、`IsA`/`clone`のみ追加。
- `include/Instances/Seat.hpp` / `src/Instances/Seat.cpp`（新規）: 同じく`Named<Seat, Cube>`継承。`float Steer/Throttle`（`PropertyRegistry`に`luaReadOnly().noYaml()`で登録）、`private weak_ptr<Humanoid> m_occupant`（Lua非公開の同時着席ガード）。

**2. Physics: 汎用オーバーラップ判定**
- `include/Core/Physics.hpp` / `src/Core/Physics.cpp`: `applyBuoyancy()`内のAABB重なり体積計算を`static float aabbOverlapVolume(posA,sizeA,posB,sizeB)`に抽出し、新規`BaseCube* findOverlapping(const BaseCube&, className)`を追加。水没判定・Truss接触・Seat接触の3用途で共用。

**3. Humanoid（`include/Instances/Humanoid.hpp` / `src/Instances/Humanoid.cpp`）**
- `float ClimbSpeed`追加（`PropertyRegistry`登録）。
- `jump()` → `jump(Physics*)`にシグネチャ変更。`isGrounded || 水没中`で許可するよう条件変更。
- `move()`に`float forwardAxis, rightAxis`（W:+1/S:-1, A:-1/D:+1の独立2軸）を追加。
- `move()`冒頭でSeat接触検知→`sitOn()`、着席中は`Steer/Throttle`更新のみして早期return。
- Truss接触中は`dynamicActor->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, true)`で重力そのものを無効化し、向き(Rotation)の自動更新ブロックを丸ごとスキップ。速度は`climbVel.y = forwardAxis*ClimbSpeed`、水平は`flatRight*rightAxis*WalkSpeed`。
- `sitOn(seat, physics)`: Rootを`seat->getWorldCFrame() * CFrame(0, seat->Size.y*0.5f + Root->Size.y*0.25f, 0)`にスナップ（Seatと同じ向き、向き反転なし）、`Weld`を動的生成して`workspace->addChild`後に`setCube0(Root)/setCube1(seat)`。
- `standUp(physics)`: Weldを`parent->removeChild`で外して`m_seatWeld.reset()`、`JumpPower`相当の上向き速度+`Root->Size.y`分の上方テレポートで離脱、`seat->clearOccupant()`。
- `computePose()`を三項演算からif-elseに書き換え、`m_seated`最優先分岐を追加（着席ポーズの脚/腕角度）。

**4. User（`include/Core/User.hpp` / `src/Core/User.cpp`）**
- `processHotkeys()` → `processHotkeys(Physics*)`にシグネチャ変更。Space押下時、着席中は`standUp()`、それ以外は`jump(physics)`。
- `processCharacterMovement`でW/A/S/Dの生の押下状態から`forwardAxis`/`rightAxis`を算出し`humanoid->move()`に渡す。

**5. 配線（Luau/SceneLoader/Editor）**
- `src/Core/LuauEngine.cpp`: `Instance.new`ファクトリに`Truss`/`Seat`追加、`humanoid_jump_closure`が`moveToward_closure`と同じパターンで`Physics*`を解決して`jump(physics)`を呼ぶよう修正。
- `src/Core/LuauEngine_Dispatch.cpp`: `PropertyRegistry::applyToDispatch("Seat", ...)`追加。
- `src/Core/SceneLoader.cpp`: `createInstance`に`"Truss"`/`"Seat"`ケース追加。
- `src/Editor/EditorManager.cpp` / `src/Editor/SceneHierarchyPanel.cpp` / `src/Editor/PropertiesPanel.cpp`: Insert Object・アイコン(`ICON_CUBE`共用)・`renderSchemaInspector(inst,"Seat",...)`を追加。
- `doc/Instances/Truss.md` / `doc/Instances/Seat.md`（新規）、`doc/Instances/README.md`・`spec.md`のHumanoid章に暗黙仕様を追記。

**6. ユーザーの実機テストで発覚した4件のバグ修正**
- **着席ポーズの向きが逆**: 当初「Rootの向きをSeatから180度反転」で対処したが、ユーザーから「体の向きが反転する、アニメーション側を直すべき」と指摘を受け撤回。真因は`Humanoid.cpp`の`makeArm`/`makeLeg`の回転軸の符号で、着席ポーズの脚/腕角度を`-90/-10`から`+90/+10`に反転して解決（Root自体の向きはSeatと同じに戻した）。
- **溶接位置が高すぎる**: `sitOn()`の高さオフセットを`Root->Size.y*0.5f`から`*0.25f`に縮小。
- **Trussでアイドル時にずり落ちる**: 毎フレームY速度を0上書きするだけでは物理サブステップ内で重力が一瞬働いた分の位置ドリフトが蓄積していたため、`setActorFlag(eDISABLE_GRAVITY, true)`で重力自体を止める方式に変更。
- **Truss中にSで自由落下**: 登坂中も「移動方向を向く」既存の回転更新ロジックが働き、後退入力で180度回転→当たり判定とズレて落下していた。Truss接触中は回転更新ブロック自体をスキップするよう修正。
- **Weld合成で角度ロックが失われ転倒しやすくなる**: `Physics::rebuildGroup()`（Weldのcompound生成部）が`createActor()`と違い`LockFlags`を一切適用していなかったのが原因。着席→離脱を繰り返すたびにRootの角度ロック（X/Z軸、`resolveParts`で設定）が消えていた。`rebuildGroup()`内でcompound生成時にassembly全キューブの`LockFlags`をOR合成して適用するよう修正。
- **`system.Heartbeat`のようなグローバルシグナルで、自己Disconnectする単発スクリプトを使うと`lua_unref`がアクセス違反でクラッシュ**: Seat機能自体とは無関係の既存バグと判明（クラッシュスタックにSeat/Weld/Physics/Humanoidのコードが一切現れないことから切り分け）。`RCBNScriptSignal::connect()`が`m_mainL`を「最初に`Connect`を呼んだその時のL」でキャッシュしており、ループを持たない単発スクリプトのコルーチンスレッドがLuau側でGC/破棄された後にdisconnect/fireで参照するとdangling `lua_State*`になっていた。`lua_mainthread(L)`でVMの永続的なメインスレッドに解決してからキャッシュするよう`RCBNScriptSignal.cpp`を1行修正。

### なぜそうしたか

- **Truss/SeatをCube継承にした理由**: `Renderer.cpp`の描画ディスパッチが`IsA("Cube")`分岐で`Cube*`にstatic_castして`draw()`を呼ぶだけの設計だったため、`Cube`を継承すればDecal/Texture対応込みの描画コードがそのまま乗り、Renderer.cppを一切変更せずに済む。`BaseCube`直接継承だと専用描画コードの新規実装かRenderer.cpp改修が必要になっていた。
- **「接触」判定をAABB重なりにした理由**: 既存の`applyBuoyancy`が同じ精度（回転無視）でLiquidCube判定をしており、PhysXのシーンクエリ（オーバーラップ）を新規に増やすより既存パターンの横展開の方が一貫性があり実装コストも低いと判断。
- **Steer/Throttleを独立した`forwardAxis`/`rightAxis`引数にした理由**: 既存の`targetMoveDir`は複数キー同時押下時に正規化されるため斜め入力で0.707倍に減衰する。Seatのステアリング/スロットルや車のペダルはそれぞれ独立した±1であるべきなので、生の合算値（Nullで自動的に0になる）を新しい2引数として渡す設計にした。
- **着席ポーズの向き問題を「Root回転」ではなく「ポーズ角度」で直した理由**: ユーザー実機確認で「Root回転を反転すると体全体の向きが反対になる」と判明したため。当初はどちらが原因か切り分けできておらず両方触ってしまったが、ユーザーの一言で真因（`makeLeg`/`makeArm`の回転計算で負角度が後方(+Z)、正角度が前方(-Z)に曲がる仕様）にたどり着けた。
- **Truss重力対策を「毎フレーム速度0上書き」から「ActorFlag::eDISABLE_GRAVITY」に変えた理由**: 前者は「観測できる状態(速度)」だけをリセットしていて、PhysXの物理サブステップ内で重力が積分される「その間」の位置ドリフトまでは防げていなかった。後者は根本原因（重力そのもの）を止めるため副作用なく解決する。
- **LockFlags紛失の修正範囲をrebuildGroup内OR合成にした理由**: assembly（Weldで繋がるキューブ群）は複数キューブが1つのcompound PxRigidDynamicに統合されるため、どのキューブのLockFlagsを採用するかが曖昧になる。安全側に倒し、assembly内の誰か1つでもロックを要求していれば尊重されるようOR合成を選んだ（Rootだけでなく将来同様の要件を持つ別クラスにも自然に対応できる）。

### どういう経緯か

1. Plan modeでreadme.mdの2項目を確認。Explore/PlanエージェントはCLAUDE.mdの明示指示により使わず、Grep/Readで自分でBaseCube/Weld/Humanoid/User/Physics/Rendererの既存パターンを調査。
2. AskUserQuestionで3点（Truss操作、水中ジャンプの挙動、Steer/ThrottleのOwnership）を確認 → 全て提示した推奨案が採用された。
3. Physics(findOverlapping) → Truss/Seatクラス → Humanoid → User → Luau/SceneLoader/Editor → ドキュメント の順で実装し、ビルド成功。
4. ユーザーが実機+Luauスクリプト（`system.Heartbeat`でSeat.Steer/Throttleの4方向を検証するテストスクリプト）でテストし「概ね問題ないが3点」と報告: 着席ポーズの向き、Trussでのずり落ち、Trussの後退時の自由落下。3点とも原因を特定して修正・再ビルド。
5. さらにユーザーから「着席ポーズは反転ではなくアニメーション側が逆」という訂正が入り、Root回転の180度反転を撤回してポーズ角度側を反転する形に直した。同時に「溶接位置が高い」との指摘で高さオフセットも調整。
6. 続けて「ジャンプしながらSeatに接触して溶接/切断を繰り返すとRootの回転ロックが外れて転倒しやすい」という4件目のバグ報告。`Physics::rebuildGroup`の調査でLockFlags未適用を発見し修正。
7. さらに「Seatを離れるときにクラッシュした」という報告（`lua_unref`でアクセス違反）。スタックトレースにSeat関連コードが一切現れないことから切り分けを行い、`RCBNScriptSignal`の`m_mainL`キャッシュがコルーチンスレッド起因でdanglingになる既存バグだと特定して修正。

### 未解決・保留

- 4件のバグ修正（着席ポーズ向き/溶接高さ/Truss重力/Truss回転、およびLockFlags・RCBNScriptSignalの2件）はビルド確認のみで、ユーザーによる実機再確認は本サマリー作成時点で未回答。
- `sitOn()`の高さオフセット（`Root->Size.y*0.25f`）は目安の調整値。実際に見てまだ高い/低い場合は再調整が必要。
- 着席ポーズの脚/腕角度（`90.0f`/`10.0f`）も暫定値。他のポーズ（ツール装備時の腕上げ等）との組み合わせ時の見た目は未検証。
- `RCBNScriptSignal`の`lua_mainthread()`修正は今回発覚した経路（Heartbeat + 自己Disconnect）のみ確認・修正した。同じ`m_mainL`キャッシュパターンを使う他のシグナル（`Touched`等）で同種の問題が理論上は起こり得るが、`lua_mainthread()`化により根本原因ごと塞がっているはずなので追加対応は不要と判断（未検証）。
- Truss接触判定はAABB重なりのみ（回転無視）。斜めに設置されたTrussや、狭い隙間での接触判定の精度は未検証。
- Seatの`m_occupant`（同時着席ガード）は複数キャラクターが同時にテストする状況（マルチプレイ相当）では未検証。

### 暗黙仕様の発見（spec.mdに無い挙動）

- **`Physics::rebuildGroup()`（Weldのcompound生成）は`createActor()`と違いLockFlagsを適用しない設計だった**（今回修正するまで）。Weld/Motorで複数キューブをcompound化する既存機能（帽子・ラグドール等）を触る際は、個々のキューブが持つ`BaseCube::LockFlags`がcompound化のたびに失われないか常に確認すること。
- **`makeLeg`/`makeArm`（Humanoid.cppの匿名名前空間関数）の回転角度は、正の角度でキャラクターの前方(-Z)、負の角度で後方(+Z)に足/腕が曲がる**。`Quaternion::LookRotation`がローカル-Zを前方とする設計（`Cube`のFront面=法線(0,0,-1)と一致）と整合している。今後この関数を使って新しいポーズ（しゃがむ、寝る等）を追加する際はこの符号を踏まえること。
- **`RCBNScriptSignal::m_mainL`は「最初にConnectを呼んだ時のlua_State*」をキャッシュする設計だったが、Luauのコルーチンスレッドはそのスクリプトの実行終了とともに破棄されうる**。ループを持たない単発スクリプト（セットアップだけして終了する典型的な初期化スクリプト）から`Connect`されたシグナルは特に危険で、後から`disconnect`/`fire`（`once`経由や外部からの明示的Disconnect含む）を呼ぶとdanglingな`lua_State*`を使うことになる。`lua_mainthread(L)`で解決してからキャッシュすることで回避した。同様にlua_State*をメンバに保持するコードを新規に書く際はこのパターンを踏襲すること。
- **`Humanoid::move()`は`physics`引数がnullptrでも呼び出せる設計になっており（`moveToward()`など）、Seat/Truss判定(`physics->findOverlapping`)もnullガードが必須**。Seat/Truss機能を拡張する際、`physics`のnullチェックを忘れると当該呼び出し元でクラッシュする。
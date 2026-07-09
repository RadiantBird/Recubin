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

## 2026-07-07 OpenGLパイプライン最適化 + Mac対応準備（IPlatformインターフェース導入）

### 指示内容
readme.mdのTodoリストのうち以下2項目を続けて依頼された:
1. 「OpenGLパイプラインを最適化(必要かは検討の余地あり)」
2. 「Mac対応を開始する」のうち「レンダラーを抽象化する(OS依存をなくし、関数の実装部分で分岐を行う)」「ファイル操作のインターフェイス化」（Metal実装/OpenGL4.1準拠自体はスコープ外、Mac実装は動作確認不要のモックでよいと明示）

いずれもPlan modeで、Explore/PlanエージェントはCLAUDE.mdの指示により使わずGrep/Readで自分で調査してから実装した。

### Part A: OpenGLパイプライン最適化

**何をしたか**（`include/Core/Renderer.hpp` / `src/Core/Renderer.cpp`のみ変更）
- **Uniform locationキャッシュ**: `Renderer::init()`で`shaderProgram`の全uniform location（`view`/`projection`/`viewPos`/`model`/`unlit`/`useTriplanar`/`u_textureScale`/`uTime`/`uIsLiquid`/`useVertexColor`/`ourColor`/`hasShadows`/`lightSpaceMatrix`、および光源8灯分の`uLights[i].*`）を一括取得してメンバ変数にキャッシュ。`renderViewport()`のMain Pass、`renderTerrain()`内で毎フレーム発生していた`glGetUniformLocation`呼び出し（特に光源ループ内の文字列結合）を置き換え。
- **フラスタムカリング**: `extractFrustumPlanes()`/`sphereInFrustum()`をRenderer.cpp内のファイルローカルstaticヘルパーとして追加（Gribb-Hartmann法で6平面抽出）。`renderInst`ラムダ内のCube/Cylinder/TriangularPrism/Sphere/MeshCube/LiquidCube各分岐で、バウンディングスフィア（中心=ワールド位置、半径=`Size.length()*0.5`）が視錐台外なら描画自体をスキップ。
- **不透明/半透明分離**: `setBlendForAlpha`ラムダを追加。`Color.a>=0.999f`のオブジェクトは`GL_BLEND`を無効化して描画。ループを抜けた直後に`GL_BLEND`を再度有効化（`renderClouds`/`renderParticles`が「BLEND常時有効」を前提にしているため）。

**なぜそうしたか**
- スコープはメインカメラパスのBaseCube系のみに限定し、Shadow Pass（別のライト空間frustumが必要でリスク増）とTerrain（既存の距離ベースストリーミングに任せる方が一貫性がある）は対象外とAskUserQuestionで確認済み。インスタンシング/バッチングも設計変更が大きいため対象外。
- バウンディングスフィアを採用したのは、BaseCube系は回転しうるため軸並行AABBだと不正確になるため。対角線の半分を半径とする保守的な球なら安全側に倒せる。

**どういう経緯か**
1. Renderer.cpp/hppを自分で読み込み、Uniform未キャッシュ・フラスタムカリング無し・GL_BLEND常時有効・インスタンシング無しの4点の無駄を発見。
2. AskUserQuestionで対象を3点（Uniform/カリング/ブレンド分離）に絞り込み、実装・ビルド成功。
3. ユーザーが実機確認し「劣化は見受けられなかった、良い修正」と評価。

**未解決・保留**: 特になし（ユーザー確認済み）。

### Part B: Mac対応準備（IPlatformインターフェース導入）

**何をしたか**
- 新規: `include/Util/IPlatform.hpp`（インターフェース、`FileFilter`構造体）、`include/Util/Platform.hpp` + `src/Util/Platform.cpp`（`getPlatform()`シングルトンアクセサ）、`include/Util/WindowsPlatform.hpp` + `src/Util/WindowsPlatform.cpp`（Windows実装）、`include/Util/MockPlatform.hpp`（Mac向けスタブ）。
- 6箇所に重複していたCOMファイルダイアログコードを統合: `src/Editor/EditorManager.cpp`（`openSceneDialog()`・パッケージ出力先フォルダ選択・Loadボタンの3箇所）、`src/Editor/SceneHierarchyPanel.cpp`（`pickFile()`/`pickFolder()`）、`src/Editor/PropertiesPanel.cpp`（`browseFolder()`、`browseFile()`呼び出し10箇所）、`src/Editor/AnimationEditorPanel.cpp`（`openAnimDialog()`）。
- `ShellExecuteW`呼び出し（SceneHierarchyPanel.cpp、PropertiesPanel.cpp）を`getPlatform().revealInFileManager()`に置き換え。
- `SetConsoleOutputCP`/`SetConsoleCP`（main.cpp、game_main.cpp、test_main.cpp）を`getPlatform().setupConsoleUtf8()`に置き換え。
- `src/Core/LuarCompiler.cpp`/`include/Core/LuarCompiler.hpp`の`LoadLibraryA`/`GetProcAddress`/`FreeLibrary`（`HMODULE`）を`getPlatform().loadDynamicLibrary`/`getSymbol`/`freeDynamicLibrary`（`void*`）に置き換え。
- 実際には使われていなかった`<windows26.h>`を`Renderer.hpp`、`EditorManager.hpp`、`ViewportPanel.hpp`、`SecondaryViewportPanel.hpp`、`LuarCompiler.hpp`から削除。`game_main.cpp`/`test_main.cpp`の不要な直接includeも削除。
- `include/Util/FileDialog.hpp`・`src/Util/FileDialog.cpp`を削除（WindowsPlatformに統合されたため）。
- `doc/Util/FileDialog.md` → `doc/Util/IPlatform.md`にリネーム・内容更新、`doc/Util/README.md`のインデックス更新。
- `src/Instances/Sound.cpp`のコメント内「`browseFile()`」表記を「`getPlatform().openFileDialog()`」に更新（削除された関数への言及を修正）。

**なぜそうしたか**
- 「レンダラーを抽象化する」を調査した結果、Rendererクラス自体の描画ロジックは既にOpenGL/GLFWでポータブルであり、真の問題は`Renderer.hpp`など4ヘッダが実際には使っていないのに`<windows26.h>`を巻き込んでいたことだと判明。ここがエンジンのコア部分のクロスプラットフォーム化を阻害していた。
- 「ファイル操作のインターフェイス化」を調査した結果、`browseFile()`相当のCOMダイアログロジックが実は6箇所に重複コピペされている実態を発見。インターフェース化する前にまずこの重複を1箇所（WindowsPlatform）に統合する必要があった。
- 既存の`IInputBackend`/`GLFWInputBackend`、`IEditorManager`/`NullEditorManager`という確立されたパターン（インターフェース+具象クラス）を踏襲し、新規パターンを持ち込まず一貫性を優先した。
- ファイル操作インターフェースの対象範囲をユーザーに確認（ダイアログ系のみ vs コンソール設定・DLLロードも含める）し、広い方（後者）を選択された。
- Mock実装の検証方法もユーザーに確認（コンパイルが通ることのみ確認 vs Windowsビルド上でMockに切り替えられるスイッチを追加）し、後者を選択。CMakeLists.txt/build.pyには触れず、環境変数`RECUBIN_MOCK_PLATFORM`でランタイム切り替えする方式にした（ビルドシステムを触らないというCLAUDE.mdの制約を守りつつ検証手段を用意するため）。
- `IPlatform::openFileDialog`/`saveFileDialog`は単一の`filterName`/`filterSpec`ではなく`std::vector<FileFilter>`で受ける設計にした。理由は次項。
- フォルダピッカーの挙動差異（EditorManager.cpp/PropertiesPanel.cppは`FOS_PATHMUSTEXIST`付き、SceneHierarchyPanel.cppは無し）は、より安全な「PATHMUSTEXIST付き」に統一した。
- `LuarCompiler`のエラーログから`GetLastError()`のエラーコード表示が失われる点は、小さな挙動変化として許容した（実装前にプラン上で明示）。

**どういう経緯か**
1. Plan mode再突入。前回のOpenGL最適化プランとは別タスクのため、プランファイルを上書きして新規作成。
2. `windows26.h`の使用箇所、`browseFile()`や`ShellExecuteW`等の実際のOS依存箇所をGrep/Readで自分で調査。
3. 調査中にCOMダイアログコードの6箇所重複という想定外の事実を発見し、当初の想定（単純にIPlatformでラップするだけ）よりスコープが広がった。
4. AskUserQuestionで2点（ファイル操作インターフェースの対象範囲、Mock検証方法）を確認してから最終プランを作成、承認を得た。
5. 実装順序: IPlatform.hpp→MockPlatform.hpp→WindowsPlatform.hpp/cpp→Platform.hpp/cpp→LuarCompiler→main/game_main/test_main→PropertiesPanel→SceneHierarchyPanel→EditorManager→AnimationEditorPanel→ヘッダの`windows26.h`削除→FileDialog削除→ビルド確認の順で実施。
6. 実装中、`SceneHierarchyPanel.cpp`の`pickFile()`が実は2つのフィルタ（Luau Script/Luar Script）を同時に登録する仕様だったと判明し、`IPlatform`のシグネチャを計画時点の単一`filterName`/`filterSpec`案から`std::vector<FileFilter>`に拡張した。
7. ビルド成功後、`RecubinTest.exe`を通常起動と`RECUBIN_MOCK_PLATFORM=1`起動の両方で実行し出力を比較、完全一致（既存の84 passed/2 failedは無関係な既存差分）を確認。

**試して失敗した/計画から変更した点**
- 当初プランの`IPlatform::openFileDialog(filterName, filterSpec)`という単一フィルタのシグネチャは、実装中に`SceneHierarchyPanel.cpp`の`pickFile()`が2フィルタ必須と判明したため`std::vector<FileFilter>`に拡張した。プラン段階の「代表的なコード例」は必ずしも全呼び出し元を検証済みではないため、実装時に全呼び出し元を洗い出してから確定させる必要がある、という教訓。

**未解決・保留**
- 実際のダイアログ操作（ファイルを開く/保存、フォルダ選択、エクスプローラーで開く）はGUIが絡むため、エディターでの実機確認は未実施。次回セッション冒頭で確認が必要。
- `RECUBIN_MOCK_PLATFORM=1`でのエディター実機起動（ダイアログがクラッシュせず空文字列として扱われるか）も未確認。`RecubinTest.exe`（ヘッドレス）のみでの確認に留まる。
- `CMakeLists.txt`/`build.py`は意図的に触っていないため、実際のMacビルドターゲットはまだ存在しない。将来Mac対応を進める場合はビルドシステム自体の対応が別途必要。
- `launcher/main.cpp`のWindows専用ショートカット生成コード（`CoCreateInstance(CLSID_ShellLink)`等）は今回スコープ外のまま。将来Mac版ランチャーが必要になれば別途対応要。
- `FileLoader.cpp`/`SceneLoader.cpp`/`Sound.cpp`に残る独立した`utf8_to_wstring()`実装（3箇所の重複）は、「呼び出し側への漏れが無い」という理由で今回スコープ外にしたが、コード自体の重複は解消されていない。将来リファクタする余地あり。
- `RecubinTest`実行結果の「2 failed」はFileRef関連の既存テスト失敗で、今回の変更とは無関係と判断したが、原因自体は未調査。

### 暗黙仕様の発見（spec.mdに無い挙動）

- **`Renderer.hpp`/`EditorManager.hpp`/`ViewportPanel.hpp`/`SecondaryViewportPanel.hpp`が`<windows26.h>`をincludeしていたが、実際にはどのヘッダもWin32型（HWND/DWORD等）を使用しておらず完全に不要だった**（`LuarCompiler.hpp`のみ`HMODULE`で正当な使用だったが、今回`void*`化して不要になった）。ヘッダの不要include確認は、実際のシンボル使用箇所をgrepしないと見落としやすい典型例。
- **`fragment.glsl`のアルファ出力は`outAlpha = ourColor.a * MatAlpha`のみで、Decal/Textureのアルファ(`texColor.a`)は最終出力に一切影響しない設計**（2026-07-05セッションで確定済みの仕様）。この仕様のおかげで、フラスタムカリング/ブレンド分離のような「Cube単位でオブジェクトの不透明度を判定する」処理は`Color.a`だけを見れば正しく動作する（テクスチャの透明度を別途考慮する必要が無い）。
- **`browseFile()`という「1箇所に定義された共通関数」が存在するにもかかわらず、実際には6箇所で同じCOMダイアログロジックがコピペされ、共通関数は一部の呼び出し元（main.cpp、PropertiesPanel.cppの一部）からしか使われていなかった**。「共通ヘルパーがあるからといって実際に使われているとは限らない」という点は、今後同種の重複コードを探す際の教訓。
- **`EditorManager.cpp`には同一のシーンファイル読み込みダイアログ処理が2箇所（`openSceneDialog()`メソッドと、Loadボタンのインラインコード）に完全に重複して存在していた**。メソッド化されているのに使われずインライン重複が残るというパターンがあるため、類似の重複を探す際は「同名メソッドの有無」だけでなく「同じロジックパターンのgrep」も必要。

---

## 2026-07-07 Decal名前の勝手な変更 + Decal/Texture/SurfaceGui のUV左右反転修正

### 指示内容
readme.mdのTodoリストのうち以下2項目の修正を依頼された:
1. Decalの名前が勝手に変わる問題の修正
2. Decalがカメラから見て左右反転している問題の修正

実装後、ユーザーから追加で「Top/Bottom面のDecalもまだ左右反転している」という指摘を受け、同種の修正を追加実施。

### 何をしたか

**1. Decal名前の勝手な変更バグ修正**
- 原因: `Decal::setFace()`/`Texture::setFace()`がFace変更のたびに無条件で`Name`を既定名(`Decal_<Face>`/`Texture_<Face>`)で上書きしていた。`PropertiesPanel.cpp`のFaceコンボボックス変更時にこの`setFace()`が呼ばれるため、F2やProperties欄で改名済みのDecal/Textureでも、Faceを変えるたびに名前が消えていた。
- `src/Instances/Decal.cpp`・`src/Instances/Texture.cpp`の`setFace()`を、「現在の`Name`がまだ変更前Faceの既定名と一致する場合のみ新しい既定名に追従し、ユーザーが改名済みなら上書きしない」という条件分岐に変更。
- `SceneLoader.cpp`のYAML読み込み時は元々プロパティ適用後に保存済み`Name`を再上書きする実装になっており（既知のコメントあり）、この修正と競合しないことを確認済み。

**2. Decal/Texture/SurfaceGuiのUV左右反転バグ修正（垂直4面）**
- 原因: `src/Instances/Cube.cpp`の`createCubeVertices()`が全キューブ系（Cube/Truss/Seat等、Named<T,Cube>継承クラス）で共有する唯一のVAOを生成しており、そのUV基底`u`（テクスチャの「右方向」）が、Front/Back/Right/Left全4面で実際のカメラ右方向と逆になっていた（クォータニオンの`getForward`/`getRight`/`getUp`から数値的に検証）。
- `u = Vector3(-nz, 0, nx)` → `Vector3(nz, 0, -nx)`に修正。
- Decal/Textureと同じVAO・同じTexCoordをSurfaceGuiベイクテクスチャの3D面表示も使っているため、`Renderer_GUI.cpp`の`bakeSurfaceGui()`にあった「焼き込みテクスチャの手動左右反転」（根本のUV不整合を打ち消すための辻褄合わせ処理）を削除。あわせて`hitTestSurfaceGui()`のクリック判定用`uAxis`（Front/Back/Right/Left分）と、Xミラー補正(`fboX = (1.0f - texU) * L.w` → `texU * L.w`)も新しいUV基底に合わせて更新。

**3. Top/Bottom面の左右反転追加修正**
- ユーザー指摘を受け再調査。Top/Bottomは視点のyawによって見え方が回転する（floor decal特有の正常な性質）ため、単純な「カメラ右方向との比較」では判定できなかった。代わりに`cross(u, vv)`（UV基底の外積）と面法線の符号を6面全てで比較したところ、修正済みのFront/Back/Right/Leftは全て`cross(u,vv) == +法線`で一貫していたのに対し、Top/Bottomだけ`cross(u,vv) == -法線`と符号が逆で、視点によらない恒常的な鏡像バグだと判明。
- `Cube.cpp`のTop/Bottom分岐の`u`を`(1,0,0)`→`(-1,0,0)`に反転。`Renderer_GUI.cpp`の`hitTestSurfaceGui()`のTop/Bottom用`uAxis`も同様に反転して整合させた。

### なぜそうしたか（判断の経緯）

- **Decal名前バグの修正方針**: 「Faceを変えたら常にリネームする」仕様自体を廃止するのではなく、「まだ既定名のときだけ追従」という条件を追加する方式を選択。Faceを変えてすぐは名前が追従してくれる利便性を残しつつ、ユーザーが改名した後は上書きしない、という両立を狙った。
- **Texture::setFace()も同時に修正するか**: readme.mdのTodoは「Decal」のみ明記だったが、全く同じバグパターンがTexture.cppにも存在したため、AskUserQuestionでユーザーに確認。「同時に修正」を選択されたため対応。
- **UV左右反転の直し方（Decal限定のシェーダーuniform分岐 vs Cube.cpp根本修正）**: 前者はDecalだけに影響を絞れるが、Cube.cppの頂点生成はDecal/Texture/SurfaceGuiベイクで完全に共有されており、後者の方が本質的な修正になる。AskUserQuestionで両案を提示し、ユーザーは「根本のUV基底自体を直す」（Texture/SurfaceGuiへの影響も許容）を選択。CLAUDE.mdの「指示されたファイルだけ触る」原則とはやや緊張関係にあるが、事前に選択肢と影響範囲を明示し承認を得た上での実施。
- **bakeSurfaceGuiの手動左右反転を「削除」した理由**: この反転処理は、UV基底の不整合を打ち消すために後から追加された辻褄合わせだったと判断。根本のUV基底を直した後もこの反転処理を残すと二重反転になり、SurfaceGuiだけ再び逆向きになってしまうため、単純な「両方直す」ではなく「片方（辻褄合わせ側）を消す」必要があった。
- **Top/Bottomの判定にcross(u,vv)を使った理由**: floor/ceiling decalは見る角度（yaw）によって画面上で回転して見えるのが正常な挙動であり、「特定の視点でのカメラ右方向と比較する」だけでは真の鏡像バグと視点依存の回転を区別できない。視点に依存しない不変量として、UV基底の外積と面法線の符号一致（＝右手系として一貫しているか）を全6面で比較する方法を用いた。

### どういう経緯か

1. readme.mdのTodo2件（Decal名前バグ、Decal左右反転）を実装依頼される。
2. `Decal.cpp`/`Decal.hpp`を読み、`setFace()`の無条件Name上書きが名前バグの原因と特定。
3. 左右反転バグは`Cube.cpp`の`createCubeVertices()`のUV基底を数式で検証（クォータニオンの`getForward`/`getRight`/`getUp`を使い、各面を正面から見た時の実際のカメラ右方向を計算）し、垂直4面全てで逆転していると判明。この修正はDecal/Texture/SurfaceGuiが同じ頂点データを共有するため影響範囲が広いと分かり、AskUserQuestionで2問（UV修正方式、Texture::setFaceも直すか）を確認してから実装。
4. ビルド成功を確認し、readme.mdのTodo2件をチェック済みに変更。
5. ユーザーから「Top/Bottomもまだ左右反転している」と追加報告。最初はTop/Bottomにも同じ「カメラ右方向との比較」を適用しようとしたが、Top/Bottomは視点のyawによって画面上の見え方が回転するため、単一の「正しいカメラ右方向」が定義できないことに気づいた（試行錯誤: yaw=0/90/180の3パターンでカメラ右方向を計算したが、それぞれ異なる結果になり、この比較方法では判定不能と判断）。
6. 代わりに`cross(u,vv)`と面法線の符号一致を6面で比較する方法に切り替え、Top/Bottomのみ符号が逆（恒常的な鏡像バグ）と判明。`Cube.cpp`と`Renderer_GUI.cpp`のTop/Bottom分をそれぞれ反転し、ビルド成功。

### 試して失敗した/やめた方法

- **Top/Bottomの反転判定に「特定の視点でのカメラ右方向」を使う方法**: 垂直4面と同じ手法（yawを固定してカメラのforward/right/upを計算し、UV基底のuと比較）をTop/Bottomにも適用しようとしたが、yaw=0では一致、yaw=90では90°回転した関係、yaw=180では不一致という具合に、比較する視点(yaw)によって結果が変わってしまい、この面には使えない手法だと判明。→ **教訓**: 面の法線が視線方向（forward）と平行になりうる面（Top/Bottom）は、視点固定の「カメラ右方向」比較では判定できない。視点に依存しない不変量（外積と法線の符号一致など）で判定する必要がある。

### 未解決・保留

- Decal画像の向き、SurfaceGuiボタンのクリック位置、Top/Bottom面のDecalの見た目について、実機での確認はユーザーに委ねている（GUIアプリの自動スモークテストは方針上禁止のため、ビルド成功の確認までに留めた）。
- `doc/Instances/Decal.md`・`doc/Instances/Texture.md`に残っている可能性のある「setFace()はNameを常に既定名に上書きする」という趣旨の記述は、今回の挙動変更に合わせて更新できていない（ドキュメント更新は指示されていないため未対応、次回関連作業時に確認要）。
- readme.mdは会話の途中でユーザー側（またはlinter）により再構成され、Todoがカテゴリ別（設計関係/エディター関係/物理エンジン関係/レンダリング関連）に整理し直され、完了済み項目の行自体が削除された。今回チェック済みにした2行はこの再構成で既に無くなっているため、追加対応は不要と判断。

### 暗黙仕様の発見（spec.mdに無い挙動）

- **`Cube.cpp`の`createCubeVertices()`は、Cube本体だけでなくTruss/Seat（`Named<T,Cube>`継承）を含む全キューブ系で共有される唯一のVAOであり、Decal/Texture/SurfaceGuiベイクの3D面表示は全てこの同一頂点データのUV座標に依存している**。この頂点生成を1箇所直すと、意図せずとも必ずこの3系統全てに影響が波及する。今後この関数を触る際は、影響範囲としてこの3系統を毎回セットで確認する必要がある。
- **`bakeSurfaceGui()`にあった「テクスチャの手動左右反転」処理は、実際にはUV基底の不整合を打ち消すための辻褄合わせだった**。根本原因（`createCubeVertices`のUV基底）を直す際は、この種の「対症療法として後から追加された補正コード」を一緒に取り除かないと二重反転で再び壊れる。同様のパターン（片方の修正が別の補正コードと組み合わさって「たまたま正しく見えていた」）が今後も見つかる可能性がある。
- **面の法線が視線方向と平行になりうる面（Top/Bottom、あるいはSkyboxの上下面など）では、「特定視点でのカメラ右方向」を基準にした左右判定が原理的に使えない**（yawによって画面上の見え方が回転するため）。この種の面のUV向きを検証する際は、`cross(u,vv)`と法線の符号一致という視点非依存の不変量を使うと判定できる。

---

## 2026-07-07 テストプレイ中のシーン読み込み確認ポップアップ

### 指示内容
readme.mdのTodo「シーンファイルをテストプレイ中に読み込もうとしたら、ポップアップありで終了するか確認とってからすぐシーンを切り替えられるようにする」を実装。

### 何をしたか（`include/Editor/EditorManager.hpp` / `src/Editor/EditorManager.cpp`のみ変更）
- 既存の挙動を調査した結果、ツールバーの「Load」ボタンは元々モードガード無しで`pendingLoadPath`に直接代入していたが、`main.cpp`側のリロード処理（`ed->isEditMode()`必須）がテストプレイ中は素通りするため、確認もなく「Stopを手動で押すまで無反応に見える」状態だった。「Open Scene」メニュー項目・Ctrl+Oは`isEditMode()`ガードで完全にブロックされていた。
- `EditorManager::requestSceneLoad(const std::string& path)`を新設し、Open Sceneメニュー・Ctrl+O（`openSceneDialog()`経由）・ツールバーLoadボタンの3箇所を統一。Editモード中は従来通り即`pendingLoadPath`に代入、Play/Pauseモード中は確認ポップアップ（`m_showPlayLoadConfirm`/`m_pendingPlayLoadPath`）を開くだけに変更。
- `renderPlayLoadConfirmDialog()`を新設（`renderSaveDialog()`と同じImGuiモーダルパターン）。「終了して読み込む」を押すと`mode = EditorMode::Edit`にした上で`pendingLoadPath`をセット。`main.cpp`側は`mode`変更を次フレームで検知し、既存のPlay→Edit遷移処理（スナップショット復元）に続けて同フレーム内で`pendingLoadPath`処理が走るため、`main.cpp`は一切変更せずに「終了確認→即シーン切り替え」を実現できた。
- 「Open Scene」メニュー項目の`isEditMode()`ガードも撤去（AskUserQuestionでユーザーに確認し「解放する」を選択）。Ctrl+Oは`handleEditorShortcuts()`自体が`isEditMode()`のときしか呼ばれない既存の仕組みのため、今回はスコープ外として据え置き。

### なぜそうしたか
- `main.cpp`のPlay→Edit遷移＋pendingLoadPath処理の順序（同フレーム内で連続実行）を確認した上で、`mode`を`Edit`に変えてから`pendingLoadPath`を設定するだけで「即切り替え」が成立すると判断し、`main.cpp`側への変更を避けた（CLAUDE.mdのスコープ厳守原則）。
- Stopボタンの完全な後処理（ビューポートフォーカス切り替え等）は再現せず、`ed->mode = EditorMode::Edit`＋`controlMode = Free`のみに留めた。これは`main.cpp`内の安全装置（`consumeSafetyHaltRequest()`）が同じ最小パターンをすでに使っており、それに倣った。

### 未解決・保留
- Ctrl+Oショートカットは引き続きEditモード中のみ有効（`handleEditorShortcuts()`自体がEditモード限定で呼ばれる既存構造のため）。
- 実機でのポップアップ表示・シーン切り替えの確認はユーザーに委ねる（GUI自動スモークテスト禁止のためビルド成功の確認までに留めた）。

---

## 2026-07-08 3Dビューポート直接ドラッグ移動がUndo履歴に積まれないバグ修正

### 指示内容
readme.mdに新規追加されたTodo「間違えてPositionを変更してしまった場合にCtrl+Zを押しても、dirty判定にはなっているものの何も反映されない問題の修正」を調査・修正。

### 何をしたか（`include/Editor/ViewportPanel.hpp` / `src/Editor/ViewportPanel.cpp`のみ変更）
- ユーザーへのヒアリングで「3Dビューポートでキューブを直接クリック&ドラッグして移動（Gizmoハンドルではない自由移動ドラッグ）」が再現操作で、「Ctrl+Zを押すと移動が戻らず、直前の追加コマンドの取り消し（削除）が実行されるように見える」という具体的な症状を確認。
- `ViewportPanel::onRender()`を調査した結果、自由移動ドラッグのUndo記録ロジック（`wasDragging`/`m_isDraggingSelected`の比較でドラッグ開始/終了を検出し`m_freeDragEntries`をキャプチャ→`MultiGizmoCommand`として記録）に同一フレーム内順序のバグを発見。クリック検出ブロック（`ImGui::IsMouseClicked(0)`時）が`m_isDraggingSelected`をtrueにした**その同じフレーム内で後から**`wasDragging = m_isDraggingSelected`を読んでいたため、「前フレームの値」のつもりが「今フレームで既に更新済みの値」になってしまい、ドラッグ開始時の`false→true`遷移を一度も検出できずbeforeキャプチャが空のままになっていた。結果、ドラッグ終了時も`m_freeDragEntries.empty()`のためコマンドが記録されず、自由移動ドラッグは見た目上位置を変えるだけでUndoスタックに一切積まれていなかった。
- 同ファイル内のGizmoハンドル操作（`m_wasUsingGizmo`）は同種の判定を、ブロック終端で明示的に`m_wasUsingGizmo = isUsingGizmo;`と更新する永続メンバ経由で正しく実装しており、これを参考にした。
- `ViewportPanel.hpp`に新規メンバ`bool m_wasDraggingSelected`を追加し、`ViewportPanel.cpp`側で`wasDragging`の参照元を`m_isDraggingSelected`から`m_wasDraggingSelected`に変更、ブロック終端（自由移動ドラッグ検出の直後、「Moveモード自由移動」セクションの手前）で`m_wasDraggingSelected = m_isDraggingSelected;`を追加して次フレーム用に保存するようにした。

### なぜそうしたか
- 既存の`m_wasUsingGizmo`パターン（同一ファイル内で既に正しく機能している同種のドラッグ開始/終了検出）と全く同じ手法を踏襲することで、最小限の変更で根本原因（同一フレーム内での前フレーム値の汚染）を解消できると判断した。`m_freeDragEntries`のキャプチャ/記録ロジック自体や、Gizmoハンドル側の処理には一切触れていない。

### どういう経緯か
1. readme.mdのTodo文言だけでは再現操作が特定できなかったため、AskUserQuestionで「編集方法」「Ctrl+Z後の数値の挙動」「再現条件」の3点をユーザーに確認。
2. 「3Dビューポートで直接クリック&ドラッグ」「新規作成キューブでも再現」「コマンド履歴が壊れて追加の取り消し(削除)が実行される」という回答を得て、自由移動ドラッグのUndo記録コードに絞って調査。
3. `CommandHistory`/`SetVec3Command`/Propertiesパネルの各Position編集経路（テキスト入力・DragFloat3スライダー）は全て問題なくコマンドを記録しており、自由移動ドラッグ経路のみに絞り込めた。
4. `ViewportPanel.cpp`の該当ブロックを行単位で追い、`wasDragging`の読み取りタイミングが同一フレーム内での`m_isDraggingSelected`書き込みより後になっている点を特定し、Plan modeで原因と修正方針をまとめてユーザー承認を得てから実装。
5. ビルド成功を確認。

### 未解決・保留
- 実機での動作確認（新規キューブ作成→直接ドラッグ移動→Ctrl+Zで位置が戻ることの確認）はユーザーに委ねる（GUI自動スモークテスト禁止のため）。
- readme.mdの当該Todoに「ほかにもある可能性あり」との注記があったが、今回はGizmoハンドル操作・Propertiesパネル編集経路は問題なしと確認済み。他のプロパティ（Rotation/Sizeの自由ドラッグ等）で同種の記録漏れがないかは今回のスコープ外（自由移動ドラッグはPositionのみ変更する経路のため対象外）。

### 暗黙仕様の発見（spec.mdに無い挙動）
- **`m_isDraggingSelected`は「クリックした瞬間のフレーム」に即座にtrueへ更新される設計**（`ImGui::IsMouseClicked(0)`時の当該フレーム内で完結）。同一フレーム内でこの値を「前フレームの状態」として読もうとすると、更新後の値を読んでしまう順序依存のバグになりやすい。前フレームの値が必要な場合は`m_wasUsingGizmo`のように専用の永続メンバをブロック終端で明示的に更新するパターンを使う必要がある。この教訓は今後同種の「開始/終了エッジ検出」コードを書く際に注意が必要。

---

## 2026-07-08 ドラッガー実装（移動範囲クランプ）+ ImGuizmo移動モード「白い球」バグ修正

### 指示内容
readme.mdに新規追加された3項目をまとめて対応：
1. 「ドラッガーの実装」（Cubeサイズ＋軸制限で移動範囲を計算する／ドラッグ軸ごとに不要軸を除去する処理／xz・xy・yzドラッグ時に該当軸を無効化）
2. 「制限ボックス外に出ないようにクランプ処理を実装」
3. 「ImGuizmoの移動モードの白い球が動いてしまうバグを修正する」

### 何をしたか
**A. ドラッガー拡張（`src/Editor/ViewportPanel.cpp`）**
- 既存の「Moveモード自由移動」（選択キューブを直接クリック&ドラッグしてサーフェスに追従させる機能。`castRaySurface()`でヒットした面の法線軸`hitAxis`を固定し、残り2軸をレイと平面の交点で自由に動かす）を調査した結果、「hitAxis固定＝不要軸の除去」自体は既に実装済みだったが、自由な2軸に移動範囲の制限が一切なく、サーフェスの端を越えてどこまでもドラッグできる状態だったと判明。
- `newPos`計算直後・`teleportTo`呼び出し前に、hitAxis以外の2軸について `surfPos[i] ± surfHalf[i] ∓ movHalf[i]`（サーフェスの端から移動物の半サイズ分内側）を範囲としてクランプする処理を追加。サーフェスより移動物が大きく範囲が反転する場合はサーフェス中央に固定するフォールバックも追加。
- 既存の`collisionFit`（他オブジェクトとの任意衝突回避）は、このクランプの後段に従来通り適用されるよう順序を維持（サーフェス端の制約と、他物体との衝突回避を分離）。

**B. ImGuizmo「白い球」バグ修正（`src/imgui/ImGuizmo.cpp`）**
- `src/Editor/EditorManager.cpp:803`の`ImGuizmo::GetStyle().CenterCircleSize = 0.0f;`（TRANSLATEモード中心ハンドルを非表示にする設定）を発見。
- `src/imgui/ImGuizmo.cpp`の`GetMoveType()`内、screen判定（`MT_MOVE_SCREEN`、中心の2軸同時移動ハンドルの当たり判定）が`CenterCircleSize`とは無関係な固定±10pxの`mScreenSquareMin/Max`のみで判定しており、描画は消えているのに当たり判定だけ生き残っていたと特定。
- `GetMoveType()`のscreen判定に`gContext.mStyle.CenterCircleSize > 0.0f`の条件を追加し、非表示状態なら当たり判定も無効化されるようにした。

### なぜそうしたか
- ユーザーヒアリングで「ドラッガー」は既存の自由移動ドラッグの拡張であること、移動範囲は「乗っているサーフェスのサイズ・端」を基準にすることを確認済みだったため、新規の別ドラッグ手段を作らず既存ブロックへのクランプ追加に留めた。
- `src/imgui/ImGuizmo.cpp`は`temp_libs/ImGuizmo/ImGuizmo.cpp`とのdiffで、SCALE/TRANSLATE負方向ハンドル追加など既にプロジェクト側で独自パッチが入っているベンダーコピーだと確認できたため、今回のGetMoveType()修正もこの前例に倣った。
- 同じ`CenterCircleSize`はSCALE中心ハンドル・ROTATE_SCREENリングの見た目にも使われており理論上同種の問題がありうるが、ユーザーの指示は「移動モード」限定だったためTRANSLATE(`GetMoveType()`)のみを修正し、他は対象外とした。

### どういう経緯か
1. readme.mdの3項目について、AskUserQuestionで「既存自由移動ドラッグの拡張か新規ドラッグ手段か」「移動範囲の基準は何か」を確認し、「既存拡張・サーフェス基準」と回答を得た。
2. 「白い球」バグについても、当初は「毎フレーム変化する行列をImGuizmoに渡すことでSCALE同様の内部参照ずれが起きているのでは」という仮説を立てたが、確証がなかったためAskUserQuestionで症状を確認したところ「過去に非表示にしたはずが実際はまだ動作している」という全く別の原因（描画と当たり判定の不整合）と判明。
3. `grep`で`CenterCircleSize`を検索し、`EditorManager.cpp:803`での0.0f設定と、`ImGuizmo.cpp`の`GetMoveType()`のscreen当たり判定コードを突き合わせて根本原因を特定した。
4. Plan modeで両修正をまとめて計画・承認を得てから実装、ビルド成功を確認。

### 未解決・保留
- 実機での動作確認（サーフェス上でキューブを端までドラッグして止まること、ImGuizmo中心の見えないハンドルが反応しなくなること）はユーザーに委ねる（GUI自動スモークテスト禁止のため）。
- `CenterCircleSize`を使うSCALE中心ハンドル・ROTATE_SCREENリングにも同種の「描画は消えているが当たり判定は生きている」問題が理論上ありうるが、今回はスコープ外（ユーザー指示が「移動モード」限定だったため）。

### 暗黙仕様の発見（spec.mdに無い挙動）
- **`src/imgui/ImGuizmo.cpp`はベンダー配布そのままではなく、プロジェクト側で既に独自パッチ（SCALE/TRANSLATE負方向ハンドル追加等）が入ったコピー**（`temp_libs/ImGuizmo/ImGuizmo.cpp`という素のベンダーコピーが別途残っており、diffで差分を確認できる）。このため今回のようにImGuizmo自体の挙動を直す必要がある場合、`src/imgui/ImGuizmo.cpp`を直接編集するのがこのプロジェクトの前例に沿ったやり方になる。
- **ImGuizmoの`Style::CenterCircleSize`は「描画の見た目」のみを制御し、`GetMoveType()`のscreenハンドル当たり判定（`mScreenSquareMin/Max`、固定±10px）とは独立している**。見た目を0にして非表示にしても当たり判定は生きたままになるため、ImGuizmoの視覚要素を「隠す」場合は当たり判定側も併せて確認する必要がある。同じ罠がSCALE中心ハンドル・ROTATE_SCREENリングにも当てはまる可能性がある。

---

## 2026-07-08 Select/Move/Resize/Rotateボタンのトグル化（無操作モード追加）

### 指示内容
エディターツールバーのSelect/Move/Resize/Rotateボタンについて、アクティブなボタンをもう一度押したら「無操作」（クリックしても選択すら変わらない、カメラ操作のみ）になるようにしてほしいという追加依頼。

### 何をしたか（`include/Editor/ViewportPanel.hpp` / `src/Editor/ViewportPanel.cpp` / `src/Editor/EditorManager.cpp`のみ変更）
- `ViewportPanel`に新規メンバ`bool toolNone`を追加。既存の`selectOnly`/`gizmoOp`による4状態（Select/Move/Resize/Rotate、常にどれか1つがアクティブ）に対し、5つ目の状態として追加。
- `isSelectMode()`/`isMoveMode()`/`isResizeMode()`/`isRotateMode()`全てに`!toolNone`の条件を追加し、`toolNone`が真の間はどれも偽になるようにした。新規`isNoToolMode()`（toolNoneそのもの）と`isGizmoMode()`（Move/Resize/Rotateのいずれか）のクエリも追加。
- クリック処理の入り口（`ViewportPanel::onRender()`内、「クリック処理: 選択 & ドラッグ開始」ブロックの条件）に`!isNoToolMode()`を追加し、無操作中はクリックしても選択もドラッグ開始も一切発生しないようにした。ボックス選択（`isSelectMode()`をチェック済み）や自由移動ドラッグ（`isMoveMode()`をチェック済み）は、上記の`is*Mode()`変更により自動的に無操作中は無効化される。
- ギズモのオーバーレイ描画条件を`!isSelectMode()`から`isGizmoMode()`（Move/Resize/Rotateのいずれか）に変更。旧条件のままだと`toolNone`中に`isSelectMode()`が偽になりギズモが誤って表示されてしまうため。
- `EditorManager::renderToolbar()`の4ボタンを、「既にアクティブなモードのボタンを押したら`toolNone = true`、そうでなければ従来通りそのモードに切り替えて`toolNone = false`」というトグル処理に書き換えた。

### なぜそうしたか
- ユーザーヒアリングで「無操作」は本当に何もしない（クリックしても選択すら変わらない、カメラ操作のみ）状態であることと、メイン・セカンダリ両方のビューポートに適用することを確認済み。
- `renderToolbar()`は`GetFocusedViewport()`で取得した`activeViewport`（メインまたはセカンダリ）に対して同じボタンコードを使う構造だったため、`ViewportPanel`側にトグル状態を持たせるだけで自然にメイン・セカンダリ両方に適用される。
- 状態表現を丸ごとenumに置き換える大きな書き換えは避け、既存の`selectOnly`/`gizmoOp`を残したまま`toolNone`を追加する最小変更にした（CLAUDE.mdのスコープ厳守方針）。

### 未解決・保留
- 実機での動作確認（同じボタンをもう一度押すと無操作になり、クリックしても選択が変わらないこと）はユーザーに委ねる（GUI自動スモークテスト禁止のため）。

---

## 2026-07-08 Weldグループ壊れ修正 + BaseCubeクローン漏れ修正 + 浮力の面分散化

### 指示内容
readme.mdの「物理エンジン関係」Todo3件を対応するよう依頼された:
1. マテリアルが違うと溶接できない問題の修正(水上でテストした結果)
2. マテリアルがBaseCube系でクローン時に保存されていない(他もある可能性あり)
3. 浮力の改善(一点集中→面に分散、アセンブリ剛体も対応)

Plan modeで、CLAUDE.mdの指示によりExplore/PlanエージェントもTaskツールも使わず、Grep/Readで自分で調査してから実装した。

### 何をしたか

**1. Weldグループ壊れ問題（`src/Core/Physics.cpp`のみ変更）**
- `Physics::recreateActor()`冒頭に、対象キューブの`actor`が他の`cubes`エントリーと共有されているか（＝Weld compoundのメンバーか）を判定するロジックを追加。共有されていれば（`sharedGroup.size() > 1`）、個別のremove/release/createActorではなく`rebuildGroup()`にグループ全体の再構築を委譲して`return`。共有されていない場合は既存の単独アクター処理のまま変更なし。

**2. BaseCube系クローン漏れ修正（8ファイル: `Cube.cpp`/`Truss.cpp`/`Seat.cpp`/`MeshCube.cpp`/`Sphere.cpp`/`Cylinder.cpp`/`TriangularPrism.cpp`/`LiquidCube.cpp`）**
- 各`clone()`の`CanCollide`コピー行の直後に、`material`/`MassDensity`/`CastShadow`/`Unlit`/`UseTriplanar`/`TextureScale`のコピーを追加（6項目×8ファイル）。`LockFlags`はスクリプト/UIから設定されない内部専用フィールド（Humanoid初期化のみが設定）のため対象外とした。

**3. 浮力の面分散化（`src/Core/Physics.cpp`の`applyBuoyancy()`のみ変更）**
- `BUOYANCY_SAMPLE_RES = 3`（3×3×3＝27点）のグリッドサンプリング定数を追加。
- 合計浮力（`weightedV` = Σ液体重なり体積×Density）は既存のAABB重なり体積計算のまま変更せず、力の適用点だけをキューブのAABB内27点のグリッドサンプル点に分散。各サンプル点がどの液体AABB内にあるかを判定し、重み比率で`weightedV`を配分して`physx::PxRigidBodyExt::addForceAtPos()`で各点に加える（既存の重心一点`addForce`を置き換え）。ダンピング計算（`frac`）は既存のAABB重なり体積ベースのまま変更なし。

ビルド確認: `python build.py build`で成功（Recubin.exe/RecubinEngine.exe/RecubinTest.exe全て生成）。

### なぜそうしたか

- **Weldバグの原因特定の経緯**: readme.mdの文言「マテリアルが違うと溶接できない」を字面通り受け取ると、Weld作成時にマテリアルの違いで拒否される分岐を探すことになるが、Physics.cpp/Weld.cpp/rebuildGroupを調査してもそのような分岐は一切見つからなかった（PxMaterialは形状ごとに独立して割り当てられ、Weld自体はマテリアルを一切参照しない）。AskUserQuestionでユーザーに実際の症状を確認したところ、「新規Weldの作成自体は成立するが、既に溶接済みのキューブのマテリアルを変更すると暗黙的に解除される」という、readmeの文言よりも正確な症状が判明した。
- **根本原因**: `BaseCube::setAnchored()`/`setMaterial()`/`setMassDensity()`が共通で呼ぶ`Physics::recreateActor()`が、`cube->actor`を常に「このキューブ専用の単独アクター」と仮定していた。Weldで結合されたキューブは`cube->actor`がcompound（複数キューブ共有のPxRigidDynamic）を指すため、素朴なremove/release/createActorはcompound全体を破棄し、変更対象のキューブだけ新しい単独アクターを作り直す（＝グループから抜ける）。さらに他メンバーの`cube->actor`（BaseCube自身が持つ生ポインタ）は誰も更新しないため、解放済みcompoundを指すダングリングポインタとして残る潜在的UAFバグも同時に発見した。この不具合はsetMaterialに限らずsetAnchored/setMassDensity/setSize（リサイズ）にも共通する経路であることも確認した。
- **修正方式の選択**: 新規にグラフ探索やWeld専用の特別処理を書くのではなく、既存の`rebuildGroup()`（Weld作成時に使われているグループ再構築処理）にそのまま委譲する方式を選んだ。`rebuildGroup()`は各メンバーの`material`/`MassDensity`/`Anchored`/`LockFlags`を都度読み直してcompoundを作り直し、`cubes`/`m_constraints`（Weldの`m_compound`、Rope/Rod/Motorの再生成）の同期まで内部で完結するため、追加コード量が最小で済み、既存のテスト済みロジックを再利用できる。
- **クローン漏れの調査**: LiquidCube::clone()でmaterialが未コピーであることに気づいた後、他の全BaseCube派生クラスのclone()を確認したところ、7クラス全てで同一パターン（Name/Color/Anchored/CanCollide/cframeのみコピー）が繰り返されていることが判明。readmeの「他もあるかもしれない」はこれを指しており、材質だけでなくMassDensity/CastShadow/Unlit/UseTriplanar/TextureScaleも全クラスで漏れていた。
- **修正スタイルの選択**: 8ファイルに同じ6行を追加する方式（重複コードの反復）を採用し、BaseCubeに共有ヘルパーメソッドを新設する案は取らなかった。CLAUDE.mdの「最適化・リファクタは明示的に指示された場合のみ行う」「より良い設計に勝手に置き換えない」という原則と、既存コード自体が「各クラスで同じ5行を個別にコピーする」スタイルを既に採用していたことに合わせた判断。
- **浮力の分散方式の選択**: グリッドサンプリング方式（3×3×3点）と、水没体積重心への単一点適用方式の2案をAskUserQuestionでユーザーに提示。単一点でも重心からずれた位置に適用点を置けば理論上はトルクが発生し転覆問題自体は解消するが、readmeの「面に分散させ」という文言に字義通り忠実ではない。ユーザーはグリッドサンプリング方式を選択した。
- **合計力の計算方法**: グリッドサンプリングによる離散化誤差が浮力の総量（Archimedesの原理との一致）に影響しないよう、合計力自体は既存の連続的なAABB重なり体積計算（`weightedV`）をそのまま使い、グリッドは「どこに配分するか」の比率計算のみに使う設計にした。これにより総浮力の正確性を保ったまま、水没箇所に応じた偏り（トルク）だけを新たに発生させられる。
- **アセンブリ対応への追加コード不要と判断した理由**: `applyBuoyancy()`は元々`cubes`内の各BaseCubeエントリー単位でループしており、Weld compoundのメンバーであっても各キューブが自分自身の位置情報を使って独立に浮力計算・力印加を行う構造だった。したがってaddForceAtPos化により各メンバーが自分の位置でグリッドサンプリングして力を加えるだけで、アセンブリ全体としても自然に分散が成立するため、特別な分岐追加は不要と判断した。

### どういう経緯か

1. readme.mdの物理エンジン関係Todo3件の計画を依頼される。IDE選択範囲（readme.mdの浮力改善Todoの詳細説明部分）も併せて提示された。
2. Plan mode開始。CLAUDE.mdの指示（計画はセッション自身が行う、サブエージェントに外注しない）に従い、Explore/PlanエージェントもTaskツールも使わずGrep/Readで自分で調査。
3. まずreadme.md/progress.mdを読んで前提を確認。次にPhysics.cpp/BaseCube.cpp/Weld.cpp/LiquidCube.cpp/Material.hppを読み込み、3件それぞれの現状コードを把握。
4. Weldバグ（1番目）について、Weld.cpp/rebuildGroup/attachShapeToCompound/getOrCreateMaterial/SceneHierarchyPanel.cpp/PropertiesPanel.cpp/Humanoid.cpp/LuauEngine.cpp/SceneLoader.cppを広く調査したが、マテリアルの違いでWeldを拒否・失敗させる分岐はコード上に一切見つからなかった。
5. これ以上の憶測での実装はリスクが高いと判断し、AskUserQuestionでユーザーに実際の症状を確認。ユーザーから「新規Weld作成は成立するが、既に溶接済みの状態でマテリアルを変更すると暗黙的に解除される」という、より正確な症状とデバッグログが提供された。
6. この情報を元に`BaseCube::setMaterial()`→`Physics::recreateActor()`の経路を再確認し、compound共有を考慮していない実装バグだと特定。
7. 同時にAskUserQuestionで浮力分散方式（グリッドサンプリング vs 単一点）についても確認し、グリッドサンプリング方式が選ばれた。
8. clone()漏れについては、LiquidCube.cppを読んだ時点でmaterial未コピーに気づき、他の全BaseCube派生クラスのclone()実装（Cube/Truss/Seat/MeshCube/Sphere/Cylinder/TriangularPrism）を横断的に確認して同一パターンの漏れを確認。
9. PxRigidBodyExt.hに`addForceAtPos`が既に存在することを確認し、Vector3.hppの演算子（componentwise `operator*`含む）が浮力分散の実装に必要な演算をサポートしていることも確認した上でプランを作成。
10. プランファイルを作成しExitPlanModeでユーザー承認を得てから実装開始。
11. 実装順序: `Physics::recreateActor()`修正→8ファイルのclone()修正→`applyBuoyancy()`のグリッドサンプリング化→`python build.py build`でビルド確認。ビルド成功。

### 未解決・保留

- 実機確認（Weldグループの安定性、クローンでのプロパティ保持、船の転覆耐性）は、GUI自動スモークテスト禁止方針のため、ビルド成功の確認までに留めた。次回セッション冒頭で以下をユーザーに確認する必要がある:
  - 2キューブをWeldで結合→Cube側のMaterialType/Anchored/MassDensityを変更→グループが分離せずクラッシュ/警告も出ないか
  - MaterialType変更済みのBaseCube系を複製し、複製後もMaterial/MassDensity/CastShadow等が保持されているか
  - 複数キューブのWeldアセンブリ（船）をLiquidCubeに傾けて浮かべ、以前より転覆しにくく傾きから復元するか
- 浮力のグリッド解像度`BUOYANCY_SAMPLE_RES = 3`（27点）は固定値で実装した。実機確認の結果、精度不足（トルクが弱すぎる）や逆に重すぎる（パフォーマンス懸念）と判断された場合は調整が必要。
- 調査中に気づいた別事象: Playモードの開始・停止を繰り返すと`[SceneLoader] Weld "Weld": cube not found`という警告が毎回コンソールに出力される。今回のTodo3件とは無関係と判断し対応しなかったが、原因は未調査。将来readme.mdに追加Todoとして起票する候補。
- `setSize()`（リサイズ）経路も`recreateActor()`を通るため今回の修正の恩恵を受けるはずだが、リサイズ動作自体の実機確認は行っていない。

### 試して失敗した/やめた方法

- **Weldバグの原因を「マテリアルの違いによるWeld拒否ロジック」だと決め打ちして実装を始める案**: readme.mdの文言を字面通り解釈すると自然にこの仮説に至るが、Weld.cpp/Physics.cpp/rebuildGroup等を実際に読んでもそのような分岐はコード上に存在しなかった。もしここで憶測のまま「material一致チェックを追加する」といった見当違いの実装をしていたら、本当の原因（recreateActorのcompound非対応）を見逃していた。CLAUDE.mdの「迷ったら実装せず質問する」方針に従いユーザーに確認したことで、正しい原因に到達できた。
- **浮力を水没体積の重心1点に単一のaddForceAtPosで適用する案**: 転覆問題自体は解決できる（理論上は正しいトルクが発生する）が、readmeの「力を面に分散させ」という要求に字義通りではないため、より計算コストの高いグリッドサンプリング案と両方提示してユーザーに選んでもらった。

### 暗黙仕様の発見（spec.mdに無い挙動）

- **`Physics::recreateActor()`は`cube->actor`を常に「このキューブ専用の単独アクター」と暗黙的に仮定しており、Weld compoundの共有アクターケースを考慮していなかった**。`BaseCube::setAnchored()`/`setMaterial()`/`setMassDensity()`は全てこの関数を経由するため、Weldグループのメンバーに対してこれらのプロパティを変更すると、暗黙的にそのキューブがグループから外れ、かつ他メンバーの`actor`ポインタがダングリングになるという副作用があった。今後`recreateActor()`を呼ぶ新しいプロパティセッターを追加する際は、この関数が既にcompound共有を考慮済み（`rebuildGroup`に委譲する分岐がある）という前提で問題ないが、直接`scene->removeActor`/`release`を呼ぶような新規コードを書く場合は、同じ「共有アクターかどうか」の判定が必要になる。
- **全BaseCube派生クラスのclone()実装は、`Name`/`Color`/`Anchored`/`CanCollide`/`cframe`のみを個別にコピーする手書きスタイルで統一されており、BaseCubeに新しいプロパティを追加した際にclone()側への反映を強制する仕組みが無い**。今回`material`/`MassDensity`/`CastShadow`/`Unlit`/`UseTriplanar`/`TextureScale`の6項目が7クラス全てで漏れていたのはこのため。今後BaseCubeに新規プロパティを追加する場合、この8ファイルのclone()全てに手動で反映を追加する必要があることを意識する必要がある（BaseCubeは意図的に手書き維持されているクラスのため、PropertyRegistryへの移行対象外であり、この手作業は今後も続く）。
- **`Physics::applyBuoyancy()`は`cubes`配列を「BaseCubeエントリー単位」でループしており、Weld compoundのメンバーは自分のactor（共有compound）に対して個別に力を加える構造になっている**。この構造のおかげで、今回の浮力分散化はアセンブリ（Weld）に対しても特別な分岐を追加せずに自然に対応できた。今後、浮力以外の「キューブごとの外力」を追加する場合も、同じくcubesエントリー単位でループする限りアセンブリ対応が自動的に得られる設計になっている。

---

## 2026-07-09 setParentキーコリジョン警告の修正 + Name変更経路の一本化

### 指示内容
readme.mdの3件のTodo（IDE選択範囲で提示）:
1. `[RCBN_WARN][Instance.cpp:45] setParent: Key collision for '...' in System. Overwriting existing child.` の修正
2. キーコリジョンによるバグ/メモリリーク防止（エラーではなく警告+ずらす）。ユーザー入力にバリデーションが無い
3. System配下がリセットされないことによるインスタンスの意図しない残存コピーの修正

Plan modeで、CLAUDE.mdの指示によりExplore/PlanエージェントもTaskツールも使わずGrep/Readで自分で調査してから実装した。

### 何をしたか

**1. `include/Instances/Instance.hpp` / `src/Instances/Instance.cpp`**
- 新規 `Instance::renameTo(const std::string& newName)` を追加。同名ならno-op、親が無ければNameを書き換えるだけ、親がいれば兄弟と衝突するかを確認し、衝突するなら`uniqueChildName()`（新設のファイルローカルstatic関数、`System::addChild()`のWorkspace用ロジックと同じ`base`+連番の命名規則）で一意名を作ってから`children`マップを更新し、`RCBN_WARN`で警告（衝突しても処理は継続、エラーにはしない）。
- `Instance::setProperty("Name", ...)`の中身を`renameTo()`呼び出しに置き換え。
- `Instance::setParent()`の衝突分岐を変更。以前は衝突した既存の子の`Parent`を外してから上書きしていたが、代わりに**新しく追加する側**（`this->Name`）を`uniqueChildName()`で一意化してから挿入する方式に変更。既存の子には一切触れない。

**2. `src/Core/LuauEngine_Dispatch.cpp`**
- `SetterTable["Instance"]["Name"]`（親のchildrenマップを整合させる体裁はあったが衝突チェックが無かった）を`obj->renameTo(...)`呼び出しに置き換え。

**3. `src/Editor/SceneHierarchyPanel.cpp`**
- F2リネーム確定処理（`inst->Name = after;`、親のchildrenマップを一切更新していなかった）を`inst->renameTo(after);`に変更。Undo記録用の`after`は呼び出し後の`inst->Name`を使うよう修正（衝突でずらされた場合に備えて）。

**4. `src/Editor/PropertiesPanel.cpp`**
- PropertiesパネルのNameフィールド（`ImGui::InputText`が変化するたび、つまり1キー入力ごとに`inst->Name = ...`していた。同じく親のchildrenマップ未更新）を`inst->renameTo(...)`に変更。Undo記録用の`after`は既存コードがdeactivation時に`inst->Name`を再取得する実装だったため変更不要（自然に反映される）。

**5. `include/Editor/CommandHistory.hpp`**
- `RenameInstanceCommand::execute()/undo()`（`m_target->Name = ...`の直接代入、childrenマップ未更新）を`m_target->renameTo(...)`に変更。

**6. `src/main.cpp`**
- 新規static関数`resetSystemForReload(system, user)`を追加（既存の`removeWorkspacesFromSystem`のすぐ後ろ）。`system->getChildren()`をスナップショットし、`user`以外の全ての子を`system->removeChild()`で除去する。
- Play→Stop復元処理（`!isPlaying && wasPlaying`ブロック）とLoadボタンによるリロード処理（`ed->pendingLoadPath`ブロック）の2箇所で、`removeWorkspacesFromSystem(system, workspaces)`の呼び出しを`resetSystemForReload(system, user)`に置き換え。`resetTerrainStreamers`/`clearWorkspacePhysics`（Workspace個別の物理後始末）はそのまま維持し、順序も変更していない。
- 上記2箇所それぞれで、`resetSystemForReload`呼び出しの直前に`ed->hierarchyPanel->selectedInstance = nullptr;`を追加（アプリ最終終了処理で既にある同じパターンを踏襲）。
- 最終アプリ終了処理（`system.reset()`直前）の既存`removeWorkspacesFromSystem`呼び出しは変更していない（今回のバグと無関係な単純な後片付けのため）。

ビルド確認: `python build.py build`で成功（Recubin.exe/RecubinEngine.exe/RecubinTest.exe全て生成）。

### なぜそうしたか

- **根本原因の特定経緯**: 警告メッセージが常に「in System」であることから、`System`直下で何かが再読込のたびに重複していると推測。`src/main.cpp`の`removeWorkspacesFromSystem()`を読んだところ、**Workspaceだけ**をSystemから除去しており、`PathfindingService`（spec.mdでSystem直下に自動生成される）や`StarterCharacter`（spec.mdで「System直下に置く」と明記）は除去対象外だったと判明。`SceneLoader::saveScene()`はSystemの全子要素（Workspace以外も）を保存するため、再読込時にこれらのクラスの新規インスタンスが`parseInstance()`で生成され、除去されずに残っていた旧インスタンスと名前が衝突していた。
- **`resetSystemForReload`を「クラス名で個別に除去」ではなく「user以外を全部除去」にした理由**: 当初は`PathfindingService`/`StarterCharacter`を名指しで除去する案も考えたが、`SceneLoader::saveScene()`がSystemの子を無差別に全部シリアライズする以上、将来新しいクラスがSystem直下に追加された場合も同じバグが再発する。「再読込のたびにYAMLから作り直されるものは全部消してよい」という一般則（`user`だけが例外でSystem/User自体はシーンをまたいで使い回す設計）の方が頑健と判断し、AskUserQuestionを使わず自分の判断で採用した（`PathfindingService`のデストラクタ・`StarterCharacter`の実装を確認し、バックグラウンドスレッドやキャッシュ等の特別な後始末が不要なことを事前に確認済み）。
- **`setParent()`衝突時の挙動を「上書き」から「新規側をずらす」に変更した理由**: AskUserQuestionでユーザーに確認し、「既存の子を上書きする」現行方式ではなく「新しく追加する側の名前を自動でずらす」方式を選択された。理由は、既存の子（他から参照が残っている可能性がある「本物」）を壊さない方が安全で、`System::addChild()`のWorkspace用ロジックと同じ考え方に統一できるため。
- **Name変更経路を5箇所とも共通ヘルパー(`renameTo()`)に統一した理由**: AskUserQuestionでユーザーに確認済み。調査の過程で、`Instance::setProperty`とLuauのNameセッターは「親のchildrenマップを更新する体裁はあるが衝突チェックが無い」バグ、SceneHierarchyPanel/PropertiesPanel/RenameInstanceCommandの3箇所は「そもそも親のchildrenマップを一切更新しない（マップのキーとNameが食い違う）」というさらに深刻な別バグを抱えていることが分かった。5箇所に同じ非自明な修正（衝突チェック+一意化+マップ整合）を重複実装するより、共有メソッドに一本化する方が正しいと判断（CLAUDE.mdの「最適化・リファクタは指示された場合のみ」とは緊張関係にあるが、単純な重複実装ではなく「同一バグの重複修正を避ける」ための最小限の共通化として許容範囲と判断し、事前にユーザーに確認した）。
- **PropertiesPanelのName入力を1キー入力ごとに`renameTo()`を呼ぶ形のまま残した理由**: 元々のコードも1キー入力ごとに`inst->Name`を直接書き換えていたため、確定時（deactivation）のみ適用する方式に変更すると既存の「入力中も他パネルにライブ反映される」挙動を変えてしまう。挙動変更は指示されていないため、今回はタイミングを変えず「同じタイミングで呼ぶ関数を安全なものに差し替える」だけに留めた。ただし衝突する中間文字列を打った場合、確定前に警告ログが出たり一時的に連番が付与されたりする可能性があり、必要ならF2リネーム同様「確定時のみ適用」に変更する余地がある（未解決・保留に記載）。

### どういう経緯か

1. IDE選択範囲（readme.mdの3件のTodo）を提示され、Plan modeで原因調査と修正計画の作成を依頼される。
2. progress.md（前回セッションの最新分）を読み、直前まで物理エンジン関係の修正が続いていたことを確認。
3. `Instance.hpp`/`Instance.cpp`を読み、`setParent()`の衝突ログが出る箇所とその挙動（上書き）を確認。
4. 警告が常に「in System」である点から、`System`直下での重複が原因と仮説を立て、`System.hpp`/`System.cpp`/`doc/Instances/System.md`を読み、`System::addChild()`がWorkspaceのみ衝突回避していることを確認。
5. `src/Core/SceneLoader.cpp`を読み、シングルトン（System/Workspace/User）だけがマージされ、他のクラスは再読込のたびに新規生成される仕組みを確認。
6. `src/Core/SceneRuntime.cpp`（`loadAndBind()`）と`src/main.cpp`（Play/Stop復元処理・Loadボタン処理・`removeWorkspacesFromSystem()`）を読み、Workspaceだけがリロード前に除去されている実装を発見し、根本原因を特定。
7. `PathfindingService.hpp/cpp`・`StarterCharacter.hpp`を読み、除去してもバックグラウンド処理等の特別な後始末が不要なことを確認。
8. Name変更経路を横断的に調査するため`SceneHierarchyPanel.cpp`/`PropertiesPanel.cpp`/`CommandHistory.hpp`/`LuauEngine_Dispatch.cpp`をgrep/readし、5箇所全てに何らかの不備（衝突チェック無し、またはマップ未更新）があることを発見。
9. AskUserQuestionで2点（Name変更5箇所を共通ヘルパーで一本化するか、setParent衝突時の挙動を「新規側リネーム」に統一するか）を確認し、どちらも推奨案（一本化する／新規側をリネームする）が選ばれた。
10. プランファイルを作成しExitPlanModeでユーザー承認を得てから実装開始。
11. 実装順序: `Instance.hpp/cpp`（`renameTo()`新設+`setParent()`修正）→`LuauEngine_Dispatch.cpp`→`SceneHierarchyPanel.cpp`→`PropertiesPanel.cpp`→`CommandHistory.hpp`→`main.cpp`（`resetSystemForReload()`新設+2箇所差し替え）→`python build.py build`でビルド確認。ビルド成功。
12. readme.mdの該当3行をチェック済みに変更。

### 試して失敗した/検討したが採らなかった方法

- **`resetSystemForReload`で`PathfindingService`/`StarterCharacter`をクラス名で個別に除去する案**: 動作はするが、将来System直下に追加される新しいクラスに対して同じ抜け漏れが再発しうる。「user以外は全部消す」という一般則の方が頑健と判断し、こちらを採らなかった（実装はしていない、検討段階で却下）。
- **PropertiesPanelのName入力を「確定時のみ`renameTo()`を呼ぶ」方式に変更する案**: F2リネームと統一感は出るが、元々の「入力中も他パネルにライブ反映される」挙動を変えてしまうため、今回は指示されていない挙動変更として見送った。

### 未解決・保留

- 実機での動作確認（Playモードの開始/終了を繰り返しても警告が出ないこと、Loadボタンでのシーン再読込、F2リネーム・PropertiesパネルのName編集で衝突時に自動で名前がずれ警告が出ること、リネームのUndo/Redoが正しく動くこと）は、GUI自動スモークテスト禁止方針のため、ビルド成功の確認までに留めた。次回セッション冒頭で確認が必要。
- PropertiesパネルのName入力は1キー入力ごとに`renameTo()`が呼ばれる実装のまま残したため、衝突する中間文字列を経由して入力すると、確定前に警告ログや一時的な連番付与が発生しうる（実害は無いはずだが未検証）。気になる場合はF2リネームと同様「確定時のみ適用」に変更する余地がある。
- `doc/Instances/Instance.md`は今回変更した`setParent()`/`renameTo()`の挙動を反映しておらず、そもそも`children`の型（`Instance*`と記載、実際は`shared_ptr`）など既存の記述も古い。今回のTodoに明記が無かったため更新していない。
- `resetSystemForReload()`は「user以外の全てのSystem直下の子を無条件で除去」するため、もしユーザーが将来Systemに手動で何か恒久的に保持したい子を追加した場合（現状のクラス構成では想定されていない）、リロードのたびに消えてシーンYAMLの内容だけが復元される点に注意が必要。

### 暗黙仕様の発見（spec.mdに無い挙動）

- **`SceneLoader::saveScene()`はSystemの子を無差別に全部シリアライズするが、再読込側（`main.cpp`の`removeWorkspacesFromSystem()`）はWorkspaceしか除去していなかった**。この非対称性（保存は全部・復元前クリアはWorkspaceのみ）が、`PathfindingService`/`StarterCharacter`のような「Workspace以外だがSystem直下に自動生成される」クラスで再読込のたびにキー衝突と残存コピーを生む原因だった。今後System直下に新しいクラス（シングルトンではないもの）を追加する場合、`resetSystemForReload()`が「user以外は無条件除去」という一般則でカバーするため個別対応は不要なはずだが、もし新クラスがuser同様「シーンをまたいで使い回すべき」性質を持つ場合は、この関数の例外リストに明示的に追加する必要がある。
- **`Instance::setProperty("Name",...)`とLuauの`SetterTable["Instance"]["Name"]`は、親の`children`マップを更新する体裁を取っていたが、実際には衝突チェックが皆無で、衝突時は既存の兄弟の`Parent`を外さないまま黙って上書きしていた**。この状態で兄弟インスタンスが後で（他から参照が無くなり）デストラクトされると、`Instance::~Instance()`の`assert(Parent.expired())`に引っかかる潜在的なクラッシュ経路だった（今回のクラッシュ自体は未発生・未確認だが、コードレビューで発見）。
- **エディターUIのリネーム経路（`SceneHierarchyPanel.cpp`のF2リネーム、`PropertiesPanel.cpp`のNameフィールド、`CommandHistory.hpp`の`RenameInstanceCommand`）は、いずれも`inst->Name`を直接代入するだけで、親の`children`マップを一切更新していなかった**。つまりリネーム後は「マップのキー（旧名）」と「実際の`Name`（新名）」が食い違っており、`getChild(新名)`は失敗し`getChild(旧名)`だけが（本来もう存在しないはずの名前で）ヒットするという不整合状態になっていた。今後同種のリネームUIを追加する場合は、必ず`Instance::renameTo()`を経由させる必要がある。

### 追記（同日）: 上記修正がUser.Inventoryの無限増殖を顕在化させた問題の追加修正

上記の`setParent()`修正（上書き→リネームに変更）をビルド後、ユーザーから「Userのインベントリだけが無限増殖する」と報告（`[RCBN_WARN][Instance.cpp:59] setParent: Key collision for 'Inventory' in User. Renamed new child to 'Inventory2' ...`）。

**原因**: `User::Inventory`（`include/Core/User.hpp`のメンバ、コンストラクタで`std::make_shared<Folder>()`済み）は`SceneLoader`のシングルトン機構（System/User/Workspaceのみ対応）に登録されていない。そのため、`main.cpp`起動時に`user->initializeInventory()`を呼んで**先に**空のInventoryをUserの子として付けてから`SceneRuntime::loadAndBind()`でシーンYAMLを読み込むと、YAML内のUser直下のFolder("Inventory")（Inventoryは中身のTool込みで毎回シーンに保存されるため、保存済みシーンには必ず含まれる）が新規インスタンスとして生成され、既存の「Inventory」と名前が衝突していた。
旧コード（今回のセッション最初の修正前）ではこの衝突時に**既存の子を上書き**していたため、木構造上は読み込んだInventoryに正しく差し替わる一方、`user->Inventory`（ゲームロジックが参照する側のポインタ）は差し替わらず、孤立した空Folderを指したままになる**という別の潜在バグ**（`addToolToSlot()`等がツリー上の実体と食い違ったオブジェクトを操作する）を抱えていた。今回の修正で「上書き」を「新規側リネーム」に変えたことで、この隠れていた衝突が毎回の再読み込みで可視化され、かつ`user->Inventory`ポインタは相変わらず更新されないため、リロードのたびに「Inventory」「Inventory2」「Inventory3」…と際限なく増え続ける形で表面化した。

**修正内容**:
- `src/Core/SceneRuntime.cpp`の`loadAndBind()`: `SceneLoader::loadScene()`実行後、`user->children`に"Inventory"（Folder型）が見つかればそれを`user->Inventory`として採用（tree上の実体とポインタを同期）、見つからなければ従来通り`user->initializeInventory()`で空Folderを付ける、という処理を追加。
- `src/main.cpp`: 起動時の先読み`user->initializeInventory()`呼び出しを削除（`loadAndBind()`側に一本化。先に空Inventoryを付けてしまうと初回ロードでも衝突が起きるため）。
- `src/main.cpp`の`resetSystemForReload()`: Systemの子だけでなく、**userの子（Inventory）もリロード前にクリア**するよう拡張。これが無いと、2回目以降のPlay/Stop・Loadのたびに前回セッションのInventoryと新規ロード分がまたキー衝突してしまう。

ビルド確認: `python build.py build`で成功。

**未解決・保留（追加分）**:
- Load機能で「Userセクションが無い、またはInventoryが未保存の旧シーン」を読み込んだ場合、`resetSystemForReload()`でuserの子をクリアした後に`initializeInventory()`が呼ばれ、**クリア直前まで使っていた（前セッションの）Inventoryオブジェクトがそのまま再アタッチされる**（真っ新な空Folderにはならない）。意図的にそう設計したが、実機で「Load後にInventoryの中身が意図せず残る」という体感が出た場合は、`resetSystemForReload()`内で`user->Inventory`ごと新しい空Folderに差し替える対応が必要になる可能性がある。
- 実機確認（Play/Stop・Load・シーン間の切り替えを繰り返してもInventoryが増殖しないこと、装備中のTool追跡が壊れないこと）はユーザーに委ねる。

---

## 2026-07-09 ローカライズ基盤導入 + BaseResolutionのカテゴリ修正

### 指示内容
readme.mdのTodo2件（IDE選択範囲で提示）:
1. 「英語/日本語混ざりをやめ、ローカライズテーブルを作成し、文字列を外部化する」（プロパティ欄とクラス名は翻訳除外、ホバーテキスト等は翻訳対象）
2. 「BaseResolutionは安全マージンでないのでそのカテゴリの線の上に出す」

Plan modeで、CLAUDE.mdの指示によりExplore/PlanエージェントもTaskツールも使わず自分でGrep/Readして調査してから実装した。

### 何をしたか

**1. BaseResolutionのカテゴリ修正**（`src/Editor/PropertiesPanel.cpp`）
- `System`クラスは`PropertyRegistry`に`BaseResolution`のみ登録されており、`renderSchemaInspector(inst, "System", ...)`の呼び出しが`SeparatorText("System (Safety Limits)")`の**内側**にあったため、BaseResolutionがSafety Limits扱いになっていた。`SeparatorText("System")`を新設し、その下で`renderSchemaInspector`を呼んでから`SeparatorText("System (Safety Limits)")`（MaxClonesPerFrame/MaxRestartsPerFrame/ScriptLoopTimeoutSecondsのみ）を続ける2段構成に変更。

**2. ローカライズ基盤新設**
- 新規 `include/Editor/Localization.hpp` / `src/Editor/Localization.cpp`。`Loc::Lang{JA,EN}`、`Loc::LocKey`（111個のキー、宣言順とテーブル順を1対1対応させる設計）、`Loc::t(key)`/`Loc::setLanguage()`/`Loc::getLanguage()`を実装。テーブルは`std::array<Entry, LocKey::Count>`をenum宣言順と同じ順で初期化する方式（インデックス直参照でO(1)、hashmapより高速・シンプル）。CMakeの`file(GLOB_RECURSE ALL_SOURCES "src/*.cpp")`で自動的にビルド対象に入るため、ビルドシステムは触っていない。

**3. 対象6ファイルの文字列外部化**
- `src/Editor/PropertiesPanel.cpp`: 「参照...」ボタン群（`locId()`という小ヘルパーを追加して`Loc::t(key) + "##suffix"`を毎回書かずに済むようにした）、Terrain再生成の確認ポップアップ一式、ブラシ編集UI（半径/モード/Lower・Smooth・Raiseの選択肢等）、Sound再生ボタン、ピッカーの案内文言などを`Loc::t()`化。**除外**: `renderSchemaInspector`のスキーマ駆動ラベルと、手書きの`Position`/`Anchored`/`Color`等プロパティ名と一致するラベル、および`SeparatorText`のカテゴリヘッダー（元々すべて英語で一貫していたため対象外）。
- `src/Editor/SceneHierarchyPanel.cpp`: Insertメニューのカテゴリ名6種+説明文、New Scriptダイアログ、コンテキストメニュー（Delete/Copy/Paste/Paste as Child、workspace切替系）を`Loc::t()`化。`tryInsertInstance<T>(..., "ClassName", ...)`のクラス名文字列自体は変更していない（クラス名は翻訳除外というルールのため）。
- `src/Editor/EditorManager.cpp`: メニューバー（File/View各項目）、3つのポップアップダイアログ（Play中シーン読込確認/未保存変更確認/Package Game）、ツールバー全般（Play/Pause/Stop、Select/Move/Resize/Rotate、スナップ系チェックボックス、Add Object/Save/Load）を`Loc::t()`化。**新規**: 「Settings」メニューを追加し、日本語/Englishのラジオ的MenuItemで`Loc::setLanguage()`を呼べるようにした。パネルタイトル6つ（Explorer/Properties/Viewport/Content Browser/Console/Animation Editor）は、EditorManager::onRender()内でパネルの`onRender()`を呼ぶ直前に毎フレーム`panel->title = Loc::t(...) + "###固定ID"`で組み立て直す方式に統一（後述の理由により各パネル.cppには手を入れていない）。
- `src/Editor/AnimationEditorPanel.cpp` / `src/Editor/ConsolePanel.cpp` / `src/Editor/ContentBrowserPanel.cpp`: 残りのボタン・タブ名・ヒント文言を`Loc::t()`化。

ビルド確認: `python build.py build`で成功（Recubin.exe/RecubinEngine.exe/RecubinTest.exe全て生成、warningなし）。

### なぜそうしたか

- **除外ルールの境界**（AskUserQuestionで確認）: 「プロパティ欄」の除外対象を「スキーマ駆動のプロパティラベルのみ」ではなく、実際には「ImGuiウィジェットのラベルが実プロパティ名と完全一致する箇所全て」（手書きの`Position`/`Anchored`/`Color`等も含む）という運用ルールに拡張した。理由: readme.mdの意図は「プロパティ欄はスクリプトAPI/シリアライズ名と一致させ続けたい」ことにあり、schema駆動かどうかは実装上の偶然に過ぎないため。`SeparatorText`のカテゴリヘッダーも同じ理由で除外（元々100%英語で一貫しており、混在問題自体が無かった）。
- **ウィンドウタイトルを各パネルの`onRender()`ではなくEditorManager.cppで一括設定した理由**: `ViewportPanel`クラスはメインビューポートとセカンダリビューポート（`EditorManager::openSecondaryViewport`が生成、ワークスペース名込みの動的タイトル+`###SecVP_<id>`を独自に持つ）で**同一クラスを共有**している。もし`ViewportPanel::onRender()`内で毎フレーム`title = Loc::t(PanelViewport) + "###Viewport"`と無条件上書きすると、セカンダリビューポートの動的タイトルを毎フレーム破壊してしまう。EditorManager側は「どれが主ビューポートか」を知っているため、そこでのみ上書きする方式にした。副作用として他5パネル（Explorer等）の`.cpp`ファイル自体は一切変更不要になった。
- **`###固定ID`パターンを採用した理由**: `EditorPanel::title`はそのまま`ImGui::Begin()`のID文字列としても使われており、`imgui.ini`のドッキングレイアウトはこの文字列をキーに保存される。単純に表示文言を差し替えるとID自体が変わり、言語切替のたびにドッキングレイアウトがリセットされてしまう。ImGuiの`###`はID計算時に`###`以降の部分文字列のみをハッシュ対象にする仕様のため、`###`の後ろを**変更前の英語タイトルそのまま**にすることで、新旧のID hashを完全一致させ、既存の`imgui.ini`との互換性を保った。この設計は`SecondaryViewportPanel`関連コードで既に使われていた`"表示文###固定ID"`パターン（`EditorManager::openSecondaryViewport`）を踏襲したもの。同じ理由で、3つのポップアップダイアログ（Unsaved Changes等）・Terrain再生成確認・New Scriptダイアログも`OpenPopup`/`BeginPopupModal`双方を`###固定ID`付きの文字列に統一した。
- **RCBN_WARN等のログメッセージ（Instance.cpp周りの日本語警告文等）は対象外とした**: readme.mdの指示は「エディターUIの英語/日本語混在」がテーマであり、開発者向けログ/警告文（ストリーム結合で動的内容を含む）はUIコピーとは性質が異なると判断。スコープを絞るため意図的に触れていない（今回の変更ファイルリストにも含めていない）。
- **PostEffectのType/Handモード等、複数箇所にある「Combo選択肢の配列」を翻訳対象外とした**: これらは"None"/"CRT"/"Right"/"Left"のように**既に100%英語で一貫**しており、混在問題が無い固定語彙（クラス名に近い性質）と判断し、除外ルールを拡張適用した。一方でTerrainブラシの`modeItems`（`"Lower（削る）"`等）は英語+日本語の混在そのものだったため、これは全体を1つの翻訳キーとして扱った。
- **「New Cube」等のAdd Objectドロップダウン項目を翻訳対象外とした**: これらは実質的に「クラス名を挿入する」メニュー項目であり、SceneHierarchyPanelのInsert Objectメニューにおけるクラス名除外ルールと同じ考え方を適用し、一貫性を優先した。

### どういう経緯か

1. IDE選択範囲（readme.mdの2件）を提示され、Plan modeで調査を依頼される。CLAUDE.mdの指示によりExplore/Planエージェントは使わず、Grep/Readで自分で調査。
2. `PropertiesPanel.cpp`の`System`欄を読み、`BaseResolution`原因を特定（`System.cpp`の`PropertyRegistry::registerClass("System", {...})`にBaseResolutionのみ登録されている点も確認）。
3. Editor全体で実際にUI文字列に日本語が混在している箇所をUnicode範囲の正規表現でgrep調査し、`PropertiesPanel.cpp`/`SceneHierarchyPanel.cpp`/`EditorManager.cpp`/`AnimationEditorPanel.cpp`の4ファイルに絞られることを確認（他パネルは日本語がコメントのみ）。
4. AskUserQuestionで4点確認: (a) テーブル形式=C++内蔵テーブル、(b) 言語切替=設定メニューUIも実装、(c) 対象範囲=全Editorパネルの文字列も含める（4ファイルのみでなく英語オンリーのパネルも）、(d) プロパティ欄の除外境界=スキーマ駆動ラベルのみ→実装時に「実プロパティ名と一致するラベル全て」に運用上拡張。
5. 全6ファイル（ConsolePanel.cpp/ContentBrowserPanel.cpp含む）のImGui呼び出しを洗い出し、111個の`LocKey`を設計してPlanファイルに記載、ExitPlanModeでユーザー承認を得た。
6. 実装順序: `Localization.hpp/cpp`新設→`EditorManager.cpp`（Settingsメニュー・パネルタイトル一括設定・メニューバー/ダイアログ/ツールバー）→`PropertiesPanel.cpp`（BaseResolution移動→文字列外部化）→`SceneHierarchyPanel.cpp`→`AnimationEditorPanel.cpp`→`ConsolePanel.cpp`→`ContentBrowserPanel.cpp`→`python build.py build`でビルド確認。ビルド成功（warningなし）。
7. `readme.md`の該当2行をチェック済みに変更。

### 試して失敗した/計画から変更した点

- **`ViewportPanel::onRender()`内で毎フレームタイトルを直接上書きする案**: 最初はこの方式を検討したが、`ViewportPanel`がメイン/セカンダリ両方で共有されるクラスだと気づき（`EditorManager::openSecondaryViewport`が同じ`ViewportPanel`型を`make_unique`している）、セカンダリの動的タイトルを破壊するため断念。EditorManager側の描画ループでメインパネルのみ上書きする方式に変更した。
- **Tool欄の`"→ %s"`（矢印記号）をASCIIの`"->"`に置き換えてしまった誤修正**: 「(未解決)」を訳す際に誤って矢印記号ごと書き換えてしまい、直後に気づいて`"\xe2\x86\x92 %s"`（元の`→`のUTF-8バイト列、EditorManager.cppの`\xc2\xb0`度記号と同じ書式）に戻した。ローカライズ対象でない記号まで巻き込まないよう、置換範囲を最小限にする必要があるという教訓。

### 未解決・保留

- 実機での動作確認（Settingsメニューから日本語⇔Englishを切り替えてボタン・ツールチップ・ポップアップ・パネルタイトルが正しく切り替わること、プロパティ欄とクラス名が言語に関わらず変化しないこと、BaseResolutionがSafety Limits欄の外に表示されること、既存の`imgui.ini`があるプロファイルでドッキングレイアウトが壊れないこと）は、GUI自動スモークテスト禁止方針のためビルド成功の確認までに留めた。次回セッション冒頭で確認が必要。
- 言語設定（`Loc::g_lang`）はプロセス内メモリのみに保持しており、永続化（次回起動時に前回選んだ言語を復元する）は未実装。今回のTodoにも明記が無かったためスコープ外としたが、将来「毎回Englishに戻ってしまう」という不満が出た場合は設定ファイル（現状エディター設定の永続化機構自体が存在しない）を新設する必要がある。
- `RCBN_WARN`等の日本語ログ/警告メッセージ（Instance.cpp、Attachment関連のコンソール警告等）は意図的に未対応のまま。将来「ログも含めて英語/日本語を統一したい」という話が出た場合は別タスクとして扱う必要がある。
- `Loc::LocKey`テーブルは111キューを手作業で1対1対応させる設計（enum宣言順とcppテーブル初期化順が一致している前提でO(1)アクセス）。将来キーを追加・並べ替える際、宣言順とテーブル順がずれると**コンパイルエラーにならずに誤った文言が表示される**静かなバグになりうる。今回はセクションごとの個数をPythonスクリプトで突き合わせて検証したが、今後保守する開発者もこの制約を意識する必要がある。

### 暗黙仕様の発見（spec.mdに無い挙動）

- **`EditorPanel::title`は表示文言とImGuiウィンドウID（`imgui.ini`のドッキングレイアウトのキー）を兼ねている**。ImGuiの`###`区切りは「###以降のみをID計算に使う」という仕様のため、表示文言だけを変えたい場合は`###`より前を自由に変更し、`###`より後ろ（旧タイトルそのもの）を固定することで、既存のドッキング状態を壊さずに表示だけ切り替えられる。この考え方は既に`EditorManager::openSecondaryViewport`で先例があったが、他のパネルには使われていなかった。今後動的にパネルタイトルを変更する新機能を追加する場合、同じ`"表示###固定ID"`パターンを踏襲する必要がある。
- **`ViewportPanel`クラスはメインビューポートとセカンダリビューポート（Insert→workspace右クリック→「新しいビューポートで開く」）の両方で同一クラスが使い回されている**（`SecondaryViewportPanel`という別クラスも存在するが、実際の「新しいビューポートで開く」機能からは呼ばれておらず、コード上未使用の可能性が高い）。ここを触る場合は「今操作しているのが主ビューポートかどうか」を呼び出し側（EditorManager）で判別する必要があり、`ViewportPanel`自身は区別する手段を持たない。
- **`System`クラスは`PropertyRegistry`に`BaseResolution`一つしか登録されておらず、`MaxClonesPerFrame`/`MaxRestartsPerFrame`/`ScriptLoopTimeoutSeconds`はスキーマ非経由の手書きプロパティ**（`System::setProperty`内で個別に分岐している）。「Safety Limits」という命名のカテゴリ見出しは、たまたま`renderSchemaInspector`呼び出しがその見出しの中にネストされていたために生まれた表示上のバグであり、`System.cpp`側のプロパティ定義自体には「どのカテゴリに属するか」という情報は存在しない。将来Systemに新しいプロパティを追加する際は、`PropertiesPanel.cpp`側でどちらのカテゴリに表示すべきかを毎回明示的に判断する必要がある。


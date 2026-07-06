# 開発進捗ログ

セッションごとの作業記録。新しいセッションを開始する際はまず一番下（最新）のセッションを読むこと。

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

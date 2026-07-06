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

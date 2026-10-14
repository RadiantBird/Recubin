# 開発進捗ログ

セッションごとの作業記録。新しいセッションを開始する際はまず一番下（最新）のセッションを読むこと。

---


## 2026-10-01 キャラクター周りのバグ修正（物理・座標・ジャンプ）

### 1. 何をしたか
- **MaintainVelocity(Torque)が摩擦で減衰する問題**: `Box3DPhysicsBackend.cpp` の `applyMaintainedVelocities()` に、ステップ後の角速度差分ぶんの回転を重心回りに加える補正を追加。YawForce(Torque+MaintainVelocity)を持つキャラは `CharacterRig::collectR6Bodies()` の全ボディを同じ剛体回転で補正する。
- **System.AutoSpawnPlayerCharacter**: `main.cpp` のエディタPlay開始処理で判定を追加（game_main.cppには元からあった）。
- **物理書き戻しのずれ**: `Box3DPhysicsBackend.cpp` の `syncCubeWorldCFramePreservingAttachments()` の「子なしCube近道」が、ワールド値をローカルとして `commitCFrame` に渡していた。座標親のローカルへ変換するよう修正。
- **ModelのPositionプロパティ**: `Spatial.cpp` のPosition/Rotation/CFrame登録を整理。`assignLocalCFrameProperty()` を追加し、Model(Tool除く)への代入はPivotTo相当（原点が指定姿勢になるよう子孫ごと剛体移動）にした。プロパティ欄・YAML・Undo/Redo・Lua(`luaSet`)・`setWorldPosition` すべて同じ経路。Tool/Model以外は従来どおり。
- **Model原点の重心追従**: `Model::syncPivotToCentroid()` / `Model::syncPivotsToCentroid()` を追加し、`Physics::update()` の末尾から毎物理更新で呼ぶ。動的(非Anchored)BaseCubeを含むModelのみ、重心差0.01stud以上で原点を重心へ更新（子のワールド姿勢は保存）。
- **Lua `Model:MoveOrigin(cframe)` 追加**（`LuauEngine.cpp` `model_move_origin_closure`、Dispatch登録）: 原点だけ動かし子のワールド姿勢は保つ。
- **ジャンプ再開が遅い問題 + 足が触れていないのにジャンプできる問題**: `Humanoid.cpp` に `isLegTouching()` を追加し、`jump()` の許可と `updateGroundHover()` のジャンプ再許可(`m_groundJumpRearmPending`解除)に使用。`Physics::isTouchActive()`（IPhysicsBackend/Box3D実装）を追加。

### 2. なぜそうしたか（判断理由）
- **摩擦無視をRoot速度の再設定ではなく姿勢補正にした理由**: 速度はステップ前設定・ステップ後復元だが、姿勢は摩擦で減った速度で積分されるため回転量が足りなかった。ステップ後に不足ぶんの回転を足す方式（近似）を採用。
- **補正をRootのみ→リグ全体に変更した理由**: Rootだけ`SetTransform`で回すと、Motor6Dのばね(Frequency 10)で他パーツが遅れて追従し、向きのずれ・ぐわんぐわん揺れる回転が出た（ユーザー報告）。YawForceの診断ブロックが元々全R6ボディを回す設計だったのに合わせた。
- **Modelプロパティ代入をPivotTo相当にした理由**: 従来の`setCFrame`は「子のワールド位置を保存」する設計で、親だけ動いて子が動かないように見えた。ギズモは別経路で子ごと動くため不一致。重心追従を入れた後は「Position=重心」なので、代入=PivotToが自然。Undoで元に戻るよう「原点基準のdelta」で移動する実装にした（pivot基準だと未同期モデルでUndoがずれる）。
- **Luaの`Model.Position`も同じ挙動にした理由**: 重心追従があると「子の位置を保つ代入」は次の物理更新で打ち消され無意味になるため。代わりに原点だけ動かす`MoveOrigin`を別メソッドで提供（ユーザー承認済み）。
- **ジャンプ判定: A)幾何近似を小さくする vs B)センサー接触に一本化 → Bを採用**: 足のTouchedを接続するゲームで、幾何近似(足裏下0.3stud)とTouchedセンサー重なりにズレがあると「触れていないのにジャンプ→Touchedを回避」が可能。Touched発火元(足センサーの`m_activeTouches`)と判定を一致させた。壁対策は足場を`normal.y>=0.5`のシェイプキャスト結果に限定。`CanTouch=false`の足場/BaseCube以外(地形)はTouchedが出ないので従来の幾何判定（ユーザー指定）。
- **センサー常時生成はしなかった**: Touchedが接続されていない足にはずるが成立しないため、既存の「観測時のみセンサー生成」のまま、未接続なら幾何判定にフォールバックする設計にした。

### 3. 経緯
- 最初: 「Force/TorqueのMaintainVelocityが摩擦で減る」→ 姿勢補正で解決。→ AutoSpawnPlayerCharacterがエディタで無視される → 修正。
- 「Model/StarterCharacter下の座標がずれる」→ 使い捨てプローブ(test_main.cppに一時コード→復元)で再現。Model配下のパーツが物理後にY+16ずれる（書き戻しのlocal/world取り違え）を特定。続いて「プロパティ代入でModelが動かない」を修正。
- その後「dream.yamlのリグだけ異常回転」→ 調査。当初は再現せず、データ不整合（左右腕脚の位置とMotor6D C0が逆、SpawnLocationなしで高さ100から落下しRagdoll）を指摘。ユーザーが直しても改善せず、症状（歩行中に向きがカメラとずれて揺れる）から、Rootのみ補正が原因と判断→リグ全体補正で解決（ユーザー確認済み）。
- ジャンプ再開が遅い → 再許可条件を緩和(HipHeight+1.0) → 「足が触れないのにジャンプできるずる」指摘 → 足接触判定へ変更 → さらに「Touchedを回避できる/壁でジャンプできる」指摘 → センサー一本化＋上向き法線。
- 最後に、Model原点の重心追従、Lua代入のPivotTo化、`MoveOrigin`追加。

### 試して失敗・やり直した方法
- Root速度を再設定するだけ/Rootのみ`SetTransform`補正 → 後者は歩行中の揺れの原因。
- 足接触の幾何判定: ①足裏から+0.2上に薄い箱を置いて下キャスト → めり込み/ちょうど接触は初期接触扱いでヒットしない。②足の上端から下向きキャスト＋tolerance 0.3に変更して解決（デフォルトリグは足が床に1stud沈む）。この間`CharacterHoverRegression`が一時6件落ち、②で解消。
- `JUMP_REARM_TOLERANCE`（HipHeight+1.0）は暫定策だったため、足接触判定導入時に削除。
- プローブ作成時の罠: sed/pythonの`\n`エスケープで文字列に改行が入りビルド失敗。`SceneLoader::loadScene`のルートは`System`ではなくフラット形式のバッグ(Instance)。

### 4. 未解決・保留
- `PhysicsMigrationRegression`は実行ごとに失敗数が揺れる（7〜12件）。今回の変更とは無関係だが原因未調査。`HumanoidRigCollisionRegression`は既存失敗で、重心追従後に診断値が変化（失敗のまま、関係未調査）。
- `Replication.cpp:948` がネットワークでCubeのローカルCFrameを送信し受信側でワールドとして適用（`setWorldCFrame`）。Model配下のパーツで同種のずれが出るはず（未修正）。
- `ViewportPanel.cpp:1370`（Fキーフォーカス）がローカル座標をカメラ位置に使用（未修正）。
- 重心は固定(Anchored)BaseCubeも含む。動く部分だけの重心にするかは未決定。
- `User::buildCharacterModel` はStarterCharacter自身の座標を無視して原点Modelにcloneする（相対配置は保たれるが、StarterCharacterを原点以外に置いた場合の意図は未確認）。
- `MoveOrigin`の名前は私が決めた。ドキュメント・エディタ補完定義（RCBN.luah等）は未更新。
- ジャンプ: 片足だけTouched接続している場合、未接続足の幾何判定では許可しない仕様にした（抜け道防止）。実機のマウス操作・可変フレームでの最終確認は未実施。足裏の許容0.3stud(`FOOT_CONTACT_TOLERANCE`)は暫定値。
- dream.yamlは左右の腕脚位置とMotor6D C0が逆（ユーザー側で修正済みと報告）。SpawnLocationなし＋ImpactRagdollThreshold 45 だと高所落下でRagdollする。

### 5. 暗黙仕様の発見（spec.mdにない）
- `Spatial::setCFrame`/`setWorldCFrame`は「子孫のワールド姿勢を保存」する。つまり親を動かしても子は動かない（spec.mdのWeld節「親が移動しても子は追従しない」と整合するが、座標系節には明記なし）。ギズモ経路(`applyEditorWorldCFrame`)だけは子ごと動く非対称があった。
- `Instance::setParent`（addChild）はSpatialのワールド姿勢を保存する（ローカルを書き換える）。クローン/ロードは`cloneForest`/`attachDeserializedChild`でローカルを復元している。
- Humanoid::move()のYawForce制御は`errorDegrees/dt`（上限8rad/s）の即時目標速度方式。摩擦で回転が鈍るバグが隠れていたため、修正後は旋回が本来の速度で速くなる。
- 足のTouchedセンサーは「リスナー接続時のみ」生成（`BaseCube::isTouchObserved()`）。センサー重なりは足裏が床に触れるまで発火せず、幾何近似より数フレーム遅い。
- `CharacterRig`のデフォルトリグは、HipHeight 3で足が床に約1stud沈んだ状態で静止する。
- Model(Tool除く)はPlay中、動的な子を含む限り原点が重心に自動更新される。Lua/プロパティ/YAMLの`Position`代入はPivotToとして子ごと動く。

---

## 2026-10-01 GUIテキスト複数行・フォント(MPLUS)・ファイルI/O文書化・Program IPC実装

### 1. 何をしたか
- **TextLabel/TextButton.Textの複数行対応**: `PropertyRegistry.hpp`に`EditorWidget::Multiline`と`PropertyDesc::multiline()`を追加。`GuiContentProps.hpp`の`text()`に付与。`PropertiesPanel.cpp`に`inputMultilineString()`（std::string直結のInputTextMultiline、長さ制限なし）を追加し、単体編集・複数選択編集の両方のString分岐で使用。描画側(`Renderer_GUI.cpp`のdrawGuiText)は改行を元から扱えるため無変更。
- **既定フォントをMPLUS1pへ**: `Renderer.cpp`で`MPLUS1p-Regular.ttf`を読み込み、`io.FontDefault`とFontAwesomeのマージ先にした（MPLUS→DotGothic16→内蔵の順でフォールバック）。`Renderer_GUI.cpp`の`resolveGuiFont`でSystemFont::DefaultをMPLUSへ。`Packager.cpp`の同梱必須フォントチェックをMPLUS1pへ。`Renderer.hpp`に`m_mplusGuiFont`追加。DotGothic16は選択肢として残した。
- **コンソールのフォント**: `ConsolePanel`に`setLogFont()`を追加し、ログ本文を補助テキストエディタと同じJetBrains Monoで描画。`Renderer.cpp`でJetBrains MonoへMPLUS1pをMergeMode(DstFont明示)でマージし日本語をフォールバックさせた。`EditorManager.cpp`のコンストラクタで`setLogFont`。
- **ファイルI/Oのドキュメント**: `doc/Util/RuntimeFileSystem.md`を新規作成。`doc/Util/README.md`・`doc/Instances/TextFile.md`から参照。
- **Program(IPC)の実装**:
  - `IPlatform.hpp`に`IPipedProcess`と`launchPipedProcess`を追加。`WindowsPlatform.cpp`(CreatePipe+PeekNamedPipeのポーリング)で実装、`MockPlatform`/`MacPlatform.mm`はnullptrスタブ。
  - `include/Instances/Program.hpp`・`src/Instances/Program.cpp`を新規作成（PhysicalFileInstance派生、`registerProgramType()`で登録、`luaCreatable=false`）。Start/Call/Send/Receive/Connect/Disconnect/Close、改行エスケープ、FIFO応答照合、`pollAll()`/`forceCloseAll()`。
  - `LuauEngine`(.cpp/.hpp)にprogram_*_closure、`PendingProgramCoroutine`、`pollProgramRequests()`(update先頭)、`resumeProgramWaiter()`を追加。`cancelAllTasks()`で待機破棄＋全Program強制終了。`LuauEngine_Dispatch.cpp`にDispatch登録。`instance_new_closure`で`luaCreatable=false`のPhysicalFileInstanceを拒否。
  - 回帰テスト`--program-ipc-regression`(`test_main.cpp`: FakePipedProcess、実プロセス往復、Luau yield/タイムアウト/Close)を追加。テスト用の子プロセスは`RecubinTest.exe --ipc-echo-child`。
  - ドキュメント: `doc/Instances/Program.md`新規、`spec.md`(Program節)、`RCBN.luah`(Program型)、`doc/Util/IPlatform.md`、`doc/Core/LuauEngine.md`、`doc/Instances/README.md`。
- **LuauコンパイルのmutableGlobals対応（TextFile.Contentバグ修正）**: `include/Util/LuauCompile.hpp`・`src/Util/LuauCompile.cpp`を新規作成。`LuauEngine::loadScriptChunk`と`Packager::compileLuauInProc`の`luau_compile`呼び出しを`LuauCompile::compile`へ置換。`LuauEngine`に`m_instanceGlobalNames`を追加し`setGlobalInstance`で記録。
- **サンプル**: `TestCases/ProgramIpcSample/`（`.rcbn`シーン、`scripts/ProgramIpcSample.luau`、`src/echo_server.cpp`、`assets/echo_server.exe`、`build.bat`、`README.md`）を作成。

### 2. なぜそうしたか（判断理由）
- **IPCの通信経路: 標準入出力パイプ vs 名前付きパイプ/TCP → 標準入出力を採用（ユーザー選択）**: 最も単純でWindows/Mac共通。代償としてDisconnect/Connectは「プロセスへの再接続」ではなく「こちらの送受信を止める/再開」の意味になった。
- **Call/Closeはコルーチンをyield（ユーザー選択）**: フレームを止めないため。既存の`PathfindingService:FindPath`（PendingPathCoroutine, owner=Script/EngineTask/Signal）の型に倣った。
- **読み取りはスレッドではなくPeekNamedPipeポーリング**: `pollStdinLine`の「reader threadを作らない」方針に合わせた。切断中も`pollAll()`で吸い上げ、子のパイプ詰まりを防ぐ。
- **Program専用のPendingリストを作り、FindPathのコードを共通化しなかった**: CLAUDE.mdのスコープ厳守（リファクタ禁止）のため。重複はあるが既存処理を触らない方を選んだ。待機中フラグは`Script::WaitingForPath`/`EngineTask::waitingForPath`を流用。
- **`resumeEngineTask`を使わず再開処理を別に書いた**: あれは再開引数をdeltaで上書きするため、Callの応答/終了コードを渡せない。
- **IPC.*グローバルのスタブは据え置き**: 「最初に実装」の範囲外。廃止は別判断。
- **MPLUS: 「既定を置換／選択肢追加／DotGothic完全置換」で迷い、既定置換+DotGothic残しを採用（ユーザー選択）**: 既存シーンの`Font: DotGothic16`を壊さないため。
- **TextFile.Contentバグの修正: mutableGlobals(案1)／optimizationLevel=0(案2)／テストのみ修正(案3) → 案1**: 性能劣化なく、該当グローバルだけimport最適化を止められるため。Packagerにも同じ関数を適用（バイトコード事前コンパイルで直し漏れるのを防ぐ）。
- **`--system-extension-regression`を先にgit stashでHEADに戻して確認した**: 自分の変更が原因か切り分けるため。HEADでも失敗しており既存バグと確定。

### 3. 経緯
1. 「TextLabel.Textを複数行に」→ 描画は対応済みでエディター入力欄(256バイト単行)だけが原因。フラグ追加方式で解決。
2. 「MPLUSを採用したいが配線がわからない」→ フォント決定箇所(Renderer.cpp/Renderer_GUI.cpp)を案内し、置換。続けてコンソール用フォント指定なし→JetBrains Mono+MPLUSフォールバック。
3. 「ファイルI/Oをまとめて」→説明後、docに保存。
4. 「IPCの実装」→プランモードで質問(通信経路/区切り/待ち方/Close)→承認→実装。
5. 全体回帰で`--system-extension-regression`が失敗。HEADでも再現→「TextFile.Contentの書き込みバグ調査」へ。
6. 調査: setter/getterは正常、書込も成功しているのに2回目の`SaveData.Content`が古い値。`SaveData['Con'..'tent']`（動的キー）では新値、`--!optimize 0`でも正常 → Luauコンパイラのimport最適化（`global.field`をロード時に定数化）が原因と特定。`luaL_sandboxthread`でsafeenvが有効になるのも条件。
7. mutableGlobalsで修正→`--system-extension-regression`が通過。
8. 最後にIPCサンプルディレクトリを作成し、RecubinTestのシーン実行で動作確認。

### 試して失敗・やり直した方法
- **原因の推測を静的読解だけで進めた**: setter/getter/キャッシュ(`s_cache`)を読んでも原因が出ず、一時デバッグ出力(`std::cerr`)とLuaプローブで絞り込んだ。stdoutの`print`とstderrの順序が入れ替わって見え、最初は「getterが呼ばれない」ことに気づきにくかった。
- **IPCテストのバグ**: `send`/`receive`で`error`文字列を使い回し、前のチェックの残りで`error.empty()`が失敗した（テスト側を`error.clear()`で修正）。
- **`Instance.new('Program')`が拒否されなかった**: `luaCreatable=false`は`instance_new_closure`が見ていなかった（TextFileだけ名前直書きで拒否していた）。`PhysicalFileInstanceRegistry::find`で汎用化。
- **サンプルのexeビルド**: ①UTF-8の日本語コメント入りソースをcl.exeが既定CP932で読んで構文崩壊 → `/utf-8`。②日本語入りの`build.bat`をcmd.exeが読めず壊れた → batはASCIIのみに。
- **ConsolePanel**: 最初のビルド時点ではMPLUSマージ先を指定しないとFontAwesome同様に直前のフォントへマージされる（DstFont明示が必要）。

### 4. 未解決・保留
- **`IPC.Connect/Send/Receive/Close`のグローバルスタブ**が残存（`LuauEngine.cpp`、`test_main.cpp`のスタブテスト含む）。廃止するか未決。
- **Packagerで配布したときProgramのexeが同梱されるか未検証**。バイトコード側のmutableGlobalsはPackagerでは`setGlobalInstance`の動的名(ワークスペース名と同名のグローバル)が分からず固定リストのみ。既存`.luauc`は再パッケージまで旧挙動。
- **Programの書き込みはブロッキング**（子がstdinを読まず64KB超で詰まる）。overlapped化は未対応。Windowsのみ対応、macOSは`launchPipedProcess`がnullptr。
- **Signalコールバック内のCall/Closeはyield不可**でエラー（FindPathと同様の制約）。
- **`resumeEngineTask`はyield再開時にdeltaで引数を上書き**するため、タスク内でFindPathを使うと経路ではなくdeltaが返る疑い（既存コード、未確認）。
- **エディター(Recubin.exe)でのProgramサンプルのPlay、コンソール/TextLabelの見た目(MPLUS・JetBrains Monoの実表示)は未確認**（ユーザーのルールでGUI自動起動検証は禁止。ビルド確認とRecubinTestまで）。
- **MPLUSのサイズ(22px)**はDotGothic16に合わせた暫定値。JetBrains Mono側のMPLUSマージも17px固定。
- **全体回帰**: 最終は198 passed/12 failed（修正前194/16）。残りは既存の失敗(`--asset-path`/`--motor6d-gyro`等)。ベースライン(130/4)とは集計単位が異なるので次回も同条件で比較すること。
- **`TextFile`のseed/overlayテストは`--system-extension-regression`のみ**。Programの`--program-ipc-regression`はRecubinTest.exe自身を子プロセスにするためWindows限定(`#ifdef _WIN32`)。

### 5. 暗黙仕様の発見（spec.mdにない）
- **Luauのimport最適化**: `workspace`/`System`/`User`等のグローバルの`global.field`(最大3階層)は、既定コンパイルではスクリプトロード時に1回だけ解決・定数化され、以後`__index`が呼ばれない。書き込みも反映されて見えない。ローカルに取ってから読む(`local f = workspace.X; f.Y`)場合は毎回`__index`が呼ばれる。今回`LuauCompile`で対策したが、固定リスト＋`setGlobalInstance`名が対象。新しいInstanceグローバルを増やすときは`LuauCompile::instanceGlobals()`(または`setGlobalInstance`)に載せること。
- **スクリプトの環境は`luaL_sandboxthread`でsafeenv=trueの別グローバル表**（`LuauEngine::execute`）。
- **`Instance.new`はPhysicalFileInstanceRegistryの`luaCreatable=false`で拒否**されるようにした（TextFile以外のProgramも対象）。TextFileは従来どおり名前直書きでも拒否される。
- **Program通信仕様**: 1行=1メッセージ、`\`→`\\`/改行→`\n`/CR→`\r`、応答はFIFO(Sendの応答と未対応の行は受信キュー)、1メッセージ1MiB・受信キュー4096件上限、Play停止/シーン切替(`cancelAllTasks`)で全Program強制終了。Disconnect中もパイプは吸い上げ続け、Callの応答のみ破棄しSendの応答は保持する。
- **`RuntimeFileSystem`のrootはCWD**（コンストラクタ引数なし時）。エディターと配布ランタイムでTextFileのoverlay(`textfiles/<StorageId>.txt`)が分離される。
- **RecubinTest.exeは`<scene>`引数でシーンをヘッドレス実行できる**（Scriptがそのまま動く）。`[Luau]`print出力で挙動確認に使える。
- **MSVCの既定文字コードはCP932**。日本語UTF-8ソースには`/utf-8`が必要。`.bat`はASCIIのみ。


## 2026-10-02 大量Cube(50k→100k)のCPU負荷削減・プロファイラー強化

### 1. 何をしたか
**プロファイラー**
- `ProfilerPanel.cpp`: 表を定義ベース(`TableDef`/`Entry`/`Label`)に組み替え、UI表示とMarkdown出力で同じ定義を共有。「Copy as Markdown」ボタン(`buildMarkdownReport`)を追加。行描画関数4本を`drawEntryRow`に統合。
- `Renderer.cpp`: ドローコール系カウンター追加(`drawCallsMain/Shadow/Instanced/Terrain`、`instanceUploadBytes`)。
- 計測スコープ追加: `physics.reconcileConstraints/staleScan/syncPivots`(`Physics.cpp`/`Box3DPhysicsBackend.cpp`)、`ui.viewportScene/Click/Gizmo/HoverPick/HoverOutline/FreeDrag`(`ViewportPanel.cpp`)、`ui.newFrame/renderPanels/toolbar/imguiRenderDrawData`と各パネル(`EditorManager.cpp`)、`main.processInput/humanoids/terrains/weather/particles`(`main.cpp`)。

**全ツリー走査の撤廃（Workspaceの登録リスト化）**
- `Workspace::registerRenderSubtree/unregisterRenderSubtree`が種別ごとのリストを保持: Constraints/Models/Humanoids/Skyboxes/Forces/Attachments(既存のParticleEmitters/Weathers/Terrains/BaseCubesに追加)。`getTreeRevision()`を追加。
- 置き換えた毎フレーム走査: `Physics::reconcileConstraints`、`Model::syncPivotsToCentroid(リスト版)`、`Box3DPhysicsBackend::hasEnabledForce`(`m_updateWorkspace`経由)、`Humanoid::updateAll(リスト版)`、`ParticleEmitter::updateAll(リスト版)`、`Weather::updateAll(リスト版・直下のみ)`、`SceneRuntime::updateTerrains`、`Renderer::renderConstraints/renderPhysicsDebug`、レンダラーのSkybox探索。
- `Box3DPhysicsBackend::update`の`staleScan`はツリーリビジョン変化時のみ実行。

**描画CPU**
- `Renderer.cpp`: 収集ループ内の`addCount`をループ後にまとめて加算、クラス名比較を1回に、`shouldCastShadow`の`IsA("MeshCube")`を除去、`visibleShadowInstances`をカスケード外で再利用。
- **`instanceCollect`の並列化**: 新規`ThreadPool`(`include/Util/ThreadPool.hpp`、`src/Util/ThreadPool.cpp`、最大7ワーカー)。Cube範囲を最大32チャンクに分け、結果は元の順序で結合(並列コピー)。

**Spatial/座標**
- `Spatial::getWorldCFrame()/getCoordinateParent()`の高速経路(`m_hasSpatialAncestor`)。`Instance::refreshHierarchyCache()`(新規仮想関数)を`setParent`が部分木全体へ呼んで更新。
- `Spatial::boundsEpoch()/notifyBoundsChanged()`を追加。`setCFrame/commitCFrame/setSize`で値が実際に変わると増える。`BaseCube::setSize`と`ViewportPanel.cpp`のギズモSize直書きにも通知を追加。

**選択レイキャスト**
- 新規`BaseCubeBvh`(`include/Core/BaseCubeBvh.hpp`、`src/Core/BaseCubeBvh.cpp`)。`Workspace::getPickBvh()`で保持、`ViewportSceneQueries::findNearestBaseCube`が使用。ツリー/エポックが2回連続で不変のときだけ構築、動いている間は線形走査にフォールバック。
- `ViewportPanel::drawHoverHighlight`: ホバー結果のキャッシュ(`m_hoverPickCache`、レイ+ツリーリビジョン一致かつ非Playのとき)。

**その他**
- `EditorManager.cpp`: ツールバーが毎フレーム呼んでいた`computeSpawnPos`(全Cubeレイキャスト)を、クリック時のみ評価する`LazySpawnPos`に変更。
- `test_main.cpp`: `ViewportHelperRegression`にBVH等価性テストを追加(700個×ランダムレイ、移動/リサイズ/削除後も線形基準と一致)。

### 2. なぜそうしたか
- **「レンダラー並列化」を最初にやらなかった理由**: 計測でドローコールは2+3、GPUは8%で、ボトルネックは個数比例のCPU処理(全走査)と判明。無駄を消すほうが安全で効果が大きく、並列化は`instanceCollect`が最後に残ってから導入した。
- **登録リスト方式 vs 変更検知フラグ(Step 2の静的Cubeキャッシュ)で迷って前者**: `Color`/`Size`等が公開フィールドでフックが無く、キャッシュは設計コストが高い。登録リストは既存の`registerRenderSubtree`に乗れて副作用が小さい。60FPSが出たためキャッシュは見送り。
- **`onAncestorChanged`ではなく新規`refreshHierarchyCache`にした理由**: `BaseCube`/`Tool`等が`Instance::onAncestorChanged`を直接呼ぶためSpatialの上書きを素通りする。`setParent`から部分木を直接走査すれば漏れない。古いtrueは遅い経路で正しく解決され、危険なのは古いfalseだけ。
- **ホバーピックをキャッシュのみ vs BVH**: キャッシュは静止時しか効かない(マウスを動かすと毎フレーム再計算)ため、BVHも追加。Playでは物理が毎フレームエポックを進めるのでBVHを使わず線形走査にフォールバック(構築を繰り返すほうが遅い)。
- **BVHの境界エポックで「同値書き込みでは増やさない」理由**: Skyboxが毎フレームカメラ位置へ書き込むため、増やすと永遠にBVHが無効になる。
- **ThreadPoolでタスク取得を世代つきロックにした理由**: 完了後に次のジョブが始まったあと、古いtaskポインタで新しいindexを実行する競合を防ぐため。

### 3. 経緯
- 最初の指示: 「50k Cubeで平均10FPS、GPU8%。CPU側が重い。プロファイラーをMarkdownでコピーできるボタンと、ドローコール項目を追加して計測したい」→ 実装。
- 計測→調査(Exploreエージェント)→プラン承認→Step0(計測追加)/Step1(描画の無駄削減)→再計測を繰り返した。順に: 物理の全ツリー走査(27→8ms)→Humanoid/Particle/Weather/Terrainの`updateAll`(main.terrainsなど)→`getWorldCFrame`高速化→ツールバーの`computeSpawnPos`(7.7ms)→追加描画の全走査→`instanceCollect`の並列化→選択BVH。
- 結果: 50kで平均10FPS→(VSyncオフ)80FPS。100kでもVSyncありで60FPS維持。ユーザー確認済み。
- ユーザーが「ギズモのハイライトでFPSが落ちる」と指摘 → 原因はギズモではなく`drawHoverHighlight`のレイキャスト(ホバー中のみ毎フレーム全Cube走査)。計測スコープを足して確認。

### 試して失敗・やり直した方法
- **`python run_regression.py`をそのまま実行した**: 専用テスト後に必ずRecubin.exeのGUI自動操作スモークが起動する(スキップ引数なし)。ユーザーの「GUI自動テスト禁止」ルールに反した。以降はスクラッチパッドで`run_regression.list_regressions`/`run_dedicated`だけを回す方法に切り替え、メモリ(`feedback_no_gui_smoketest.md`)に追記。
- **ヒアドキュメント経由のPython/補助スクリプトが通らない箇所があった**: Writeで直接ファイルを書き換えて回避(`ProfilerPanel.cpp`)。
- **スレッドプール単体テストのコンパイル**: 日本語コメント入りUTF-8をMSVCがCP932で読んで構文崩壊 → `/utf-8`を付ける。
- **BVHテスト**: `Quaternion::fromAxisAngle`に正規化していない軸を渡して`[ASSERT]`で異常終了 → `.normalize()`で修正。
- **ベンチ用のレイが全Cubeを外れていた**: 最初の測定が無意味(0.0000ms)だった。当たるレイに直して測り直した。

### 4. 未解決・保留
- **実機確認が必要**(GUI自動起動禁止のため未確認): 並列化した描画でCubeの欠け・重複・順序が起きていないか、ホバーのBVH経路、Skybox追従、ギズモのSizeドラッグ後の選択。
- **残るホットパス(100k)**: `physics.syncCubes` 4.8ms(Anchoredの変更検知が必要。`Spatial::boundsEpoch`方式で取れる見込み)、`render.instanceCollect` 3.7ms、`mainInstanceUpload` 0.4〜1.2ms。60FPSが出ているため見送り。
- `GuiAutomation`スモークが`Editor/CrashRecovery/*`でタイムアウトした(今回の変更が原因か既存かは未確認)。
- 回帰テストは「GUIなし専用テスト」で54 passed/11 failed、`[ERROR]`14件(ベースラインと同数)。**失敗名のHEADとの突合は未実施**(以前のログとの比較のみ)。シーン実行(`run_scene`)は回していない。
- `Weather::updateAll(リスト版)`は「Workspace直下のみ」を維持するため`Parent`で絞っている。ネストしたWeatherの扱いは従来から未定義。
- `Humanoid::updateAll`のグローバルな呼び出しID(`g_currentHumanoidUpdateAllInvocation`)はリスト版でも1呼び出し1IDで維持。

### 5. 暗黙仕様の発見（spec.mdにない）
- **`Spatial::Size`は公開フィールドで、書き換えフックが無い**。BVHなどの境界キャッシュは`setSize`または`notifyBoundsChanged()`に依存する。直接書いたら必ず`notifyBoundsChanged()`を呼ぶこと(忘れるとBVHが古くなり拡大したCubeを取りこぼす)。
- **`Parent`を書くのは`Instance::setParent`だけ**(例外: `Workspace`のデストラクタ)。登録リストやキャッシュの整合はここに乗る。
- **`Spatial::setCFrame`は子孫のワールド姿勢を保存する**設計(子孫のローカルを再コミットする)ため、親が動いても子孫のワールド位置は動かない。選択・描画のキャッシュはこの前提に依存しない作りにした。
- **`ui`セクションは`render`に包含される**(エディターでは`renderViewport`が`renderUI`内の`ViewportPanel`から呼ばれる)。`swap`は`render`の外。フレーム時間 ≒ ui + physics + swap + 計測外。
- **エディターではPlay中のみ物理・Luauが動く**(`SystemState::isPlaying`)。VSyncオフ計測では`swap`がほぼ0になりCPU律速が見える。
- **`Quaternion::fromAxisAngle`は正規化済みの軸を要求する**(デバッグアサート)。
- **`computeSpawnPos`は全Cubeへのレイキャスト**。毎フレームのUIから呼ぶと個数に比例して重くなる。
- **MSVCの既定文字コードはCP932**。日本語UTF-8ソースを単体で`cl`コンパイルするには`/utf-8`が必要(CMakeビルドは通る)。

## 2026-10-08 Spring/PrismaticConstraint追加・乗り物のGyro無力化修正・Box3D独自パッチ整理

### 1. 何をしたか
- **Spring / PrismaticConstraint追加**(コミット`b8d76e2`)。新規: `Spring`、`PrismaticConstraint`(hpp/cpp)、`Util/SpringHelix`(コイル頂点生成)、`doc/Instances/Spring.md`・`PrismaticConstraint.md`。配線: `IPhysicsBackend`/`Physics`/`Box3DPhysicsBackend`(`createSpring`/`createPrismatic`、IsAチェーン全箇所、`updateConstraint`)、`SceneLoader`、`LuauEngine`/`LuauEngine_Dispatch`/`RCBN.luah`、`SceneHierarchyPanel`/`EditorManager`/`InstanceCatalog`/`IconsDef`、`Renderer`(Springはコイル描画、Prismaticは`renderPhysicsDebug`のみ)、`Workspace.hpp`のfriend、`spec.md`/docs。
- **Gyro無力化の修正**(`Box3DPhysicsBackend.cpp`の`applyGyroForces`)。無名namespaceに`collectRotationCoupledBodies`と`rotationCoupledInertia`を追加。GyroのPartとPrismaticで連結した動的ボディ全体の慣性(質量中心まわり＋平行軸)でトルクを計算する。連結が無い場合は従来の式のまま。
- **テスト**: `test_main.cpp`の`runSpringPrismaticRegression`へ、Prismatic接続Cubeでも目標角へ回る4変種(Part側/Base側/重力あり/制限あり)と、baller相当の車両(Sphere+BallSocket、Seat+Prismatic+Spring、乗員Weld)が直立を保つ2件を追加。
- **Box3D独自パッチの破棄**: `temp_libs/box3d`の未コミット差分(`b3SphericalJoint_SetAngularLimits`、7ファイル約326行)を`git checkout`で上流`8441b4a`へ戻した。`spec.md`のBallSocket節、`doc/Instances/PhysicsConstraint.md`、`doc/Instances/Humanoid.md`を「円錐(X/Y)＋ツイスト(Z)の近似」へ修正。

### 2. なぜそうしたか
- **Springの挙動**: 双方向ばね(Distance joint+spring)を採用。Ropeは引張のみなので差別化できる。`FreeLength=0`は「生成時の距離」、update時は現在の自然長を保持する(Ropeの`MaxDistance=0`がupdateで`B3_LINEAR_SLOP`に潰れる既知挙動を踏襲しないため)。
- **Prismaticの軸**: Box3Dの`b3PrismaticJoint`はソースのコメントがZと書いてあるがコードはjoint frame**X軸**方向。`rotationFromX`を新設した。B側アンカーは軸直交のずれを除いてA軸線上へ置き、生成時のスナップを避けた。Lower/UpperはCube0アンカー→Cube1アンカーの軸方向距離(生成時位置を0とする相対値ではない)。
- **ヘリックス生成を`Util/SpringHelix`に分離**: 単体テストから頂点数・端点一致を検証するため(GUI確認禁止の方針)。
- **Gyroの慣性を連結ボディで計算**: AとBで迷った。(A)連結ボディ全体の慣性を使う / (B)MaxTorqueやゲインを上げる。Bは実測で「wantedが上限250に全く届いていない(慣性が小さすぎ)」と分かったので不採用。Aのうち、連結対象は**Prismaticのみ**とした(BallSocketは回転を伝えないのでSphereを含めない、Spring=distance jointも回転を伝えない)。回転中心は実際のピボットが不明なため質量中心とした。過大推定するとトルク過剰で離散時間制御が不安定になるが、質量中心まわりは最小値なので安全側(やや鈍い)になる。
- **Box3Dパッチは破棄(選択肢2)**: 呼び出しが`bde3bf9`(9/25)で外れており、サブモジュール内の未追跡パッチはmac等で"Unknown API"になる。今後の上流更新にも弱いため、ユーザー判断で仕様側を現実装(cone+twist)に合わせた。

### 3. どういう経緯か
1. 指示: SpringとPrismaticConstraintを他の拘束と同様にAttachmentなしで動作させる。Springはコイル描画(bool)、Prismaticは物理デバッグON時のみ描画。→Plan modeでExploreし、質問(挙動/見た目/軸)に回答を得てimplementerへ委譲、ビルド・専用テスト合格。
2. 「PrismaticがONだとGyroが効かない」→当初はコードを読んでも干渉が見つからず、動的Cube同士・Gyro側Cube0/1・重力・制限ありの4変種で全て回転することを確認。Anchored相手の場合のみ回らないが物理的に正しい。→ユーザーが`assets/bin/baller.rcaet`を提示し「プレイヤーが乗ると無力化」と再特定。
3. baller構成(薄い板Body+Gyro、Sphere、BallSocket、Seat+Prismatic+Spring)を再現。乗員なしでも少しずつ傾き(0.1°→4.8°/6秒)、乗員Weldで約1秒で転倒(約100°)。デバッグ出力でBody単体の慣性I≒0.057が使われていると判明→上記修正。修正後は最大傾き0.24°。専用テスト71件合格。
4. 「Box3D側の未保存は誰が書いたか」→9/14の`cf04ea9`(BallSocket軸別制限)で追加、9/25の`bde3bf9`で呼び出しのみ削除と判明。ユーザー判断で破棄、docs修正。

### 失敗・ハマりどころ
- `test_main.cpp`へPythonヒアドキュメントで`'\n'`を書くと実改行になりビルドエラー(2回)。ファイルはCRLFなので`\r\n`も混じる。今後は`Edit`ツールで直接編集する。
- `RecubinTest.exe`をWindows側Pythonから相対パスで起動するとFileNotFound。絶対パスにする。
- 最初のGyro再現テストはBody=Anchoredの変種を入れて失敗した(物理的に正しい挙動)。期待値側のテストから外した。

### 4. 未解決・保留
- **見た目の実機確認が未**: Springのコイル(Radius/Coils/Thickness、両端リード長)、Prismaticの物理デバッグ表示、`ICON_SPRING`/`ICON_PRISMATIC`(fa-arrows-v/h代替、`fa-solid-900.ttf`にグリフがあるか未確認)。
- **実キャラクターが乗る経路は未テスト**: 着席(`Humanoid::sitOn`のSeatWeld、RootGyro無効化)は剛体Weldで代用。baller.rcaetを実機で乗って確認してほしい。
- **Gyro連結慣性の限界**: 回転中心を質量中心で近似している。BallSocketでぶら下げる車両(今回)では質量中心≠実ピボットのため、なお実効応答は鈍め。Prismatic以外(Motor6D/Weld相当)で連結した場合は対象外。応答が足りなければ`RESPONSE_RATE`やピボット推定を検討。
- **BallSocketの`Locked`の意味**: Zを`Locked`にしても0°固定ではなくMin/Maxの範囲が使われる(cone+twist近似)。軸別独立制限・非対称制限は不可。直したい場合は`createBallSocket`でZ Lockedを(0,0)にする案がある。
- **`--audio-diagnostics-regression`は距離減衰5項目で失敗**(今回の変更と無関係、作業前から失敗か未確認)。
- Migration scene(Spring/Prismatic)は未追加。`doc/Instances/Rope.md`/`Rod.md`はPhysX記述のまま古い(今回は範囲外で触っていない)。
- 破棄したBox3Dパッチのバックアップはセッションのscratchpad(`box3d-angular-limits.patch`)のみで、セッション終了で失われる可能性がある。

### 5. 暗黙仕様の発見
- **新しい拘束クラス追加時はBox3DPhysicsBackendのIsAチェーンが約9か所必要**(衝突収集/`clearConstraintHandle`/`rebuildAssembly`/Weld分割後再生成/`updateConstraint`/pending dispatch/invalid check)と`Physics::reconcileConstraints`のスナップショットチェーン。漏れると「Weldで組み直された時に拘束が消える」サイレントバグになる。
- **Gyroは対象Partの剛体の慣性だけでトルクを決めていた**(連結ボディを考慮しない)。MaxTorqueが十分でも`wanted`が上限に届かないため「効かない」ように見える。
- **GyroのAnchored相手のPrismatic**: Prismaticは相対回転を完全固定するので、Anchored/静的に繋がったPartはGyroでは回らない(仕様どおり)。
- **Box3Dの`b3PrismaticJoint`の動作軸はjoint frameのX**(コメントはZ)。
- **サブモジュール`temp_libs/box3d`は上流そのまま維持する方針**。独自APIを足すとmac/他環境のビルドで未定義になる。
- **BallSocketの制限は公式API(cone+twist)の写像**であり、`spec.md`に明記した(以前は軸別と書かれて実装と乖離していた)。

## 2026-10-11 脆弱性調査・クラッシュ証明・修正（NaN/不正入力/ファイル破損）

### 1. 何をしたか
- **調査→証明→修正の3段階**で実施。証明は `test_main.cpp` の `namespace SecurityProbe`（`--security-probe <name> <workDir>`、回帰レジストリ非登録・手動用に残置）。修正後の担保は `--security-regression`（`runSecurityRegression`、登録済み、作業dir `build/security_regression`）。
- 修正（implementerエージェントに委譲→メインでレビュー・追加修正）:
  - **M1 GLB**: `MeshCube::loadFromGLB` に `cgltf_validate` 追加、primitive単位でインデックス≥頂点数なら巻き戻して破棄。
  - **F1 地形再生成**: `TerrainStreamer::workerLoop` の Regenerate で `remove_all(DataPath)` を廃止し、無名ns `removeRegionFiles` で `r_<int>_<int>.yaml` のみ削除（非再帰）。
  - **F2 原子的保存**: 新規 `Util/AtomicFile`（`AtomicFile::writeReplacing`: 一意名tmp→rename1回、失敗時は元ファイル保持）。`SceneLoader::saveSceneResult`、`TerrainStreamer::flushRegionsToDisk`、`RuntimeFileSystem::write` で使用。
  - **C1 .rcaetインポート**: `AssetImporter::instantiate` で `extractAll` 前に `validateParse`（ドライパース）、本番も `parseIntoGuarded` で例外捕捉。
  - **F5 RuntimeFileSystem**: `write` はディレクトリ/ルートを拒否、`move` は事前removeを廃止しrename1回、`overwrite=false` で既存先なら失敗、ルートのmove禁止。
  - **F3 Packager**: `..` で外に出る相対パスは `sub/filename` へ再配置、`reserveDestination`/`reserveScriptDirectory` でコピー先衝突を連番(`_N`, `dup_N/`)回避。
  - **N1 NaN（3層）**: `Replication::onGameMessage` の AvatarBatch は Client時のみ処理＋非有限エントリ破棄、`clientStoreWorldTransforms` も破棄。`Spatial::setWorldCFrame/setCFrame/commitCFrame` と `assignLocalCFrameProperty`(Model/Tool分岐)、`Model::pivotTo` で非有限位置を拒否。`Box3DPhysicsBackend` に `rejectNonFinite`（SetTransform/SetTargetTransform/速度/力/トルク直前、ログはcontext毎3回まで）。
  - **実機検証用**: 起動フラグ `--debug-inject-nan-avatar-batch`（`game_main.cpp` の `NetworkLaunchArgs`、`ReplicationManager::setDebugInjectNaNAvatarBatch`/`sendDebugNaNAvatarBatch`。Hostのみ、通常バッチ20回ごとにNaN版を追加送信）。`TestCases/NetworkTest/` に `assets/scenes/AvatarBatchNaN.yaml`、`run_avatar_batch_nan.ps1`、`AvatarBatchNaN_README.txt`。

### 2. なぜそうしたか
- **F1は案A（region名パターンのみ削除）**: 案B（マーカーファイル必須）は既存データに互換問題が出るため、ユーザー判断で案A。
- **C1は展開前ドライパース**: 失敗時に展開済みファイルが残るのを避けるため（ユーザー判断）。代償として埋め込み前の元パスでGLB等を一度読みに行き、警告が出得る・インポートが重くなる。
- **F2/AtomicFileは「remove(target)→再rename」フォールバック禁止**: AssetContainer::writeにはこのフォールバックがあるが、2回目失敗で元ファイルを失うため採用しなかった（AssetContainerは範囲外で未変更）。
- **NaNの関所をSpatialに置いた**: 侵入口（ネットワーク/Luau `Vector3.new(0/0,..)`/YAML `.nan`/内部計算）がすべて位置setterを通るため。Box3D直前の関所は内部計算由来の速度・力用の多重防御。
- **Packagerのスクリプト衝突は`dup_N/`サブディレクトリ**: `compileLuauInProc` のシグネチャを変えない規約のため（出力名を渡せない）。
- **実機検証は起動フラグ方式**: 修正後はHostもNaNを生成できず、Luauに生送信APIもない。Luauデバッグ送信APIは悪用面が増えるため不採用（ユーザー選択）。

### 3. どういう経緯か
1. 「脆弱性を調査してクラッシュ証明→修正点を決める」→ Network(ByteReader/NAT)は堅牢と確認。候補: F1/M1/C1/F3/F2/F5/N1/P1/L1。
2. 案Aでプローブ14件を実装・実行: F1/M1×3/C1/F5×4/F3×2 再現、N1は物理ステップでハング（60秒タイムアウト）、P1（子プロセス連続出力でpollが戻らない）は再現せず→除外。NaN原点のレイキャストは安全。
3. ユーザー回答（F1案A、C1展開前検証、N1多層、F2含める、回帰登録）→implementerに委譲。レビューで `removeRegionFiles` の `filename().string()`（ANSIで表せない名前で例外→ワーカー死亡）を `AssetPath::toStored` に修正。
4. 追加指示でModel経路（`assignLocalCFrameProperty`、`Model::pivotTo`）を塞ぎ、実機検証用フラグとシーンを作成。
5. ps1がPowerShell 5.1で構文エラー→BOMなしUTF-8の日本語コメントが化けて`param`が壊れていた。ASCII化して解決。

### 失敗・ハマりどころ
- **プローブで `poll()` を子起動直後に呼ぶと0msで戻る**（子がまだ出力していない）。500ms待っても再現せず、P1は実害なしと判断。
- **NaNハングの切り分け**: 最初はレイキャストを疑ったが、別プローブでNaNレイキャストは安全と判明。ハングは「Anchored体にNaN姿勢→数フレーム後のBox3Dステップ」。
- **`.ps1` に日本語を書くとWindows PowerShell 5.1で壊れる**（BOMなしUTF-8）。ps1はASCIIのみ。
- **`RecubinEngine.exe --help` を誤って実行**（GUI起動禁止方針に違反）→ `TestCases/NetworkTest/system_extensions.receipt` が生成されたので削除。起動系コマンドを確認目的で叩かない。
- `Spatial.cpp`/`Box3DPhysicsBackend.cpp`/`Model.cpp` はCRLF。編集はPythonでCRLFを保って行った（Editツールだと混在の恐れ）。`test_main.cpp` はLF。
- `--physics=physx` は「no longer supported」で即終了するため、PhysXでの回帰は実行不能。

### 4. 未解決・保留
- **AvatarBatch NaN の実機検証はユーザー実施待ち**（`run_avatar_batch_nan.ps1`、修正後のスクリプトは未起動確認）。
- AvatarBatchのNaN破棄経路は回帰テストで直接検証できていない（Clientロールに実ネットワークが必要）。
- 未証明・未対策の候補: L1（Luauから `workspace:Destroy()` / `Instance.new("Workspace"/"Terrain")`、同DataPathのTerrain二重ストリーマ）。
- `AssetContainer::write` には「remove→再rename」フォールバックが残っており、F2と同種のリスクあり（範囲外で未修正）。
- C1ドライパースの副作用（元パスでのアセット読み込み・警告ログ）。
- `--audio-diagnostics-regression` の距離減衰5項目失敗は継続（今回と無関係、10/08から）。
- 変更は全て未コミット。

### 5. 暗黙仕様の発見
- **位置setter（Spatial）は回転の正規化しか検査していなかった**。NaN位置は全経路から物理まで素通りし、Box3D（Release、assert無効）でハングする。
- **MSVCの `std::filesystem::rename` は既存ファイルを置換する**。そのため旧 `RuntimeFileSystem::move(overwrite=false)` は上書き防止になっていなかった。
- **cgltfは `cgltf_validate` を呼ばない限りaccessor/バッファ範囲・インデックス最大値を検証しない**。
- **Terrain.DataPath は任意フォルダを指せる**（「Use Existing...」）。DataPath配下はregionファイル以外も存在しうる前提で扱う必要がある。
- **Host側のリモートアバターは物理プロキシ**のため、Client→HostのAvatarBatch偽装は姿勢に効かない。実害経路は「悪意あるHost→全Client」。
- ネットワークゲームではスクリプトが起動しない（game_main）ため、ネットワーク検証シーンでLuauによる状態表示はできない。

---

## 2026-10-11 キャラクター/ネットワーク物理の改修（Seat降車姿勢・Host権威+固定tick決定論・補間表示）

### 1. 何をしたか
- **Seat降車で傾いたまま**（`Humanoid::standUp`）: 降車ホップをYawのみ残した直立姿勢(`Quaternion::fromEuler(0,yaw,0)`)にし、`collectCharacterBodies`の全ボディを同じ剛体変換で`moveWeldAssembly`（ネイティブボディ単位で1回）。
- **第1段階（明確なバグ修正）**（`Replication.cpp/.hpp`）:
  - Hostのリモートキャラで`updatePhysicsState`を実行（ジャンプ1回きり・ホバー無しで高さ3.5〜3.9studずれる問題）。
  - リモートキャラをテンプレートの`CanCollide`に戻し、NoCollisionをRoot同士→衝突ボディ同士へ拡大（`enablePhysicsProxy`、`RemoteAvatar::templateCanCollide`）。
  - レガシー物理用の予測シーン（shadow root/static mirror/入力リプレイ）を削除し、履歴比較の補正に置換。補正は`translateLocalCharacter`で全R6ボディを並進＋速度保持（`setMemberWorldCFrame`が速度を0にするため保存・復元）。回転は補正しない。
  - `hostSendWorldTransforms`がローカルCFrameを送っていたのを`getWorldCFrame()`に（10/01の保留事項）。
- **Client側で車体が置き去り**: `Physics::setMemberKinematicTarget` / `IPhysicsBackend::setBodyKinematicTarget`（既定は瞬間移動、Box3Dは`b3Body_SetTargetTransform`）を追加し、`clientApplyWorldSmoothing`からネイティブボディごとに1回呼ぶ。
- **2a キャラ制御の固定tick化（全モード）**: `IPhysicsBackend::setPreStepHook`（Box3Dの`stepOnce`ループで`applyBuoyancy`前に呼ぶ）、`Physics::update`中だけ`Humanoid::stepControlAll`を登録。`Humanoid`に`HumanoidControlInput`、`setTickControlled`/`setControlInput`/`clearControlInput`/`requestJump`/`requestStandUp`/`stepControl`を追加。`User::processInput`/`processCharacterMovement`/`applyQueuedJump`は入力を渡すだけに（`move()`と毎フレームの`updatePhysicsState`呼び出しを削除）。
- **Seat**: 着席高さを`Humanoid::seatedRootHeight`（RootJoint/HipのMotor6D C0/C1から股関節高さ→曲げた脚の下面が座面に接する高さ）に。着席をHost権威化: `Seat::setTouchSitEnabled`（Client中は無効、`ReplicationManager::update`で毎フレーム設定、デストラクタで有効に戻す）、新メッセージ`SeatState=11`（Reliable、`hostSendSeatStates`/`clientApplySeatState`/`applyLocalSeatPath`）、`NetworkManager`のゲームメッセージ範囲を`SeatState`まで拡張、`Humanoid::getSeat()`。
- **2b 入力のtick同期**: `Humanoid::setControlStepCallback`（stepControl冒頭）、`getControlInput`/`hasPendingJump`/`hasPendingStandUp`。Clientは`clientRecordInputStep`で1step=1seqの入力と適用前Root位置・速度を記録し、`clientSendInputs`で未ack分を毎フレーム最大32件まとめて送る。Hostは`hostQueueInputs`→`hostConsumeInputStep`（ピア別`PeerInputQueue`、3件溜めて開始、未着は直前移動を継続しcursor据え置き、12件超で古いものを破棄）。AvatarBatchのposeは「そのseqを使う直前の状態」。補正は0.25stud超で位置＋速度差を適用。`AvatarInputWire`の`jumpSeq/standUpSeq`、`bufferLocalInput`、`m_pendingAvatarInput`等を削除。AvatarStateの形式変更（`NetworkTypes.hpp`のコメント更新）。
- **2c 補間表示**: `System.NetworkInterpolationDelay`（秒、0〜1、既定0.1、luaReadOnly、`SceneRuntime::copySystemScalars`、`doc/Instances/System.md`、`spec.md`）。AvatarBatch/WorldTransformsの先頭にHost tick（`hostTickForSend`）。Clientは`TimedPose`のスナップショット(最大64)を`pushSnapshot`/`sampleSnapshots`で補間、`clientNoteHostTick`/`clientAdvanceRenderClock`で表示時刻を管理。指数平滑を廃止。Client側リモートアバターのボディは重力OFF。
- **テスト**（`test_main.cpp`）: 新規`--network-avatar-regression`（Hostプロキシのホバー/ジャンプ/重複jump、Client→Host同一入力の移動量一致、補正、WorldTransformsワールド座標、Weld同期物体、補間、Seat権威/着席高さ）、`--character-tick-control-regression`（60fpsと30fpsで同一結果、ジャンプ要求の消費）。`ReplicationTestAccess`を拡張。既存`--remote-avatar-spawn-transform-regression`の1項目を新方式の期待値へ変更、セキュリティテストの`worldTransformsPayload`にtickを追加。

### 2. なぜそうしたか
- **降車の傾き原因**: RootのLockFlags(AngularX|Z)は「その時点の姿勢」で回転を固定する。傾いたまま`createActor`されるとGyroのX/Zトルクがロック軸に通らず二度と起き上がれない。ロックはそのままに、姿勢を直立にしてから降ろす方を選んだ。Rootだけ起こすとMotor6Dのばねで手足に引っ張られるため全ボディを動かした。
- **方式: Host権威+tick決定論 vs 完全ロックステップ vs Client権威** → Host権威+tick決定論（ユーザー選択）。Box3Dにワールドのスナップショット/巻き戻しAPIが無く全世界リプレイは不可。ロックステップは入力遅延・途中参加の全状態転送・Win/Mac間の非決定性リスク。
- **2段階**で進めた（ユーザー選択）。第1段階で明確なバグを潰して症状を軽くし、第2段階で構造を変えた。
- **Weld compoundの同期をAPI追加で解決**（ユーザー選択）: Replicationだけで瞬間移動する案は速度0になり、車体に立つキャラが運ばれない。
- **固定tick化は全モード**（ユーザー選択）: オフラインとネットワークで制御経路を一本化。1updateの2step目以降だけキャラ7ボディを`syncCube`（`syncCube`は`m_bodies`線形探索なので毎stepは大量Cubeシーンで重い。1step目は前フレーム末の`syncAllCubes`で最新）。
- **着席Host権威**（ユーザー承認）: 各ピアが独立に接触着席していたのが二重着席の原因。着席が往復遅延ぶん遅れる代わりに食い違いが起きない。降車はローカル予測で即時。
- **2bのジャンプ/降車はtick入力のフラグに統合**: 入力自体が冗長送信されseqで重複排除されるので、第1段階のjumpSeq方式は不要になった。
- **Host入力バッファの未着時はcursorを進めない**（holdで遅延を自動拡張）＋溢れたら破棄。進める方式だと遅れて着いた入力が常に捨てられ予測外れが続く。
- **補間遅延はSystemプロパティ**（ユーザー選択）。表示時刻は「最新受信tick＋受信後経過」から推定し緩やかに追従（受信間隔ごとの段差で時刻が跳ねないように）。

### 3. どういう経緯か
1. 調査依頼（ネットワークのジッター・向き・ジャンプ、Seat降車後の傾き）→コード調査で原因を列挙。
2. Seat降車修正→ユーザー確認OK。
3. 第1段階を計画（Plan mode）→実装→実機で「車体が置き去り、車輪が横転」。
4. 使い捨てプローブ（`--car-probe`/`--car-client-probe`、Floating world/baseplate.yaml読込）でHost物理は正常、Clientでweld compoundのAnchored体が`syncCubeWithBodyState`によりbody poseで上書きされ動かないことを特定→キネマティック目標API追加。
5. 「旋回すると車輪が曲がる」→プローブでオフラインでも再現（6秒で軸ずれ約85°）。原因は車体(約400)と車輪(約13)の質量比。車輪MassDensity=10で約4°、SUB_STEPS=8で約13°。ユーザーがMassDensityを上げて解決（エンジンは変更せず）。
6. 第2段階: 2a→実機OK→Seat二重着席・着席高さの報告→修正→実機OK→2b→実機OK→2c。

### 試して失敗・やり直した方法
- `MOTOR_CONSTRAINT_HERTZ`を240→60に変えても結果がビット単位で同一。Box3Dは`joint.c:1466`で関節hertzを`0.25*サブステップ周波数`(=60Hz)に頭打ちしている（240設定は実質無効）。
- プローブでMotorの`DriveVelocity`をフィールド直書きしても物理に反映されない（`setDriveVelocity`→`updateConstraint`経由が必要）。
- 2aのテストで経過時間をfloat積算すると60fps/30fpsで入力切替フレームがずれ0.24studの差に見えた。tick数で判定すると差は2e-6。
- 比較テストでSpawnLocation(8×8)から歩いて落下しmaxTravel=368になった→広い床を追加。
- 着席テストで脚の下面が座面より0.55下→脚がMotor6Dのばねで重力に負け約73°しか曲がっていないだけで、股関節高さは正しかった（テストは股関節基準に変更）。
- Bashヒアドキュメント内のPythonで`'\n'`を書くと実改行になりC++ビルドが壊れる（今回も2回）。スクラッチパッドに.pyを書いて実行するか、Editツールを使う。
- `--character-rig-v2-regression`という名前の専用テストは存在しない（`Unknown regression mode`）。

### 4. 未解決・保留
- **2cの実機確認がまだ**（他プレイヤーの滑らかさ、沈み込み解消、`NetworkInterpolationDelay`の効き）。リモートアバターの補間と重力OFFはClient役（実ネットワーク）でしか動かず自動テスト未検証。
- 着席中の脚がMotor6Dのばねで約73°までしか曲がらず垂れる（ユーザーに報告済み、未対応）。
- 車輪が曲がる問題はコンテンツ側（MassDensity）で回避。エンジン側の対策（質量比の警告、効いていない`MOTOR_CONSTRAINT_HERTZ`の整理、サブステップ増）は未実施。
- Client入力送信は60Hz×未ack最大32件（RTT100msで約30KB/s）。量子化等は未検討。未ackが32件を超える高遅延ではHostが入力を取りこぼす。
- Client側の補正は巻き戻しなし（位置は1回最大0.5stud、速度差は全量）。Clientではキネマティックな同期物体・他プレイヤーとの接触で誤差が出うる。
- PhysXバックエンドはtickフック未対応（PhysXは既に起動不可）。
- `SeatState`再送はロスター増加時のみ。Clientがローカル予測で降車した直後にロスターが増えると一瞬再着席する可能性。
- `--audio-diagnostics-regression`の失敗は継続（10/08から、今回と無関係）。
- `scripts/Recubin.luard`はセッション開始前からの変更でコミット対象外。

### 5. 暗黙仕様の発見
- **Rootの`LockFlags`(AngularX|Z)は作成時の姿勢で回転を固定する**。傾いた姿勢でボディが作り直されると直立に戻れない（Gyroも効かない）。
- **Weld compoundのAnchoredボディは同期時にbody poseが正**（`syncCubeWithBodyState`の`sharesBody`分岐）。論理CFrameを書いても動かない。単独Anchoredは論理→`SetTargetTransform`。
- **Box3Dの関節hertzは`0.25*inv_h`で頭打ち**（SUB_STEPS=4なら60Hz）。
- **`syncCube`は`m_bodies`の線形探索**（O(N)）。
- **エディターのローカルサーバーは非プレイヤーの専用Host**（`startHost(0,false)`）。
- **`setMemberWorldCFrame`(=`setBodyWorldCFrame`)は線速度・角速度を0にする**。
- **接触イベント（Seat着席など）は`Physics::update`末尾の`dispatchContactEvents`でまとめて配送**され、固定stepループ中には起きない。
- 既定リグは股関節がRoot中心の1下、baseplateのStarterCharacterはRoot 2×4×1で股関節がRoot中心（リグごとに着席高さが違う）。
- キャラ制御はPlay中、固定step(1/60)ごとに`Humanoid::stepControl`で進む。`User`は入力を渡すだけ（spec.md未記載）。

## 2026-10-11 ネットワーク物理の演算主導権化・車輪Motorの車軸曲がり修正

前セッション（641f0f6「Host権威+固定tick決定論」）の続き。コミットは 27fd630（主導権化）と 4c38fae（Motor/サブステップ）。

### 1. 何をしたか
**Clientの同期物体のstepごと駆動（主導権化の前段、27fd630に含む）**
- `Physics`: `addPreStepCallback`（owner付きの固定stepごとのコールバック）を追加。
- `ReplicationManager::clientStepWorldObjects`: 物理ボディを持つ同期物体へ、固定stepごとにそのstep後の時刻の補間姿勢をキネマティック目標として与える（Weld compoundはnative bodyごとに1回）。目標が4stud以上離れていれば瞬間移動（`driveKinematicBody`）。

**演算主導権（Network Ownership）への切り替え（27fd630）**
- `NetworkTypes.hpp`
  - `AvatarState` を入力列から「Clientが演算した自キャラの姿勢」（Clientのtick・pos・quat・速度・walkCycle・接地・操作軸）に再定義した。
  - `OwnedTransforms=12` / `Ownership=13` / `JointDrive=14` / `StandUpRequest=15` を追加した。
  - `AvatarBatch` から `lastProcessedSeq` を削除した。
- `NetworkManager.cpp`: ゲームメッセージの範囲を `StandUpRequest` まで広げた。
- 新規 `SnapshotClock`（`include/Network`・`src/Network`）
  - 相手のtickを追う補間時計。表示時刻とstep時刻を持つ。
  - Clientは「Hostの時計」として1つ、Hostは「Clientごとの時計」として1つずつ使う。
  - 旧 `clientNoteHostTick` / `m_renderTick` / `m_kinematicTick` を置き換えた。
- `Replication.cpp/.hpp`
  - **削除したもの**：入力の記録・再送・ジッターバッファ・補正の一式。
    - `clientRecordInputStep` / `clientSendInputs`
    - `PeerInputQueue` / `hostQueueInputs` / `hostConsumeInputStep` / `hostDrainInputSurplus`
    - `reconcileLocalPose` / `m_lastProcessedSeq`
  - **Host側に追加したもの**
    - `hostReceiveAvatarState`
    - `hostStepProxies`：Host上の他プレイヤーを、動的ボディのまま受信姿勢へ速度で追従させる。重力は切り、4stud超は瞬間移動する。着席・死亡・Ragdoll中は追従しない。
    - `hostSimulateAvatars`：歩行表示に加え、着席中はClientの操作軸を `Seat.Throttle/Steer` へ書く。
    - 主導権の処理：`collectMechanism` / `hostUpdateOwnership` / `hostAssignOwnership` / `hostReclaimOwnership` / `hostSendOwnership` / `hostSendJointDrives` / `hostReceiveOwnedTransforms` / `hostStepOwnedObjects`
  - **Client側に追加したもの**：`clientSendAvatarState` / `clientSendOwnedTransforms` / `clientNotifyStandUp` / `clientApplyOwnership` / `clientApplyJointDrive`
  - **その他**
    - `onPreStep`：Client・Hostの役割で分けず、両方の処理を呼ぶ（対象が無い側は何もしない）。
    - `translateLocalCharacter` を `translateCharacter(Instance&, ...)` に一般化した。
    - `enablePhysicsProxy`：proxy内のGyroとForceを止め、`setTickControlled(false)` にする。
    - `hostRescanWorld`：主導権を渡してAnchoredにした部品も同期対象に残す。
    - `setSyncedWorldCFrame`：同期で部品を動かすとき、Attachmentのローカル姿勢を保つ。
- `doc/Instances/Seat.md`：ネットワーク時の着席・降車・主導権の流れを記述した。

**Motor車軸の曲がりの修正とPhysicsSubsteps（4c38fae）**
- `Workspace.PhysicsSubsteps`（int、既定8、範囲1〜16）を追加した。YAML・Luau・エディターに対応し、Cloneはレジストリ経由で複製される。
  - Box3D側は固定値の `SUB_STEPS=4` を廃止し、物理更新のたびに `Workspace` からクランプして `m_subSteps` に取る。
- `Model.cpp`：`syncPivotToCentroid` / `syncPivotToPrimaryCube` で、部品に付いたAttachmentのローカル姿勢を戻すようにした（`movePivotKeepingCubeAttachments`）。
- `Physics::reconcileConstraints`：関節の取り付け位置の比較（`sameFrame`）に許容誤差を入れた（位置1e-4、回転はdotで1e-7）。
- `Box3DPhysicsBackend::createMotor` / `Motor.hpp` の `NativeJointFrames`
  - 最初にnative jointを作ったときの支点と、Cube1から見た向きを、各Cubeのローカル座標で保持する。
  - ボディだけが作り直されたとき（着席のWeld合成、Anchoredの切り替え）はこれを再利用する。
  - Cube・Attachment・Axisが変わったときは使わない。
  - Servoは、作り直し直後に今の角度から目標への速度を入れ直す。
- `MOTOR_CONSTRAINT_HERTZ` に、関節の硬さの上限（15Hz×サブステップ数）についてのコメントを足した。
- doc：`Workspace.md`（PhysicsSubsteps）、`Motor.md`（ボディの作り直しでは基準が変わらないこと）を更新した。
- テスト
  - `--network-avatar-regression` を全面的に作り直した。
    - proxyの追従と向き、瞬間移動
    - Seatの操作軸
    - 機構の収集
    - 主導権の付与と回収
    - JointDrive
    - 受信姿勢で動かした車を受け取っても車軸がずれないこと
  - 新規 `--motor-axle-regression`
    - YAMLでの読み込み
    - サブステップ数ごとの傾き
    - 運転中にjointが作り直されないこと
    - 作り直しで傾きが取り込まれないこと

### 2. なぜそうしたか
- **入力再生をやめて主導権へ**
  - Host権威＋入力再生では、Host・Clientどちらかのフレームが詰まると、入力バッファが空になるか溢れた。その結果、読み飛ばし・補正・ワープ・着席待ちが起きた。ローカル検証では3窓が1台で動くので頻発する。
  - バッファ方針を2回変えても解消しなかったので、ユーザーの判断でRoblox方式に切り替えた。
  - 主導権化すると、入力の遅れが自キャラに影響する経路がなくなる。
- **Host proxyを動的ボディのまま速度で追従させた**：キネマティックにすると、Anchored（Box3Dではキネマティック）の物体との接触イベントが出ず、Touched（座席・キルブロック等）が壊れるため。
- **着席はHost権威のまま、降車はClientが先に立つ**：着席の二重化を防ぎつつ、降車の応答を速くするため。Clientは以前からローカルで立っていたので、入力フラグを専用メッセージに置き換えた。
- **Motorの駆動値の複製**：車のScriptはHostでしか動かない（Clientでは `LocalScript` 以外を実行しない）。そこで、Hostのスクリプトが設定した値を所有Clientへ送る（1往復分の遅れはRobloxと同じ）。
- **車輪の関節（b3WheelJoint）への変更は不採用**
  - 測定で改善しなかった（密度1・前進旋回で14.5°、revoluteは11.4°）。
  - 軸を保つ拘束はrevoluteと同じ2×2の解き方で、同じ硬さの上限がかかる。
  - 回転角を取得する関数がなく、Servoも実現できない。
- **サブステップはWorkspaceのプロパティにした**（ユーザーの選択）：効いた唯一の候補だが計算量に比例するので、ゲームごとに上げられるようにした。
- **車軸の曲がりは「関節の作り直し」を3段で止めた**
  - 発生源（Modelのpivot同期による丸め誤差）を止めた。
  - 比較に許容誤差を入れた（他の経路から誤差が入っても毎step作り直さない）。
  - 正当な作り直し（着席・Anchoredの切り替え）でも、元の基準を使うようにした。
  - 1つだけだと、残りの経路から再発するため。

### 3. どういう経緯か
1. 前セッションのコミット後、ユーザーが「車輪が回るのは根本対策を」「Clientが運転するとジッター。演算主導権の譲渡は？」と提案した。
   - ①Clientのstepごとのキネマティック駆動 → ②Jointの変更 → ③主導権、の順で進める合意をした。
2. ①を実装すると挙動は改善したが、「Wを押しながら乗ると、入れ直したとき一気に飛ぶ」「ジッターで乗れない」との報告があった。
   - Hostのログで、入力バッファが溢れて約40stepずつ読み飛ばしていることが分かった。
   - **1回目の修正（失敗）**：未着で待つ方式を「毎tick必ず進む」方式に変えた。
     - Client側で重いフレームに物理stepが間引かれると、Hostの読み取り位置がClientより先へ進み、以降の入力をすべて読み飛ばす状態から戻れなくなった。Clientの補正が一度も出ないことで判明した。
   - **2回目の修正（失敗）**：「何も届いていなければ3件溜まるまで待つ。抜けたseqは進める。余剰は1秒窓の最小値で少しずつ捨てる」方式にした。
     - それでも溢れが連続し（約0.7秒ごとに約40件まとめて届く）、改善しなかった。
3. ユーザーが「主導権の譲渡」を選び、計画を承認したので実装した（27fd630の主要部分）。
4. 実機で「車輪の軸が違う」「降車時に車体が落ちる」「分解する」との報告があった。
   - プローブで再現した：Anchoredの車を動かしてから解放すると、車体が沈んで走らない。
   - 原因は、Replicationが `Spatial::setWorldCFrame` で部品を動かし、子のAttachmentが元の場所に取り残されていたこと。そのため主導権の切り替えでMotorを作り直したとき、支点が走行距離ぶんずれていた。
   - `setSyncedWorldCFrame` で修正した。修正を外すとテストが失敗することも確認した。
5. ジッターの解消が確認できたのでコミットした（27fd630）。
6. Motorの制約破綻の根本対策に入った。
   - 計画に沿って、候補（現状 / WheelJoint / サブステップ / 減衰比 / 慣性）をプローブで測定した。サブステップだけが効いた。
   - ユーザーの選択で、`Workspace.PhysicsSubsteps` を実装した。
7. 実機で「サブステップ16でも、しばらく運転すると車軸がめちゃくちゃになる」との報告があった。
   - 作り直しで傾きが取り込まれることを、テストで再現した。
   - 診断ログを入れて、**4つのMotorのjointが毎step作り直されている**ことが分かった。
   - 原因を順にたどった：比較が完全一致 → Attachmentのローカル姿勢に1e-6の誤差 → 発生源は `Model::syncPivotToCentroid`。
   - 3段の修正後、運転中の作り直しは0回になった。現状4サブステップでの傾きも、11.4°から4.2°に下がった。
8. ユーザーが改善を確認し、コミットした（4c38fae）。

**試して失敗した方法**
- 入力バッファの「毎tick必ず進む」方式：Clientのstepが間引かれると永久に読み飛ばす。
- 入力バッファの「空なら待つ＋抜けは進める＋余剰を捨てる」方式：ローカル3窓の詰まりに勝てない。
- b3WheelJoint：軸の傾きは改善しない。Servoも使えない。
- 減衰比を上げる（5、10）：悪化した。
- 車輪の、軸に垂直な方向の慣性を大きくする：傾きは0.3°まで下がるが、車全体が曲がれなくなる（車輪の慣性が車の向きを変える回転にも効くため）。
- サブステップを増やすだけ：jointが毎step作り直されている間は、16でも車軸が曲がったまま残る。

### 4. 未解決・保留
- **主導権まわりの制約**
  - Clientの自キャラは、Hostが演算する物体（Client側ではキネマティック）を押せない。Host上のproxyは押せる。
  - 降車の直後、Client側の車が補間の遅れの分だけ一瞬戻ることがある。
  - 他プレイヤーの表示は約0.2秒遅れる（Hostの補間0.1秒＋Clientの補間0.1秒）。
- **不正対策**：非有限値の拒否だけ。速度上限・瞬間移動の検証はしていない（リスポーン等と区別が要る）。
- **使われなくなったAPI**：`Humanoid::setControlStepCallback` は、Replicationから使われなくなった（範囲外なので残した）。
- **重複した処理**：Attachmentのローカル姿勢を保つ処理が3か所にある。整理は未実施。
  - `Box3DPhysicsBackend` の `syncCubeWorldCFramePreservingAttachments`
  - `Replication` の `setSyncedWorldCFrame`
  - `Model` の `movePivotKeepingCubeAttachments`
- **Anchoredの部品の丸め誤差**：Modelのpivot同期で、Anchoredの部品自身のワールド姿勢にも毎フレーム丸め誤差が乗っている可能性がある（未確認）。
- **一時的なたわみ**：運転中の車輪のたわみは、密度1・既定8で約2.5°残る（止めれば戻る）。気になる場合はPhysicsSubstepsを12〜16にする。
- **既定値変更の影響**：PhysicsSubstepsの既定を4から8にしたため、全シーンが少し硬くなり、ソルバーの計算は約2倍になる。baseplateでは差が測定誤差の範囲だった。大量の動的物体があるシーンでは未計測。
- **baseplateの車の作りの問題**：車輪のAttachmentが、車体側と1stud食い違っている（z方向）。joint作成時に引き寄せられている。コンテンツ側の問題。
- **継続中の既知事項**
  - `--audio-diagnostics-regression` の失敗（10/08から、今回と無関係）
  - `scripts/Recubin.luard` はコミット対象外

### 5. 暗黙仕様の発見
- **`Spatial::setWorldCFrame` は子孫のワールド姿勢を保つ**（子は一緒に動かない）。Attachmentも置いていかれるので、部品を動かすときはAttachmentのローカル姿勢を戻す必要がある。物理同期の `syncCubeWorldCFramePreservingAttachments` はこれをしている。
- **`commitCFrame` の扱いは更新元で違う**：Physics・Networkが更新元のときは子孫のワールド姿勢を保ち、それ以外（Deserialization等）は子孫のローカル姿勢を保つ。
- **`Physics::reconcileConstraints` は毎フレーム比較し、変化があれば関節を作り直す**：比較するのは、拘束ごとの「Cube・ボディ・Attachment・ローカル姿勢・Axis」の組。差があればnative jointを破棄して再登録する。Weld以外は、ボディが作り直されただけでも作り直す。
- **`Box3DPhysicsBackend::createMotor` は、その時点の姿勢から関節を作る**：支点はAttachmentから、向きは「Cube0の向き×Axis」から作る。今回、2回目以降は最初の基準を使うように変えた。
- **Anchored⇔非Anchoredの切り替え（`BaseCube::setAnchored` → `recreateActor`）では、付いている拘束がすべて削除される**（`removeCube`）。次の `reconcileConstraints` で作り直される。
- **Scriptの実行場所**：`UseNetwork` が有効なとき、Clientは `LocalScript` だけを、プレイヤーでない専用Hostは `Script` だけを実行する（`skipScriptForNetworkContext`）。
- **エディターのローカルサーバーのClientログ**は `%TEMP%\RecubinStudio\local-server-*\client-N.log` に出る（リポジトリ内ではない）。
- **Box3Dの車輪の関節（b3WheelJoint）**
  - 回転軸は関節の座標のZ、サスペンションの軸は関節の座標のXになる。
  - 回転角を取得するAPIがない。

## 2026-10-14 プロパティ欄の改善・TextLabel揃え/TextScaled・ビューポートでのGUI編集（GUIタブ/Locked/リサイズ/非固定解像度）

### 1. 何をしたか
**プロパティ欄（`PropertiesPanel.cpp`）**
- 色スウォッチ：`drawColorSwatch` を追加し、Color4 行の先頭に実際の色を表示する。クリックで既存の Palette ポップアップを開く。複数選択で値が食い違うときは、先頭の色を表示してツールチップで混在を示す。
- CFrame の統合入力を単一選択でも表示：`Spatial.cpp` の `cframe.multiOnly()` を外した（`noYaml` は維持）。
- 数値欄の Undo/Dirty 漏れの修正：`mergeEditEdges` を追加した。Vec3・Color4 のドラッグ欄と CFrame の Rotation 欄の「編集開始」と「確定」を、行の最後で行う Undo 記録の判定に合流させる。単一選択・複数選択とも対象。

**TextLabel / TextButton**
- `GuiContent.hpp`：`TextContent` に以下を追加した。
  - `XAlignment`（Left/Center/Right、既定 Left）
  - `YAlignment`（Top/Center/Bottom、既定 Center）
  - `TextScaled`
- `GuiContentProps.hpp`：`textXAlignment` / `textYAlignment` / `textScaled` を追加した。既定値のときは `serializeIf` で YAML へ出力しない。`TextLabel.cpp` と `TextButton.cpp` で登録した。
- `Renderer_GUI.cpp` の `drawGuiText`
  - `TextContent` を受け取る形にした。
  - 行ごとに横揃えして描画する。
  - TextScaled は拡大・縮小の両方で枠いっぱいにする。
  - BillboardGui 用の `fitToBounds`（縮小のみ）は残した。
- `RCBN.luah`、`doc/Instances/TextLabel.md`・`TextButton.md`、`spec.md` を更新した。

**画面GUIの矩形計算の共通化**
- 新規 `Core/ScreenGuiLayout`：`pixelScale` / `computeRect` / `hasWorldGuiAncestor` / `collectScreenElements`。
  - `Renderer_GUI.cpp` のインライン計算と static の `hasWorldGuiAncestor` を置き換えた。
  - `pixelScale` に `uniform` 引数（縦横同率）がある。

**ビューポートでの GUI 編集（新規 `Editor/ViewportGuiEdit.cpp`。ViewportPanel のメンバー関数）**
- クリック選択（最前面）、Ctrl での追加・解除、5px の閾値を超えたらドラッグ移動。
  - Undo：移動は `SetPropertyCommand` の `CompositeCommand` で1回分として記録する。
  - 吸着：AABB の左・中央・右と上・中央・下を、他要素とビューポートの同じ線へ 6px 以内で吸着させる。計算は新規 `Editor/GuiSnap` の `snapRect`。
  - 選択枠、ホバー枠、ガイド線を描く。
- Shift 押下中だけ、プライマリ選択の要素に 8 つのリサイズハンドルを出す。
  - 計算：`GuiSnap::resizeRect` / `snapLine` / `resizeHandles`。
  - Undo：Position と Size をまとめて1回分として記録する。
- `ViewportPanel::onRender`
  - GUI タブ中（`isGuiEditMode`）は、3D の操作を全部止める（地形ブラシ、Weld、クリック選択、矩形選択、ギズモ、自由ドラッグ、ピボット）。
  - GUI タブ以外では GUI はクリックを素通りし、選択枠だけを描く。
- `Locked` の要素はクリックが素通りし、ドラッグ・リサイズ・揃えの対象外になる。

**ツールバーの GUI タブ（`EditorManager::renderToolbarGui`、`ToolbarCategory::Gui`）**
- ボタン：
  - 4 種の追加（Workspace 直下の画面中央に置き、選択状態にする）
  - 基準解像度のプルダウン（16:9 と 4:3）
  - 揃え 6 方向（1 つなら画面に、複数なら選択範囲全体に揃える）
  - 全画面化
  - 吸着 ON/OFF（`GuiEditState::snap`）
- 処理は新規 `Editor/GuiToolbarActions` にまとめた：`addElement` / `align` / `fitToScreen` / `setBaseResolution` / `editableSelection` / `resolutionPresets` / `baseResolution`。
- `ViewportPanel.hpp` に `GuiEditState` を追加した。EditorManager が所有し、全ビューポートで共有する。
- `Localization` に文言を追加した。`IconsDef.hpp` に揃え・磁石・拡大のアイコンを追加した。

**Locked / Alt / 固定解像度**
- `ScreenGuiObject.Locked`：エディター用。true のときだけ YAML へ出力する。`RCBN.luah` と doc にも追記した。
- `User::setAltLookAllowed`：false の間は Alt のフリールック（マウスロック）を開始せず、開始済みなら解除する。EditorManager が「GUI タブ中かつ Play 中でない」ときに false にする。
- `System.UseFixedResolution`：エディター用、既定 true、`luaHidden`、false のときだけ YAML へ出力する。`SceneRuntime::copySystemScalars` にもコピーを追加した。
  - false のとき、`ViewportPanel::renderLayoutAndScene` は黒帯なしで、パネルの実サイズを描画解像度にする。
  - Pixel 単位の GUI は、`GameGuiRenderContext::uniformPixelScale` と `ViewportLayout::uniformGuiScale` により縦横同率で拡大する。
  - `Renderer::renderScreenGui` に既定値付きの引数 `uniformPixelScale` を追加した。

**テスト（`test_main.cpp`）**
- `--gui-text-layout-regression`：揃え・TextScaled の YAML・複製・Luau、矩形計算、吸着、CFrame の editable。
- `--gui-toolbar-regression`：Locked、追加、揃え（Pixel/Scale、単一/複数）、全画面化、解像度、Undo。
- `--gui-viewport-edit-regression`：リサイズ計算、縦横同率の拡大、UseFixedResolution、Alt フリールックの抑止。

### 2. なぜそうしたか
- **AABB フィットは「磁石のような吸着」にした**：ユーザーの説明は「ぶつかり、スライドすると中央で揃う」。押し返す衝突だと精密な配置を邪魔するので、Figma 型のスマートガイドにした。左辺と右辺の吸着で「ぶつかって隣接」が実現できる。
- **矩形計算は Renderer の関数にせず `ScreenGuiLayout` に分けた**：エディターが GL を持つ Renderer に依存しないため。テストからも直接呼べる。
- **揃えの既定値は Left/Center にした**：従来の描画と同じなので、既存シーンの見た目が変わらない。YAML にも差分が出ない。
- **Locked は「クリック素通り」にした**（ユーザーの選択）：全画面の背景をロックすれば、手前の要素も奥の要素も触れる。BaseCube の Locked（最前面ならクリックを遮る）とは挙動が違う。
- **Locked に `group("Editor")` を付けなかった**：見出しは後続のプロパティにも引き継がれる。ScreenGuiObject に付けると、派生クラスの Text などまで「Editor」の下に入ってしまう。
- **縦横同率の拡大はエディターのビューポートだけにした**：ランタイム（`NullEditorManager`）はウィンドウ全体に、縦横別の倍率で描いている。エディター用のプロパティでランタイムの見た目を変えないため、`GameGuiRenderContext` で切り替える。
- **GUI タブの揃え・全画面化は、非固定解像度でも BaseResolution の枠を基準にした**：ランタイムで表示される範囲に合わせるため。
- **リサイズ対象はプライマリの1要素だけにした**：複数要素を同時にリサイズする仕様は決めておらず、精密な調整の用途を優先した。

### 3. どういう経緯か
1. 最初の指示は「プロパティ欄で色が見えない／CFrame に統合入力がない」と「TextLabel の揃え・フォント・Scaled／ドラッグ移動／AABB フィット」。
   - 調べると、フォント設定は既存の Font / FontFile の UI で足りていた（ユーザーの確認により対象外）。
   - CFrame の統合入力は実装済みだったが、`multiOnly` のため複数選択でしか表示されていなかった。
2. 計画を承認してもらい、1〜4 の順で実装した。
   - 途中で、Python の一括置換で `hasWorldGuiAncestor` を消すつもりが `drawGuiText` ごと消してしまった。diff で気づいて復元した。
3. 報告時に「数値欄のドラッグが Undo に記録されていない疑い」を伝えた。ユーザーが「手動入力で Dirty にならない」と確認したので修正した（`mergeEditEdges`）。CFrame の Rotation 欄にも同じ漏れがあった。
4. ユーザーから「全画面の GUI があると 3D を編集しにくい」として、GUI タブ・クイック追加・Locked の追加を依頼された。ボタンの種類とモードの挙動を質問で確定してから実装した。
5. 実機テストで 3 点の報告があった。
   - Alt が競合して吸着の一時オフが効かない。原因は `User::processCameraRotation` の Alt フリールックだった。
   - リサイズがほしい。
   - 黒帯が無駄。
   
   3 点とも対応した。固定解像度については、「解像度の出力＝描画解像度に使う」「縦横同率」「既定 true」を質問で確定した。

**試して失敗した方法・ハマりどころ**
- bash の heredoc で Python スクリプトを渡すと、クォートやエスケープが壊れた。文字列中の `'\n'` が実際の改行になったり、`"\xef..."` が化けたりした。スクラッチパッドにファイルとして書いて実行する方式（`editlib.py`）に切り替えた。
- CRLF のファイル（`Localization.hpp`/`.cpp`、`spec.md`、`Spatial.cpp`）では、LF 前提の置換が一致しなかった。読み込み時に改行を正規化し、書き込み時に元へ戻すようにした。
- テスト内で変数名を `far` にしたら、Windows のマクロと衝突してコンパイルエラーになった。
- 吸着のテストで、期待値を「上辺どうし」と誤っていた。実装は正しく、中央線の方が近かった。

### 4. 未解決・保留
- **見た目と操作感はユーザーの実機確認待ち**：スウォッチ、揃え、ドラッグ、ガイド線、GUI タブのボタンの並び（ツールバーの幅に収まるか）、ハンドルの掴みやすさ、黒帯なしの表示。
- **リサイズは単一要素のみ**。複数選択のリサイズ（グループの拡大縮小）は未対応。
- **AnchorPoint / UDim2 / 親子の入れ子レイアウトは無い**：GUI はすべて Workspace のフラットなリストで、ビューポート基準。揃えやドラッグもこの前提で作っている。
- **`PropertyTextInput` のヒントが `"x, y, z"` 固定**：CFrame（6 値）や Color4（4 値）でも同じ表示になる。直すには関数シグネチャの変更が要るので未対応。
- **TextButton の `InvisibleButton`**：編集中もアクティブ項目になるため、`IsWindowHovered` に `AllowWhenBlockedByActiveItem` を付けて回避した。編集中にボタンの Activated が発火するかは未確認。
- **`SystemFont::MPlus = 2` は C++ 側にだけあり、enum 名として登録されていない**（既存。今回は触っていない）。
- **継続中の既知事項**
  - `--audio-diagnostics-regression` の失敗（10/08 から、今回と無関係）
  - `scripts/Recubin.luard` はコミット対象外

### 5. 暗黙仕様の発見
- **PropertiesPanel が行ごとに Undo を記録するとき、`IsItemActivated` / `IsItemDeactivatedAfterEdit` は最後のウィジェットしか見ない**。1 行に複数のウィジェットを置く型では、各ウィジェットの直後に `mergeEditEdges` で合流させる必要がある（漏れると Undo も Dirty も起きない）。
- **シーンの Dirty は `CommandHistory` の execute/record（`m_onChange`）に連動する**。値を直接書き換えても Dirty にならない。
- **`PropertyDesc.separator`（`group()`）は後続のプロパティにも引き継がれる**。基底クラスで付けると、派生クラスのプロパティも同じ見出しの下に入る。
- **画面GUIはツリー構造に関係なく、Workspace のフラットなリスト（`getRenderScreenGuiObjects`）として描画される**。位置は親基準ではなくビューポート基準で、ZIndex の昇順に描く。
- **ランタイム（`NullEditorManager`）は黒帯を付けず、ウィンドウ全体に Pixel の GUI を縦横別の倍率で描く**。エディターの固定解像度の表示と一致するのは、ウィンドウの縦横比が BaseResolution と同じときだけ。
- **Alt は `User::processCameraRotation` で「押した瞬間にフリールックを切り替える」キー**。エディターで Alt を別の用途に使うときは、`setAltLookAllowed(false)` で止める必要がある。
- **`std::variant` の `PropValue` は `==` で比較できない前提で扱われている**。既存は `propValuesEqual` を使っている。

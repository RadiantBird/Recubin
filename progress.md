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

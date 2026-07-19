# 開発進捗ログ

セッションごとの作業記録。新しいセッションを開始する際はまず一番下（最新）のセッションを読むこと。

---

## 2026-07-18 起動時ようこそタブ + シーン新規作成

### 何をしたか

**Phase A: シーン新規作成の基盤（無題状態と初回保存）**
- `Localization.hpp/cpp`: `MenuNewScene` を MenuOpenScene の直後に挿入、末尾（Count直前）に WelcomePanel セクション5キー（`PanelWelcome`/`WelcomeMessage`/`WelcomeBtnNew`/`WelcomeBtnContinue`/`WelcomeBtnOpen`）を追加。enum と kTable の同位置挿入を厳守。
- `EditorManager.hpp/cpp`: `pendingNewScene` メンバと `requestNewScene()`（Editモード時のみ要求を立てる）。ファイルメニュー先頭に「シーンの新規作成」(Ctrl+N)、`handleEditorShortcuts()` に Ctrl+N。`saveCurrentScene()` を無題対応（scenePath 空なら saveFileDialog、キャンセルは何もせず dirty 維持 = 実質「名前をつけて保存」の新設）。
- `main.cpp`: 「Loadボタンによるシーンリロード」ブロックを新規作成と共用化（`pendingNewScene` 検知 → 空パスで `initNewScene`）。タイトルバー更新ブロックを「dirty か scenePath の変化」監視に拡張し、`updateWindowTitle` と `saveLastScenePath` をここに一元化（ロード/名前をつけて保存/新規作成のすべてで追随）。

**Phase B: WelcomePanel 新設 + 起動フロー変更**
- 新規 `include/Editor/WelcomePanel.hpp` + `src/Editor/WelcomePanel.cpp`: ConsolePanel の定型に倣った EditorPanel 派生。「Recubinへようこそ！」+ ボタン3つ（新規作成/前回の続きから/読み込み）。`SetNextWindowDockID(dockspaceId, FirstUseEver)` で中央ドックのタブとして初回配置。「前回の続きから」は lastScenePath が無効なら BeginDisabled、有効ならパスをツールチップ表示。読み込みはダイアログキャンセル時にタブを閉じない。
- `EditorManager.hpp/cpp`: welcomePanel メンバ追加、コールバック配線（onNewScene→requestNewScene / onLoadLast→requestSceneLoad / onOpenScene→openSceneDialog+pendingLoadPath判定）、表示メニューにトグル、タイトル設定 `###Welcome`。
- `main.cpp` 起動フロー: **LastScenePath 自動ロードと「ダイアログ→キャンセルで終了」を廃止**。無題の空シーン（scenePath=""）で起動し、`loadLastScenePath()` の結果は welcomePanel->lastScenePath に渡すだけ。起動時の `saveLastScenePath` も削除（前回パスを上書きで消さないため）。welcomePanel->isOpen は Panels 永続化に含めず起動時に必ず true。
- 検証: ビルド成功、回帰テスト 130 passed / 3 failed（既知ベースライン維持）。CMake は src/*.cpp を GLOB_RECURSE するため新規 cpp の登録作業は不要だった。

### なぜそうしたか

- **新規シーン＝「存在しないパスで loadAndBind」**: `SceneRuntime::loadAndBind` はロード失敗/Workspace 無しのとき空 Workspace+Lighting を自動生成する既存挙動があり（SceneRuntime.cpp:115-123）、専用の新規作成ロジックを書かずに済む。リロードは必ず main ループの後始末シーケンス（streamer→physics→resetSystemForReload の順）経由にした。EditorManager 内で直接ロードすると Terrain/Physics 破棄順クラッシュを踏むため。
- **無題→初回 Ctrl+S で保存ダイアログ**（vs 作成時に即保存先決定）: AskUserQuestion でユーザー選択。一般的なエディター挙動。
- **ようこそタブ＝中央ドックのタブ**（vs フローティング/モーダル）: ユーザー選択。ConsolePanel 型の通常パネル + SetNextWindowDockID で実現し、ViewportPanel のような特殊処理は不要。
- **タイトル/LastScenePath 更新の一元化**: ロードブロック内の直接呼び出しのままだと「名前をつけて保存」でタイトルが追随しない。既存の dirty 監視ブロックを scenePath 変化も見るよう拡張する方が、更新経路が1本になり漏れがない。

### どういう経緯か

1. デプロイパッケージ作成後、「デプロイ版は初回起動でダイアログ→キャンセル即終了になる」盲点にユーザーが気づき、ようこそタブ導入を依頼。
2. Explore 2並列で起動フロー（main.cpp:506-524）とGUI構造（DockSpace+パネル定型、Loc の enum/kTable 同順規約）を調査。「Save As が存在しない」ため新規シーンには初回保存の新設が必須と判明し、AskUserQuestion で上記2点を確認。
3. Phase A → B の順で implementer に委譲（B は A の LocKey/pendingNewScene に依存）。両方 git diff で指示どおりを確認。途中でセッション切断があったが、Phase A の変更はワーキングツリーに残っており Phase B から再開。

### 試して失敗した方法

- 特になし。

### 未解決・保留

- **実機確認はユーザーに委ねた**。観点: 初回起動（editor_settings.yaml 退避）でようこそタブが中央ドックに出るか / 3ボタンの挙動（前回無しでグレーアウト、読み込みキャンセルでタブ残留）/ 無題 Ctrl+S → ダイアログ → タイトルと LastScenePath 更新 / Ctrl+N・表示メニュー再表示 / JA/EN 切替（ローカライズずれ検知）。
- **無題状態で Play すると `PathfindingService::ScenePath = ""`**（navcache のパス導出元）。NavMesh ベイクを無題シーンで実行した場合のキャッシュパスが未検証。
- 既存ユーザーの imgui.ini には ###Welcome の配置記録が無いため初回は FirstUseEver で中央ドックに入るが、一度フローティングに剥がすと以後その位置を記憶する（仕様どおりだが認識しておく）。
- 無題シーンでは Packager（ゲームパッケージ化）がシーン未保存のまま動くとどうなるか未確認。パッケージ前に保存を促す導線は無い。

### 暗黙仕様の発見

- **updateWindowTitle は空パスで "Recubin Studio"（ファイル名なし）表示になる**既存仕様があり、無題表示はこれをそのまま流用した（「無題」の明示表示はしていない）。
- **通常の Ctrl+S 保存では LastScenePath は書かれていなかった**（起動時とロード時のみ）。今回の一元化で「名前をつけて保存」時にも書かれるようになった。

### 追記（同セッション）: ランタイムの imgui.ini 共有問題の修正

- 報告: 同梱の RecubinEngine.exe を開くとエディターの imgui.ini が書き換わりレイアウトが壊れる。
- 原因: ImGui 初期化は `Renderer::init`（Renderer.cpp）で共有されており、ランタイム（EDITOR_DISABLED）もドッキング無し・NullEditorManager でパネルを持たないのに既定の `imgui.ini`（CWD相対）を読み書きしていた。
- 修正: Renderer.cpp の EDITOR_DISABLED 分岐に `io.IniFilename = nullptr;` を追加し、ランタイムのレイアウト永続化を無効化。別ファイル名に分ける案もあったが、ランタイムに可動パネルが無く保存する価値が無いこと、パッケージ出力先にゴミ ini を作らないことから「書かない」を選択。RecubinTest も同定義のため同様に無効化される（ヘッドレステストには好都合）。軽微変更のためメインセッションが直接編集。ビルド確認済み。

---

## 2026-07-18 雨の貫通防止 + Weather子（RainEmitter等）の永続化

### 何をしたか

**雨（CollisionCutoff）の貫通防止** — `ParticleEmitter.hpp/cpp`
- `computeKillHeight()` 新設: 地形は `TerrainStreamer::raycastVoxel`（ボクセルDDA・PhysX非依存）、オブジェクトは Play 中のみ `Physics::raycast`、エディタ編集中は BaseCube への OBB スラブ法レイキャスト（`obbRayHit`、ViewportPanel の選択判定と同ロジックのファイルローカル関数）で判定し、最も高い命中面を killHeight に採用。
- `CutoffContext` 構造体（streamer / physics / CanCollide な cubes リスト）を update/emit ごとに1回 `buildCutoffContext()` で構築し、毎粒子のツリー走査を回避。PhysX 採用条件は `physics && getScene() && SystemState::get().isPlaying`。
- `Particle::cutoffRefresh` 追加: 落下中約0.2秒間隔で現在位置から killHeight を再計算（風で流されるズレに追従）。空振り時は0.5秒間隔に緩和。スポーン時に `frand01()*0.2f` で初期位相をばらけさせスパイク防止。

**Weather 子の永続化** — `SceneLoader.cpp` / `Weather.cpp` / `Weather.hpp`（コメント）
- `SceneLoader::saveNode` の WeatherSkyAnchor/WeatherLightningAnchor/WeatherAmbient 保存除外フィルタを撤廃し、全子を通常保存。
- `Weather::ensureChildren()` を adopt-or-create 化: ロード済みの子を名前+IsA で採用（プロパティは一切上書きしない）、無いものだけ従来の既定値で生成。これで RainEmitter 等へのユーザー編集がシーン保存で残り、再読込時の重複も起きない。
- 検証: ビルド成功、回帰テスト 130 passed / 3 failed（既知ベースライン維持）。エディタ/Play 両方での雨の停止はユーザー実機確認済み。

### なぜそうしたか

- **地形をボクセルDDAにした理由**: PhysX シーンに依存しないため、エディタ編集中・物理未登録チャンクでも貫通しない。エディタの地形ブラシが同じ理由で採用済みの実績パターン（ViewportPanel.cpp:578-）。
- **killHeight 定期再計算（vs スポーン時1回 / 毎フレーム判定）**: AskUserQuestion でユーザー選択。風ズレに追従しつつ、レイキャスト頻度を粒子あたり5回/秒に抑える折衷。
- **永続化は adopt-or-create（vs Weather プロパティへの昇格）**: ユーザーは RainEmitter に直接書き込んでおり、任意プロパティを保持するには子をそのまま保存するのが自然。SceneLoader は Sound/ParticleEmitter/Cube を全て生成できるためラウンドトリップ可能と確認済み。
- **Play 判定を `SystemState::get().isPlaying` にした理由**: BaseCube::setSize が同フラグで enqueue/直接実行を分岐しているエンジン内の実績ある判定。ランタイム exe は game_main.cpp:200 で常時 true。

### どういう経緯か

1. 「雨が貫通する」報告 → 調査で既存 CollisionCutoff がスポーン時1回の PhysX レイのみと判明。方針4点（両モード対応/地形+オブジェクト/定期再計算/単に消す）をユーザー確認して implementer で実装。
2. 「エディタでまだ貫通」+「RainEmitter の編集が消える」報告 → オブジェクト判定が PhysX 頼み（エディタで空振り）と、SceneLoader の Weather 子除外が原因と特定。OBB フォールバックと adopt-or-create を実装。
3. それでもエディタで貫通 → **失敗1**: `getPhysicsEngine()` 非 null 判定 — エディタでも Physics オブジェクトは存在。**失敗2**: `getScene()` 非 null 判定 — シーンも main.cpp:517 で起動時から存在。**失敗3**: `hasCubeActors()` 判定 — 編集中でも setSize 等の `recreateActor` で部分的にアクターが生成され不安定。→ 最終的に `SystemState::get().isPlaying` で確定し、ユーザー実機確認で解決。

### 試して失敗した方法

- 「PhysX が使えるか」をオブジェクト（Physics/シーン/アクター）の存在で推測する方法は全滅（上記3連敗）。**編集/Play の区別は SystemState::get().isPlaying を使うこと**。存在ベースの推測は編集中の部分的なアクター生成で必ず崩れる。

### 未解決・保留

- 既存シーンは Weather の子が未保存のため、**一度保存し直すまで**は従来どおり既定値生成（保存後から永続化が効く）。
- `Weather::clone()` は従来どおり子を複製しない → エディタで Weather を複製すると雨の編集は既定値に戻る（必要なら別途対応）。
- OBB フォールバックは Workspace 内の CanCollide な BaseCube 全走査（update ごと1回 + 粒子あたり5回/秒の全 Cube スラブテスト）。巨大シーンで重い場合は SpawnRadius ベースの XZ 距離フィルタを collect に足す余地あり。
- 保存 YAML に WeatherSkyAnchor 等（カメラ追従で Position はほぼ無意味な値）が入るようになった。実害は無い想定だが認識しておく。

### 暗黙仕様の発見

- **PhysX の Physics オブジェクトとシーンはエディタ起動時から存在する**（main.cpp:517 → Physics::init が即 createScene）。「Play 押下後のみ存在」は誤り。Play まで登録されないのはキューブアクターで、それも編集中の recreateActor（setSize/setAnchored/setMaterial 等）で部分的に生成される。
- Weather の子はこれまで名前ベースで保存除外されており（SceneLoader.cpp）、「ユーザーが Weather 直下に追加した子は保存される」が自動生成子の配下（RainEmitter 等）は丸ごと消えていた。

---

## 2026-07-19 動的ホスト移行（応用tier）完成 — リソース集計・自動選出・ロール切替Luau IF

### 何をしたか

readme.md「ネットワーク関係」の未完了3項目（リソース計測・集計 / ホスト離脱検知・自動選出・引き継ぎ / ロール切替インターフェース）を3フェーズで実装し、localhost 3プロセスでのライブ移行検証まで完了した。

**Phase 1: ピアID基盤＋リソース計測・集計**
- `NetworkTypes.hpp`: `PeerId`（0=無効、初代Host=1、移行を跨いで不変）/ `PeerEndpoint`（host+listenPort。NAT越えの将来拡張点とコメント明記）/ `PeerInfo`（旧PeerResourceInfoを置換）/ MessageType追加（Hello=2/Welcome=3/Roster=4/ResourceReport=5）/ `MigrationState`。
- `NetworkManager.hpp/cpp`: `poll()`をprivate化して`update(dt)`新設（poll＋周期送信）。Client→Hostへ0.5秒毎ResourceReport、Hostは`peer->roundTripTime`でレイテンシ観測し「変化時+1秒毎」にRosterをRELIABLE配布。`measureCpuScore()`（起動時1回、xorshift+float演算40M回のマイクロベンチ）。Hello受信でPeerId採番→Welcome返信。無名namespaceに境界チェック付き`ByteWriter/ByteReader`。
- `game_main.cpp`: `--listen-port`引数追加（省略時は接続先ポート）、`poll()`→`update(deltaTime)`差し替え。

**Phase 2: ホスト離脱検知・決定的自動選出・引き継ぎ**
- `updateMigration(dt)`状態機械（Electing/Rehosting/Reconnecting、総経過10秒でタイムアウト→shutdown）。
- Host切断検知はhandleEventのDISCONNECTで**フラグを立てるだけ**にし、実処理はpoll()から戻った後のupdate()で行う（enet_host_serviceループ中のm_host破棄は未定義動作のため。poll条件にも`m_host &&`ガード追加）。
- 選出は決定的（cpuScore最大→latencyMs最小→PeerId最小）。全Clientが同一Rosterを持つため合意メッセージ不要で同じ勝者に一致する。
- 勝者=`promoteToHost()`（Client host破棄→自listenPortで再create→Roster引き継ぎ）。敗者=`connectToEndpoint()`（初回0.5秒待ち→1.5秒間隔リトライ。NAT越え対応時はこの関数だけ差し替えるとTODO明記）。identityは既存Helloが`m_localPeerId`を申告する仕組みで自動維持。
- Hello処理の判定を「ロスターに存在するか」→「**接続中Peerが実際にそのIDを使っているか**(m_peerIds走査)」に修正。新Hostは旧Rosterを引き継ぐため、旧判定だと再接続Clientの前回IDが必ず「使用中」誤判定され再採番されてしまう。あわせて既存エントリ上書き時にcpuScoreを保持。

**Phase 3: ロール切替のLuauインターフェース**
- `NetworkManager`: `changeRole()`で`m_role`変更を一元化し`onRoleChanged`コールバック発火。`roleToString()`。
- `System.hpp/cpp`: `NetworkRoleChanged`シグナル（Heartbeatと同型）。
- `LuauEngine.hpp/cpp`: `fireNetworkRoleChanged(old,new)`（fireHeartbeatと同型、文字列2引数）。
- `LuauEngine_Dispatch.cpp`: `System.NetworkRoleChanged`（シグナル）/ `System.NetworkRole`（"Offline"/"Host"/"Client"）/ `System.LocalPeerId`（読み取り専用getter）。
- `game_main.cpp`: onRoleChanged→fireNetworkRoleChanged配線。クリーンアップのshutdown直後に`onRoleChanged = nullptr`（プロセス終了時のsingletonデストラクタが破棄済みluauEngineへ触れない防御）。
- Script実行系は**追加改修なし**: LuauEngineのフィルタは毎フレーム再評価で、Client時代はScript未実行（Completed=false）のため、Host昇格の次フレームから自然に最初から実行される。

**検証**
- 各フェーズ後ビルド成功。実装はCLAUDE.mdの役割分担どおり全フェーズimplementerに委譲し、メインセッションがgit diffで設計と照合レビュー。
- ライブ検証: 一時startup.yaml+void.yaml改変シーン（UseNetwork:true+Script+LocalScript）で`RecubinEngine.exe --host 7777`+Client×2を起動。3者で同一Roster（id/score/latency）共有→Host kill→約30秒でENetタイムアウト検知→両Clientが同じ勝者(id=2)を選出→勝者がClient→Host昇格（LuauのNetworkRoleChanged発火、Scriptが新Host上で実行開始）→他方が再接続し**同じid=3を維持**→チャット疎通・新Roster配布まで全チェーンをログで確認。検証後、一時ファイル（startup.yaml/net_migration_test.yaml/net_migration_test_*.luau）は削除済み。
- readme.md: 親項目と3サブ項目を[x]化（LAN/localhost前提の注記付き）。

### なぜそうしたか

- **CPUスコア=起動時マイクロベンチ**（vs FPS移動平均/合成）: AskUserQuestionでユーザー選択。安定していて選出結果が揺れない。レイテンシはENetが`peer->roundTripTime`で自動計測するため実装不要。
- **移行後は新Hostのローカル世界が正**: ユーザー選択。ワールドレプリケーション未実装のため唯一の現実解。真のシームレス化はレプリケーション実装後。
- **NAT越えは「設計の継ぎ目のみ」**: ユーザーは「考慮したい」を選択。実装はLAN前提のまま、住所を`PeerEndpoint`に一元化し接続経路を`connectToEndpoint()`1関数に集約、TODOコメントで将来のSTUN/リレー差し替え点を明示した。
- **選出を決定的アルゴリズムにした**（vs 投票/合意プロトコル）: Rosterは常にHostから全員へ同一内容が配布されるため、各自がローカルで同じ計算をすれば必ず同じ勝者になる。ハンドシェイクは「勝者の昇格＋敗者の再接続リトライ」だけで済み、メッセージ種別の追加が不要。
- **PeerId維持をHello再申告方式にした**: 再接続時のHelloに前回IDを載せ、新Host側が「接続中Peerで実使用中でなければ」そのまま採用。専用の再識別メッセージを増やさず基盤tierの既存フローに乗せた。

### どういう経緯か

1. Plan modeで開始。progress.md/doc/archive.mdの基盤tier記録（2026-07-09）を読み、Exploreエージェント1体でgame_main統合・Scriptライフサイクル・User構造・Luauシグナルパターンを調査。
2. AskUserQuestionでスコープ（3項目すべて）/CPUスコア計測法/移行後の世界の扱い/NAT越えの4点を確認して計画承認。
3. Phase 1→2→3の順にimplementerへ委譲。Phase 1レビュー時に「Hello のID使用中判定がホスト移行の再接続と衝突する」問題を発見し、Phase 2の指示に修正を織り込んだ。
4. 回帰テストで130 passed/**4 failed**となりベースライン(3 failed)超過を検知→切り分けの結果、4件目はコミットa92bb9a（2026-07-19、User character programmable）で追加された`CharacterChanger.luau`が`User.Character`に代入するがヘッドレスRecubinTestには`User`グローバルが無いことによる**既存問題**と特定（ネットワーク変更とは無関係）。メモリのベースライン記録を130/4に更新。
5. ライブ検証で全チェーン成功を確認し、readme.md更新。

### 試して失敗した方法

- **git stashしてrun_regression再実行によるベースライン比較**: run_regression.pyはビルド済みバイナリを実行するだけでリビルドしないため、ソースをstashしても結果は変わらず比較にならない（すぐ気づいてpopで復旧）。正しくは「stash→リビルド→実行」か、今回のように失敗内容そのものからの切り分け。

### 未解決・保留

- **ENetのHost死亡検知は約30秒かかる**（プロセスkill時はタイムアウト頼み。既定のENET_PEER_TIMEOUT_MAXIMUM=30秒）。体感を縮めたければ`enet_peer_timeout()`で各Peerのタイムアウトを短縮する余地あり（未実装）。検知時刻がピア間で大きくずれると、敗者の10秒タイムアウトが勝者の昇格前に切れるリスクが理論上ある（localhostでは同時検知のため未発生）。
- **NetworkEvent（Luau向けRemoteEvent相当）は未着手**。`SignalEvent`（Fired+Fireクロージャ、LuauEngine_Dispatch.cpp:435-436）が雛形になることは調査済み。
- **ワールドレプリケーション・リモートPeerのUser/キャラクター表現は未実装**（User::s_instanceのsingleton前提が障害になる点はExplore調査で判明済み）。
- **NAT越えは未実装**（PeerEndpoint/connectToEndpointが差し替え点）。
- レイテンシのRoster配布が26ms⇔27msの揺れで毎秒dirty扱いになりがち（1ms閾値）。実害はないがログが多い。気になるなら閾値を数ms〜に緩める。
- pathfinder.yamlの`CharacterChanger`回帰失敗（`User`グローバル不在）はユーザーのコミット由来のため未修正のまま。ヘッドレスでも`User`を用意するかテスト側で除外するかは次回判断。

### 暗黙仕様の発見

- **RecubinTest（ヘッドレス）にはLuauの`User`グローバルが存在しない**。`User`に触るスクリプトを含むシーンをFIXED_SCENESに入れると必ず失敗する。
- **enet_host_serviceのイベントループ中にENetHostを破棄してはならない**（whileループ条件が破棄済みポインタに触れる）。ロール切替のようなhost再構築はイベントハンドラでフラグだけ立てて、ループ脱出後に行うこと。
- **ClientのRosterはHost切断後もshutdownするまで残る**ため、切断直後の選出に「最後に受信したRoster」をそのまま使える（今回の決定的選出はこの性質に依存している。public connect()はRosterをクリアするが、移行用connectToEndpoint()はクリアしない、という非対称が意図的に存在する）。

---

## 2026-07-19 Packagerのアセット収集漏れ修正（MeshFile/IconPath/Terrain DataPath）＋ダイアログ相対化

### 何をしたか

パッケージ出力したゲームで .glbモデルとAppImageアイコンが開発環境の絶対パス（`C:\Users\Ryarta\Documents\Recubin\assets\...`）のまま参照され、ランタイムのAssetGuardにout-of-rootブロックされるバグを修正した。

- `Packager.cpp`:
  - `collectPaths`/`rewritePaths` の対象キー（従来 `ContentPath`/`Texture`/`FacePath`/`SkyboxPaths` の4種のみ）に **`MeshFile` と `IconPath` を追加**。これが直接原因（.glbとアイコンが収集もパス書き換えもされていなかった）。
  - `assetSubdir` に `.glb`/`.gltf` → `assets/models` の振り分けを追加、`package()` のサブディレクトリ作成リストにも `assets/models` を追加。
  - **Terrain の `DataPath`（チャンク保存先ディレクトリ、既定 "terrain"）の同梱処理を新設**。ファイル用copyFile経路とは別に `fs::copy(recursive)` でディレクトリごとコピー。相対パスはそのままの位置（YAML書き換え不要）、絶対パスは `terrain/<dirname>` へ入れて `DataPath` を書き換え。ディレクトリ未存在（未編集地形）は警告のみでスキップ。
- `PropertiesPanel.cpp`: `toProjectRelative()` ヘルパーを新設し、**MeshFile / IconPath のファイル選択ダイアログの2箇所**で、選択ファイルがプロジェクト（カレントディレクトリ）内なら相対パスに変換して格納するようにした（再発防止）。他のダイアログ箇所（Texture/Sound/Skybox等）はPackager対応済みキーのため触っていない。

検証: ビルド成功、回帰テスト 130 passed / 4 failed（既知4件のみ、新規回帰なし）。実装はimplementerに委譲し、メインセッションがgit diffで設計と照合。

### なぜそうしたか

- **AssetGuard側は触らない**: サンドボックス（実行フォルダ配下のみ許可）は正しい動作。緩めても他のPCには開発フォルダが存在せず配布物として壊れたままなので、「Packagerがコピーして相対パスへ書き換える」のが本筋。ユーザーから「親ディレクトリ配下にアクセスできれば安全では」という質問があり、この理屈（サンドボックスの問題ではなくパッケージの自己完結性の問題）を説明して合意した。
- **DataPathを専用経路にした理由**: DataPathはファイルでなくディレクトリ（TerrainStreamerのチャンク保存先）。既存の copyFile 経路は `fs::copy_file` でディレクトリに失敗するため、収集段階から `dirPaths` に分離した。
- **ダイアログ相対化はMeshFile/IconPathの2箇所に限定**: スコープ厳守。他のパス系ダイアログはPackagerホワイトリスト対応済みで実害がないため今回は見送り（`toProjectRelative` は将来そのまま適用可能）。

### どういう経緯か

1. ユーザーがパッケージ（Desktop\NetworkTest）をテストした際のログで `Blocked out-of-root asset path: ...hair-Curuata.glb / GirlTorso.glb / newHead.glb / the-cat.png` を報告。
2. Plan modeでExplore調査 → Packagerのキーホワイトリストに `MeshFile`（SceneLoader.cpp が直接出力）と `IconPath`（AppImageのPropertyRegistry登録キー）が無いことを特定。相対パスの `hooo.png`（Texture）や `.luauc`（ContentPath=FileRef経由）が正常な理由もホワイトリストの内外で説明がついた。
3. 絶対パスの出所はファイル選択ダイアログ（WindowsPlatform.cppの `SIGDN_FILESYSPATH` は常にフルパス）を無加工格納していたことと特定。
4. AskUserQuestionでスコープ確認。Terrain DataPath同梱は「今回一緒に直す」、ダイアログ相対化も「入れる」をユーザーが選択。

### 試して失敗した方法

- 特になし。

### 未解決・保留

- **実パッケージでの動作確認はユーザーに委ねた**。確認観点: 再パッケージ後の出力YAMLで `MeshFile` が `assets/models/...` 相対になっていること / `assets/models/*.glb` がコピーされていること / 実行時にAssetGuardのBlocked警告が消えること / エディターで.glbを選び直すとYAMLに相対パスが入ること。
- **絶対パス由来アセットのファイル名衝突**（別ディレクトリの同名ファイルが `assets/xxx/<filename>` へフラット配置されて潰し合う）は既存挙動のまま未対応。
- 既存シーンYAML内の絶対パス（void.yamlのStarterCharacter等）はそのまま。Packagerが処理するため実害はないが、エディターで選び直せば相対化される。

### 暗黙仕様の発見

- **Packagerのアセット追跡は「YAMLキーのホワイトリスト」方式**（`collectPaths`/`rewritePaths`）。新しいパス保持プロパティ（クラス）を追加したら、このホワイトリストへの追加を忘れるとパッケージで必ず壊れる。FileRef の `ContentPath` に乗せればこの問題を回避できる（FileRef.hppのコメントどおり「Packagerが追跡するキー」）。
- **Terrain::DataPath はファイルではなくディレクトリ**。パス系プロパティでも一律にファイル扱いできない例。
- **AssetGuardはランタイム（game_main.cpp）のみ有効でエディターでは無効**。そのため開発中は絶対パス参照でも動いてしまい、パッケージして初めて顕在化する（今回のバグが長く気づかれなかった理由）。

---

## 2026-07-19 ランタイム黒画面（クリアカラーのみ）修正 — renderViewportのNullEditorManager回帰

### 何をしたか

パッケージ出力したゲーム（RecubinEngine.exe）でウィンドウがクリアカラーのみになり3Dシーンが一切描画されないバグを修正した。

- `IEditorManager.hpp`: 純粋仮想 `ownsSceneRender()` を追加（「renderUI内でシーンを描くか」）。
- `EditorManager.hpp/cpp`: `ownsSceneRender() → true`（ViewportPanelがシーンを描く）。
- `NullEditorManager.hpp`: `ownsSceneRender() → false`（GUIしか描かない）。
- `Renderer.cpp` `render()`: シーン描画ゲートを `if (!editor)` → `if (!editor || !editor->ownsSceneRender())` に変更し、コメントを実態に合わせて更新。

検証: ビルド成功、回帰テスト 130 passed / 4 failed（既知4件のみ）。エディター側はゲート条件が従来と同値（実エディターで引き続きスキップ）のため描画経路に変化なし。実機の見た目確認はユーザーに委ねた。

### なぜそうしたか

- **原因**: コミット d0e6321「Better Rendering Performance!!」が、エディターでの二重描画回避のため renderViewport を `if (!editor)` に閉じ込めた。この「editorが居る=ViewportPanelがシーンを描く」という仮定は、ランタイムが使う NullEditorManager（非nullだがシーンを描かない）に当てはまらず、ランタイムだけシーン描画が丸ごと消えていた。物理・アセット・スポーンのログは正常なのに画面だけ黒、という症状はこれで完全に説明がつく。
- **仮想メソッド方式にした理由**（vs NullEditorManager::renderUIでrenderViewportを呼ぶ / dynamic_cast判定）: 「シーンを誰が描くか」はEditorManagerの性質なので、インターフェースに意味のあるフラグとして持たせるのが最小かつ明示的。GUIクラスがシーン描画を呼ぶ構造や型判定のハックを避けた。
- ユーザーの初期仮説（カメラ座標飛び/ネットワーク同期未実装/アセット）はいずれも原因ではないと調査で棄却（renderViewport自体が未到達のため）。前セッションのPackager修正は成功しており、パッケージ内のYAML/アセットは正常だったことも確認済み。

### どういう経緯か

1. ユーザーがパッケージ（Desktop\NetworkTest）を再テストし「ウィンドウがクリアカラーのみ」と報告。readmeにTodo追加（カメラ？同期未実装？アセットエラー無し）。
2. Plan modeでまずパッケージ実物を読み取り検査 → assets/models/*.glb同梱・YAMLパス書き換え・Lighting/Workspace/User存在をすべて確認し、パッケージ起因を棄却。
3. Exploreエージェントでランタイム描画経路を調査 → `Renderer::render()` のゲートと NullEditorManager が非null であることの矛盾を特定。`git log -S "if (!editor)"` で回帰コミット d0e6321 を特定。
4. implementerに委譲して修正、ビルド＋回帰確認。

### 試して失敗した方法

- 特になし（一発特定）。

### 未解決・保留

- **実機確認はユーザーに委ねた**。観点: Desktop\NetworkTest の RecubinEngine.exe を build/Release の新ビルドに差し替え（または再パッケージ）→ シーンが描画されること / ゲームGUI・ProximityPromptが従来どおり重なること / エディターの見た目に変化がないこと。
- readme.md の「画面が真っ黒」Todoのチェック更新はユーザー確認後。
- **次タスク（readme Todo後段）: オブジェクト座標のネットワーク同期（レプリケーション）**。現状マルチプレイは接続・ロスター・ホスト移行基盤のみで、ワールド状態は同期されない。
- ウィンドウリサイズコールバックがランタイムに無い（毎フレームglfwGetFramebufferSizeで追従するため実害は小さいが、調査中に気づいた点）。

### 暗黙仕様の発見

- **ランタイムの editor は nullptr ではなく NullEditorManager 実体**（game_main.cpp:153）。`if (!editor)` による「エディター有無」判定はランタイム検出として機能しない。エディター/ランタイムの挙動分岐は IEditorManager の仮想メソッド（今回の ownsSceneRender のような性質フラグ）で表現すること。
- **RecubinTest はこの黒画面を検出できない**（ヘッドレスでスクリプト結果のみ検証し、画面ピクセルは見ない）。描画の生死は回帰テストの死角。

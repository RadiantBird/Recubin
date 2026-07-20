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

## 2026-07-19 ワールドレプリケーション実装（アバター同期＋ワールドオブジェクト同期）

### 何をしたか

readme Todo「オブジェクトの座標を同期したりする機能」を実装。マルチプレイで相互のアバターが見え、ホストの物理結果が全クライアントに反映されるようになった。黒画面修正はユーザー実機確認済み → readmeチェック更新。

- `include/Network/ByteStream.hpp`（新規）: NetworkManager.cpp無名namespaceのByteWriter/ByteReaderをヘッダへ抽出＋writeVector3/writeQuat/readVector3/readQuat追加
- `include/Network/NetworkTypes.hpp`: MessageType追加（AvatarState=6/AvatarBatch=7/WorldMapping=8/WorldTransforms=9）。DummyPosition=1は撤去（欠番コメント）
- `include/Network/NetworkManager.hpp` / `src/Network/NetworkManager.cpp`: sendDummyPosition撤去。`sendBytes(payload, channel)`（生ペイロード全ピアbroadcast）と `onGameMessage` コールバック（AvatarState〜WorldTransformsをゲーム層へ委譲。Host視点はm_peerIdsでsenderId解決、Hello前の未登録ピアは無視）を追加
- `include/Core/User.hpp` / `src/Core/User.cpp`: spawnCharacterのリグ構築部を `static User::buildCharacterModel(searchRoot, name)` に抽出（挙動不変。spawnCharacterとリモートアバター生成が共用）
- `include/Network/Replication.hpp` / `src/Network/Replication.cpp`（新規）: ReplicationManager本体
  - アバター同期: 自キャラRootのワールドcframeを20Hz送信（Client→AvatarState→Host、HostがAvatarBatchで全員分配布）。ロスターと突き合わせて RemotePlayer_<peerId> を生成/破棄（buildCharacterModel→Humanoid削除→全パーツCanCollide=false→Workspaceへ。Root相対オフセットで剛体一括追従）
  - ワールド同期: Hostが1秒ごとにWorkspace走査（!Anchored && CanCollide、自キャラとRemotePlayer_*サブツリー除外）しパス→netId永続採番。変化時/新規接続時にWorldMapping(RELIABLE)配布。cframeは移動検出＋停止後0.5sテール＋5秒ごと全量スナップショットで20Hz・36件/パケット分割のWorldTransforms(UNRELIABLE)。クライアントは対象をsetAnchored(true)でキネマティック化し受信cframeを平滑適用（指数補間 α=1-exp(-15dt)、初回スナップ）。**送受信はローカルcframeそのまま**（階層同一前提、ワールド変換なし）
  - ホスト昇格時: clientReleaseWorldObjects()で全対象をAnchored=false復元→自世界を権威として再スキャン・再配布。Offline時も復元＋全クリア
- `src/game_main.cpp`: DummyPositionモックブロック削除。ReplicationManager生成・onGameMessage/onRoleChanged配線・毎フレームupdate（NetworkManager::update後、物理前）・クリーンアップでonGameMessage=nullptr
- `readme.md`: 黒画面Todo解決記録、レプリケーション達成項目を追加、ホスト移行項の「ワールドレプリケーション未対応」注記を更新

### なぜそうしたか

- **ReplicationManagerを新設しNetworkManagerと分離**: NetworkManagerはトランスポート（バイト列）に徹し、Instance/Workspaceを知る層を隔離。将来のNetworkEventも onGameMessage/sendBytes に載せられる
- **クライアント側はAnchored化して受信cframe書き込み**（vs ダイナミックのままteleportTo）: syncPhysics()が非Anchoredでは毎フレーム物理結果でcframeを潰す（BaseCube.cpp:187-200）ため上書き同期は競合する。Anchored=trueならcframe→setKinematicTargetの追従で読み戻しが無い。既存機構だけで実現でき、昇格時はsetAnchored(false)に戻すだけで復元できる
- **オブジェクトIDはパスベース**（getWorkspaceRelativePath）: インスタンスに一意IDが無く、兄弟間の名前一意保証（uniqueChildName）によりパスがツリー内一意。同一シーンをロードする前提ではこれで十分。UNRELIABLE転送はnetId(u32)に圧縮しパスはRELIABLEのマッピング表で一度だけ送る
- **リモートアバターはCanCollide=false（衝突なし）**: ユーザー決定。アクター自体が作られず純視覚。帰結としてクライアントはホスト権威オブジェクトを押せない（既知の制限）
- **リモートアバターのHumanoidは削除**: 誰も駆動せず（applyBodyAnimationはUserが自分にのみ呼ぶ）、Rootが動的アクターだと落下するだけ。パーツはRoot相対オフセットの剛体一括追従（歩行アニメはスコープ外）
- ユーザー決定: スコープ=キャラ＋ワールド同期の両方 / 衝突なし / 新ホストのローカル世界が権威（前回決定踏襲）

### どういう経緯か

1. ユーザーが黒画面修正を実機確認 → 「次はオブジェクト同期まで計画」
2. Plan mode: Explore 3並行（ネットワーク層/オブジェクト識別・座標制御/キャラ生成）→ CFrame構成・CMake GLOB・getRoster既存をgrepで確定 → 設計を自分で確定しAskUserQuestionでスコープ・衝突を確認 → プラン承認
3. implementerに4分割で委譲（トランスポート層→リグ抽出→アバター同期→ワールド同期）、各回git diffレビュー
4. ステップ4で事故: implementerが自分で実装せずバックグラウンドに別エージェントを生んで終了 → SendMessageで再開指示 → 二重エージェントが同一ファイルに重複実装を書き関数二重定義に → 再開側が重複を除去して単一実装に統一（最終ファイルは全文レビューで整合確認）
5. スナップショットのforceAllフラグがタイマーずれで失われうる点だけメイン側で微修正（m_worldSnapshotPending化）
6. 検証: ビルド成功、回帰130/4維持。localhostライブ検証（一時startup.yaml+net_repl_test.yaml、Host+Client×2、ログリダイレクト）: マッピング2件配布/適用(unresolved 0)・全ピアでRemotePlayer生成・姿勢受信を確認。Host kill → id=3昇格「released 2 world objects」→再スキャン→新マッピング配布、生存クライアントは再接続して再適用。ウィンドウクローズ起因の2段目移行（id=2昇格）も正常。一時ファイルは削除済み

### 試して失敗した方法

- implementerへの委譲が1回「別エージェント起動して自分は何もしない」で空振り（報告文だけ成功風）。**成果物はgit statusとシンボルgrepで実在確認してから受け入れること**。二重実行時は重複定義ビルドエラーの掃除が必要になった

### 未解決・保留

- **実機の見た目確認はユーザーに依頼**: 2インスタンス起動（--host / --connect 127.0.0.1）で相互アバター表示・落下Cubeの同期・ホスト終了後の引き継ぎを目視確認
- 歩行アニメーション同期なし（リモートアバターは剛体ポーズで滑る）。次の改善候補
- クライアントは世界に物理干渉できない（衝突なしの帰結）。入力転送 or オブジェクト所有権移譲が将来テーマ
- ホストScriptがInstance.new等で動的生成したオブジェクトはクライアント側でパス解決不能（WARNしてスキップ）。生成/削除の複製は未対応
- リネーム/再parentされた同期対象はパスが変わり remove+add 扱い（クライアント側は解決不能になりうる）
- 非アクティブWorkspaceは同期対象外。NetworkEvent（Luau公開）は引き続き保留
- ENetの切断検知は依然タイムアウト約30秒（enet_peer_timeout短縮は未着手）

### 暗黙仕様の発見

- **StarterCharacterのクローンにScript/LocalScriptが含まれるとリモートアバターにも複製される**（今回はHumanoidのみ削除）。StarterCharacterにスクリプトを入れる運用を始める場合は要注意
- Model子パーツのcframeはローカルで、syncPhysicsはローカル整合を保つ（2026-07-18修正の恩恵）→ ローカルcframeをそのまま送受信すれば階層同一前提で変換不要
- ByteWriter/ByteReaderはByteStream.hppへ移動済み。今後の新メッセージはこれを使う（NetworkManager.cpp内無名namespaceにはもう無い）

## 2026-07-20 リモートUserのSystem.Users化 ＋ 非利き手が上を向くバグ修正

### 何をしたか

- `src/Instances/Humanoid.cpp` の `Humanoid::computePose()`: 片手ツール装備時、非利き手の腕角度に誤用されていた `180.0f`（本来は非接地時バタつきポーズ用）を `swing`/`-swing`（接地時分岐と同じ歩行スイング）に変更。
- `include/Core/User.hpp` / `src/Core/User.cpp`: `currentTool`からleft/rightのarm-raised状態を算出するロジックを `getToolArmRaiseState()` に一元化。`processMovement()` のFree/Program/無フォーカス時3箇所（従来 `applyBodyAnimation(false,false)` 決め打ちだった）と `processCharacterMovement()` の重複ロジックをこれに統一。`User.cpp:251` のTODOコメントを解消。
- `include/Core/NullInputBackend.hpp` / `src/Core/NullInputBackend.cpp`（新規）: 何も入力を供給しない `IInputBackend` 実装。
- `User`クラス: `peerId`メンバ、`isRemoteUser`フラグ、`createRemoteUser(peerId)` staticファクトリを追加。コンストラクタに `isRemoteUser=false` のデフォルト引数を追加（既存呼び出し元は無修正）。`s_instance`の書き換え（コンストラクタ・デストラクタ両方）を `isRemoteUser`/`s_instance==this` でガードし、複数User生成時にローカルシングルトンが壊れないようにした。
- `src/Core/LuauEngine_Dispatch.cpp`: `User.PeerId`（読み取り専用）プロパティを追加。
- `include/Network/Replication.hpp` / `src/Network/Replication.cpp`: `RemoteAvatar`構造体に `identity`（`shared_ptr<User>`）を追加。`spawnRemoteAvatar`でリモートピアごとに`User::createRemoteUser()`を呼び、`character`を既存の`RemotePlayer_<id>` Modelに紐付けたうえで`System.Users`配下に追加。`despawnRemoteAvatar`で対応するUserもツリーから除去。
- `src/game_main.cpp`: メインループでローカル`user->peerId`をネットワーク接続状態に応じて毎フレーム更新（Host即時1、Client はWelcome受信まで0、切断で0に戻る）。

### なぜそうしたか

- 「非利き手が上を向く」バグは調査の結果、真因が`Humanoid::computePose`（`User.cpp:251`のTODOとは別の場所）にあると判明。両方まとめて直すことで、TODOの「呼び出し箇所が多いから危ない」という懸念を、ロジックをヘルパー関数に一元化することで解消した（呼び出し箇所が多いこと自体は変わらないが、重複コードでなく1箇所の実装を共有するので保守上安全）。
- リモートUserは「`RemotePlayer_<id>` Modelを丸ごとUser化」ではなく、「既存のModelはそのまま、Userをidentityとして追加で被せる」設計にした。ローカルUser（`System.Users`配下のidentity）とCharacter（`Workspace`配下の見た目/物理）が元々ツリー上別々という既存パターンをそのまま踏襲でき、既存のアバター同期ロジック（CanCollide剥奪・Humanoid除去・パーツ追従）を一切変更せずに済むため。
- `User`のコンストラクタに`bool isRemoteUser`引数を追加する案を採用（新クラスへの分離はしなかった）。理由: `User::clone()`が明示的に未サポート（`IInputBackend`がコピー不能なため）で複製経路が使えず、かつ`s_instance`シングルトンの書き換えタイミングがリスクの本質だったため、最小限のガード追加で解決できると判断。`LocalPlayerController`的な分離リファクタは今回のスコープ外（過剰設計と判断）。
- ローカル自分自身の`User.PeerId`も設定するかはユーザーに確認し、「設定する」を選択。role-changedイベント1回のフックだとClient側のPeerId確定（Welcome受信）タイミングを取り違えるため、毎フレーム同期する方式（1行）を採用。

### どういう経緯か

1. ユーザーから「ネットワーク接続してきたピアはUsersの中にUserとして追加されているか」という質問を受け、調査の結果「されていない（RemotePlayer_<id>という別のModelのみ）」と回答。
2. readme.mdの既存Todo「リモートユーザーを実際にSystem.UsersにUserとして生成...」と、User.cpp:251のTODO（非利き手バグ）の2件をまとめて設計・実装するようユーザーから指示。
3. Plan Modeで2つのExploreエージェント（バグ調査／Userシングルトン・Usersコンテナ調査）を並列実行し、根本原因とアーキテクチャ制約を洗い出してから計画を作成。
4. ローカルUserのPeerId設定要否をAskUserQuestionでユーザーに確認（設定する、を選択）。
5. Humanoid.cpp+User.cpp TODO解消をStep1としてimplementerに委譲・レビュー。続けてリモートUser一式（NullInputBackend新規/User拡張/Dispatch追加/Replication配線/game_main.cpp）をStep2として1つのimplementerに一括委譲・レビュー。両ステップともビルド成功、回帰テスト130 passed/4 failed（既知ベースライン）維持を確認。

### 未解決・保留

- 見た目確認（ツール片手装備時に非利き手が自然に振れる/下がること、`System.Users:GetChildren()`でリモートUserが見える・`.PeerId`が正しいこと）はユーザー側での実機確認待ち。
- リモートUserの`Inventory`は空Folderのみ生成し、ホットバー/ツール所持状態自体はレプリケートしていない（今回のスコープ外）。将来ツール所持状態も同期したい場合は別途設計が必要。
- readme.mdの他のネットワーク関連Todo（予測移動+ホスト権威検証、リモートアバターのアニメーション同期、workspace全体の生成/削除監視、チャットGUI化）は未着手のまま。

### 暗黙仕様の発見

- `User`のコンストラクタ/デストラクタは無条件で`s_instance`を書き換えていた（ガードなし）。単一User前提のコードだったため今まで問題化しなかったが、複数User生成時は生成/破棄順序次第でローカルシングルトンが破壊される潜在バグだった。
- `Humanoid::computePose`の「非接地時バタつきポーズ用の180度」が片手ツール分岐の非利き手にも誤用されていた。`makeArm()`のpivot構造上、180度は「半回転で上を向く」値であり、0度（自然に下垂）とは真逆の意味を持つ。今後同様の角度定数を追加する際はpivotの回転中心（`makeArm`のpivotOffset）を踏まえて意味を確認すること。
- `User.Character`（Model）は`User`自身の子ではなく`Workspace`側に別途addChildされる設計（`spawnCharacter`内コメントに明記）。`User`（identity）と`Character`（見た目/物理）はツリー上別々というパターンは、ローカル/リモート問わず一貫していた。

## 2026-07-20 クライアント予測移動＋ホスト権威検証（物理干渉の有効化）

### 何をしたか

- `include/Network/NetworkTypes.hpp`: `AvatarState`メッセージ(値6は不変)のコメントを「結果姿勢」から「移動入力スナップショット」に変更(意味の再定義。ワイヤ上の値・チャンネル・頻度は不変)。
- `include/Core/User.hpp` / `src/Core/User.cpp`: `MovementInput`構造体(`flatForward`/`flatRight`/`targetMoveDir`/`isPressingMove`/`ctrlLockEnabled`/`forwardAxis`/`rightAxis`)と`lastMovementInput`メンバを追加。`processCharacterMovement()`が既存ロジックで計算した値をそのままここに保存するだけ(移動計算自体は無変更)。
- `include/Network/Replication.hpp` / `src/Network/Replication.cpp`: `ReplicationManager::update()`のシグネチャに`Physics*`を追加(Host側で`Humanoid::move()`を代理実行するために必須)。`RemoteAvatar`に`humanoid`(保持したまま、以前は`removeChild`で破棄していた)と`isPhysicsProxy`フラグを追加。新規`enablePhysicsProxy()`: RootだけCanCollide=true/Anchored=falseにして`Physics::recreateActor()`で実体化し、ローカルUserのRoot＋既存の他プロキシRootとの間に`NoCollision`インスタンス(既存の制約Instance、Weld/Rope同様の仕組み)を生成してPvP衝突を除外。新規`hostSimulateAvatars()`: 各ピアの最新入力(`m_pendingAvatarInput`)を長さ1超過なら正規化・軸値を[-1,1]にクランプしてから`Humanoid::move()`を代理実行し、結果を`m_latestPoses`に反映(既存のAvatarBatch配布へそのまま合流)。`applyAvatarPoses()`は`isPhysicsProxy`なアバターをスキップ(`Humanoid::move()`内の`applyBodyAnimation()`が既に全パーツを正しいFKで駆動済みのため、二重書き込みを避ける)。Client側は新規`reconcileLocalPose()`: Host権威姿勢とローカル予測の差が1.5studを超えたら`BaseCube::teleportTo()`でスナップ補正(回転は対象外、位置のみ)。ホスト移行(Client→Host昇格)時は`m_pendingProxyUpgradeAll`フラグを立て、次の`update()`で既存の全リモートアバターを一括してプロキシ化する。
- `src/game_main.cpp`: `replication.update(deltaTime, workspace->getPhysicsEngine());`への1行変更のみ。

### なぜそうしたか

- 「クライアントはワールドオブジェクトを押せない」問題の根本原因は、Host権威のワールドオブジェクトがクライアント側で`setAnchored(true)`(キネマティック化)されており、キネマティックはダイナミックActorを押しのけるが逆に押されないというPhysXの仕様だった。解決には「クライアントのキャラクターがHostの物理シミュレーション上で本物のダイナミックActorとして存在する」ことが必須と判断し、Host側に各リモートピア用の物理プロキシ(実体Actor)を持たせる設計にした。
- 新規クラスを作らず、既存の`RemotePlayer_<id>` Model(見た目用に既にある)にHumanoidを温存し、Rootだけを物理化する方式を選んだ。理由: 既存のアバター同期(パーツ追従・CanCollide=false化・spawnロジック)を大きく作り替えずに済み、`Humanoid::move()`が持つSeat/Truss/接地判定等の既存ロジックをHost側でもそのまま再利用できるため(車輪の再発明を避けた)。
- プレイヤー同士の衝突除外には新規のグループ/マスク機構を作らず、既存の`NoCollision`Instance(Weld/Rope同様のペア単位制約)をそのまま流用した。既に動作実績があり、Workspace配下に追加するだけで自動登録・自動クリーンアップされるため。
- 「妥当性チェック」は、個別の異常値検知ロジック(速度上限・テレポート検知等)を新設するのではなく、「Hostが常に自前の物理定数(WalkSpeed等)で入力から結果を計算し、クライアントの自己申告位置を一切信用しない」構造そのものによって速度改鼠・テレポート・すり抜け系のチートを構造的に防ぐ設計にした。入力側は`targetMoveDir`等の長さ1超過を正規化、軸値を[-1,1]にクランプするだけに留めた(過剰な検知ロジックを追加しない、との判断)。
- ユーザーへの確認: 今回のスコープを「フルスコープ(物理干渉+異常値クランプ)」「プレイヤー同士の衝突は含めない」に決定(AskUserQuestion)。ローカル自分自身の`PeerId`もあわせて設定することを決定(前セッションの続きの機能)。

### どういう経緯か

1. 前タスク(リモートUserのSystem.Users化 + 非利き手バグ)完了後、ユーザーがreadme.mdの次のTodo「クライアントが移動する場合、まず予測移動し、ホストが演算し、妥当性チェック。これにより物理干渉を可能にする」を選択。
2. Plan Modeで2つのExploreエージェント(物理エンジン/PhysX Anchored・CanCollide挙動とHumanoid::move()の内部実装、ネットワークメッセージプロトコルとRTT計測)を並列実行し、さらに自分でPhysXのCollision Filtering機構(`NoCollision`Instance、`Physics::recreateActor`)を直接コード読解で調査。
3. AskUserQuestionで「フルスコープか物理干渉のみか」「PvP衝突を含めるか」を確認。
4. 3ステップに分割してimplementerに委譲: (1)配線土台(型・シグネチャ変更のみ)、(2)Host側物理プロキシ本体、(3)Client側reconciliation。各ステップ後にgit diffを設計と照合してレビュー。
5. ビルド・回帰テスト(130 passed/4 failed維持)は全ステップで成功したが、**localhost 2プロセスでの実地ログ検証で重大な不具合を発見**: `MovementInput`/`AvatarInputWire`構造体の`Vector3`フィールド(`flatForward`/`flatRight`/`targetMoveDir`)に明示的な初期化子を書いていなかったため、`Vector3`がコンストラクタを持たない集合体(`float x,y,z;`のみ)であることと相まって、入力未受信時にゴミ値(未初期化スタック/ヒープメモリ)のまま`Humanoid::move()`に渡っていた。これによりHost側プロキシが際限なく漂流し、Client側の`reconcileLocalPose()`が1.5〜2.0stud程度のずれを検知し続けて毎フレーム近くスナップ補正を繰り返し、ついには`Physics.cpp`の「Spiral of Death」安全装置(フレーム時間超過時のフォールバック)まで誘発するほどの不安定化を起こしていた。
6. 該当2箇所(`User.hpp`の`MovementInput`、`Replication.hpp`の`AvatarInputWire`)の`Vector3`メンバに`{}`明示初期化を追加(6行程度の軽微な修正のためメインセッションが直接実施)。再ビルド・再回帰テスト後、localhost検証をやり直し、初期の物理settle起因の一時的な補正バースト(約1秒間に19回程度)の後にreconciliationが完全に収束・停止し、Spiral of Deathも再発しないことを確認。

### 未解決・保留

- Client側からHostが視ている「他ピアの物理プロキシ」の見た目(Torso/Head/Arms/Legs)は、Host machine上でのみ`Humanoid::move()`の正しいFKアニメーションで駆動される。ghost(Client視点でのリモートアバター)は従来通りRoot姿勢のrel-offset一括追従のままで、`applyBodyAnimation`由来の自然な歩行アニメーションは今回もリモート視点には反映されない(readme.mdの別Todo「リモートアバターにもアニメーションを再生する」は引き続き未対応のまま)。
- ツール所持状態(`leftArmRaised`/`rightArmRaised`)はHost側の代理`move()`呼び出しで`false,false`固定。ツール同期は今回のスコープ外。
- プレイヤー同士(リモートアバター同士)の衝突は`NoCollision`で意図的に除外したまま(ユーザー確認済みのスコープ判断)。
- 見た目確認(実際にクライアントがワールドの箱を押せること、通常操作でreconciliationのガタつきが気にならないこと)はユーザーに依頼。今回のログ検証は「接続→スポーン→物理プロキシ化→NoCollision登録→reconciliation収束」の一連の流れがクラッシュ・スパイラルオブデス無く動作することの確認に留まる(実際のWASD入力による押し出しは自動テストでは検証していない)。

### 暗黙仕様の発見

- **`Vector3`はコンストラクタを持たない集合体(`struct Vector3 { float x, y, z; ... };`)であり、明示的な初期化子(`{}`や`= Vector3(0,0,0)`)を書かない限り、クラスメンバとしてのデフォルト構築時にゼロ初期化されない(ゴミ値になりうる)**。他の箇所で`Vector3`をメンバに持つ既存構造体は暗黙的に安全な文脈(スタック上での即代入、あるいは他の初期化経路)で使われていたため今まで顕在化しなかったが、「未受信時のデフォルト値」としてネットワーク越しの構造体に使う場合は要注意。今後同様の構造体を追加する際は必ず全フィールドに明示初期化子を書くこと。
- `Physics::recreateActor()`は、`CanCollide=false`で一度もActorを持ったことのないCubeに対しても(`Humanoid::enterRagdoll()`の前例通り)安全にActorを新規生成できる。`workspace.pendingInstances`経由の自動登録パスを待たずに、任意のタイミングで明示的にActor化できる汎用APIとして使える。
- `NoCollision`Instance(Weld/Rope同様の制約)はコード側から`std::make_shared<NoCollision>(cubeA, cubeB)`→`someParent->addChild(nc)`するだけで自動登録・自動クリーンアップされる。Luauスクリプトやシーンエディタ専用の機能ではなく、C++側からの動的な衝突制御にもそのまま使える汎用機構だった。

## 2026-07-20 入力リプレイ方式によるクライアント予測補正の改善

### 何をしたか

- `include/Core/Physics.hpp` / `src/Core/Physics.cpp`: `Physics::update(Workspace&, float dt)`から、Workspace非依存な部分(重力適用を除く、蓄積dtでのsimulate/fetchResultsループ+浮力/Force適用+遅延op処理)を`Physics::stepOnce(float dt)`に、末尾のActor姿勢読み戻しループを`Physics::syncAllCubes()`に切り出した。`update()`はこの2つを呼ぶよう再構成しただけで、処理順序・挙動は完全に不変(回帰テスト130 passed/4 failedで確認)。
- `include/Instances/Humanoid.hpp`: リプレイ専用の状態退避/復元用アクセサ(`getWalkCycle`/`setWalkCycle`/`getCurrentMoveDir`/`setCurrentMoveDir`)を追加。`move()`自体のロジック・シグネチャは無改修。
- `include/Network/Replication.hpp` / `src/Network/Replication.cpp`: `ReplicationManager`に「クライアント予測専用の分離PhysXシーン」を追加。
  - `m_predictionPhysics`(別`Physics`インスタンス、Client役割時のみ遅延生成)、`m_shadowRoot`(ローカルRootの複製、どのInstanceツリーにも属さない)、`m_predictionHumanoid`(Root=シャドウの軽量Humanoid)。
  - 静的ジオメトリ(`Anchored && CanCollide`)のみを約2秒ごとに差分ミラーリング(`rescanPredictionStaticGeometry`)。Seat/Truss/LiquidCube/Terrainはミラー対象外(スコープ外、既知の近似)。
  - 毎フレーム、シャドウRootを本物のRootへ強制同期(`syncPredictionShadowToLocal`)。
  - `AvatarState`メッセージにフレーム通し番号(`seq`)を追加。クライアントは毎フレーム(20Hz送信スロットルとは独立に)入力+Humanoid内部状態(`currentMoveDir`/`walkCycle`/`Rotation`)のスナップショットを`m_inputHistory`(`std::deque`)へ積む(`bufferLocalInput`)。
  - Host側は入力を消費するたびに`m_lastProcessedSeq[peerId]`を記録し、`AvatarBatch`の各エントリに含めて配布。
  - `reconcileLocalPose()`を全面書き換え: ズレが小さければ何もしない、極端に大きければ即座にハードスナップ(フォールバック)、それ以外は「シャドウをHost権威姿勢へ巻き戻す→ack以降の未確認入力を1件ずつ`m_predictionHumanoid->move()`+`m_predictionPhysics->stepOnce()`で再生→結果を本物のRootへ1回だけ反映」という入力リプレイを行う。

### なぜそうしたか

- 前回実装した「クライアント予測＋ホスト権威検証」は、ズレたら閾値超過で即座にteleportするだけの単純な補正だった。ユーザーが実機で動かした結果「ロールバック」「追い越し」に見えて気持ち悪いとの指摘を受け、改善方針を検討。滑らかなブレンド補正・簡易キネマティックリプレイ・分離予測シーンによる本格リプレイの3案を提示し、ユーザーは本格実装を選択。
- このエンジンは全アクターが単一のPhysXシーンに同居する構造のため、シーン全体を巻き戻すと他オブジェクトの物理が二重に進んでしまう。そのため「ローカルプレイヤー専用の複製Root+静的ジオメトリだけをミラーした分離シーン」でリプレイを行う設計にした。
- `Humanoid::move()`のアニメーション補間(`currentMoveDir`/`walkCycle`)がdtではなく呼び出し回数に固定係数をかける実装だったため、「1フレーム=1回のmove()呼び出し」を厳守してリプレイすれば`move()`自体は無改修で正しく動くと判断した(過去にリプレイの都合で移動ロジック自体を書き換えると、シングルプレイの挙動にも影響するリスクがあったため、move()には一切触れない設計にした)。

### どういう経緯か

1. ユーザーから「補正がロールバック/追い越しに見えて気持ち悪い、改善できるか」と相談を受け、原因(閾値超過での即座teleport)を説明した上で3案を提示。
2. 「入力リプレイ方式」を選択されたが、調査の結果「1シーン共有のため丸ごと巻き戻せない」という制約が判明したため、いったん立ち止まって「滑らかブレンド/簡易キネマティックリプレイ/本格分離シーン」の3択を再提示。ユーザーは制約を理解した上で「分離予測シーン(本格実装)」を選択。
3. Plan Modeで2つのExploreエージェント(Physicsシーンの構造・BaseCube/Workspaceの制約、メッセージプロトコルとHumanoid::move()の副作用)を並列実行し、`Physics::createNoCollision`/`Physics::recreateActor`等の既存APIがそのまま使えることや、`Humanoid::move()`の補間が呼び出し回数依存であることを確認してから設計。
4. 4ステップ(Physics::stepOnce切り出し→予測シーン基盤→入力履歴バッファ+プロトコル拡張→リプレイ本体)に分割してimplementerへ委譲。各ステップ後にgit diffレビュー、ビルド・回帰テストを実施。全ステップとも設計通りに実装され、回帰テスト(130 passed/4 failed)も維持された。
5. localhost 2プロセスでの実地ログ検証を実施。前回セッションで見つけた「未初期化Vector3によるSpiral of Death」は再発しないことを確認できたが、**新たに「reconciliationが継続的に(20Hzごとに)発火し続け、収束して止まる様子が見られない(drift 1.5〜3.4studの範囲で常に補正が走り続ける)」という挙動を観測した**。前回(input-replay導入前)の単純なteleport版では同じテストシーンで数秒以内に収束・完全停止していたため、リプレイ導入による挙動変化の可能性がある。ただし自動テストでは実際のWASD入力を送っていない(ヘッドレスウィンドウにフォーカスが無い可能性が高い)ため、「本当に静止した状態で収束しない不具合」なのか「たまたまウィンドウにフォーカスが移り実際に歩いていたことによる、latency分の正常な継続補正」なのかをログだけでは切り分けられなかった。クラッシュ・Spiral of Death・ビルドエラー・回帰テスト失敗は無い。

### 未解決・保留

- **上記の「reconciliationが止まらない」現象の原因切り分けがユーザーの実機確認待ち**。実際にWASD移動しながら、(a)静止時にreconciliationログが完全に止まるか、(b)移動中の補正が以前より滑らかに感じられるか、の2点を確認してほしい。もし静止時にも継続的に補正が発生している場合は、`reconcileLocalPose()`のリプレイ回転処理(`m_shadowRoot->cframe.Rotation`への代入がsyncPhysicsの読み戻しで即座に上書きされ実質無効化されている可能性がある箇所、`src/Network/Replication.cpp`の`reconcileLocalPose()`内)を最初に疑うこと。
- Seat/Truss/LiquidCube/Terrain上でのリプレイは近似(存在しないものとして計算)。大きくズレた場合は閾値(8stud)超過でハードスナップにフォールバックする设计のため機能上は破綻しないが、見た目の滑らかさはそれらの地形上では期待できない。
- ツール所持状態のリプレイは今回もスコープ外(Host代理move()同様`false,false`固定)。

### 暗黙仕様の発見

- `Spatial`(`BaseCube`の基底)の`Rotation`は`Quaternion& Rotation;`という`cframe.Rotation`への**参照**であり、独立した別ストレージではない(`include/Instances/Spatial.hpp:15`)。`someCube->cframe = x;`と`someCube->Rotation = x.Rotation;`はどちらか一方で十分。
- `Spatial::getWorldCFrame()`は親が無ければ`cframe`をそのまま返す(`src/Instances/Spatial.cpp:9`)。ツリーに一切addChildしない「シャドウ」オブジェクトでもワールド座標=ローカル座標として安全に扱える。
- `Physics::createActor()`/`removeCube()`はWorkspace非依存で、任意のBaseCube(ツリーに属さないものも含む)に対して直接呼び出せる。複数の`Physics`インスタンス(=複数PxScene)を同一プロセス内に共存させることも、static参照カウント方式のおかげで技術的に問題なく可能(前例が無かっただけ)。
- `Humanoid::move()`のアニメーション/回転補間は**dt非依存の固定係数**(呼び出し回数ベース)。ネットワークリプレイ等で「同じ入力を圧縮/間引いて再生する」実装をすると、実際の経過時間と乖離した見た目・軌跡になる。フレーム単位で漏れなく呼び出すことが必須。

## 2026-07-20 ジャンプ同期・補正精度改善・リモートアバター可視性の調査

### 何をしたか

- **ジャンプのネットワーク同期** (`User.hpp/cpp`, `Replication.hpp/cpp`, `NetworkTypes.hpp`):
  - `User::MovementInput`に`jumpRequested`追加。`processHotkeys()`のjump呼び出し時にセット、`processInput()`冒頭で毎フレームクリア。
  - AvatarStateのflags bit2で送信。20Hz間引きでタップを取りこぼさないよう`m_pendingJumpLatch`(送信時クリアのORラッチ)を導入。
  - Host側`hostSimulateAvatars()`は`in.jumpRequested`ならプロキシの`jump(physics)`をmove()直前に実行(jump()自身の接地/水中ガードで妥当性担保)。リプレイ側`reconcileLocalPose()`も同様に各エントリで再生。
- **補正精度の改善** (`Replication.cpp`):
  - AvatarBatchエントリに`linVel`(Vector3)を追加(順序: id→pos→quat→vel→lastProcessedSeq)。Hostは自分と各プロキシのRoot actor線速度を`m_latestVels`に記録して配布。
  - クライアントはリプレイ巻き戻し時、従来の速度ゼロ化をやめ`m_hostAuthoritativeSelfVel`を`setLinearVelocity`で復元(落下/ジャンプ中の補正が正確になった)。
  - 前セッションで疑っていた回転リストア無効化バグを修正: 巻き戻しの`setGlobalPose`のqをauthoritativeではなく`firstEntry.rotationBefore`にし、actorとcframeの回転を一致させた(従来はsyncAllCubes()の読み戻しでcframe側のリストアが即上書きされていた)。
- **「クライアントから他ユーザーが見えない」報告の調査**: 一時診断ログ(`[AVDIAG]`、撤去済み)を仕込み、ユーザーの実シーン(Desktop\NetworkTest)をscratchpadへコピーしてlocalhost 2プロセスで再現テストを実施。

### なぜそうしたか

- ジャンプ問題の真因は「ジャンプが`processHotkeys()`から直接`jump()`を呼び、`lastMovementInput`→AvatarStateに含まれない」こと。Hostプロキシは跳ばない→権威姿勢が地上のまま→補正がジャンプを即引き戻す。「ジャンプできない」「補正が強い」の両方の主要因だった。
- 速度同期を入れたのは、巻き戻しで速度ゼロから再生すると落下中の権威との鉛直速度差で毎回ズレが再発するため。

### どういう経緯か

- ユーザーがTODO.mdに実機テストの感想を記載(ジャンプ不可/補正強すぎ/他ユーザー見えない)。ただしテストに使われたパッケージ(Desktop\NetworkTest)のRecubinEngine.exeは**7/19 23:52ビルド=入力リプレイ実装(7/20 2:48、コミット445b332)より前の中間ビルド**と判明。
- 可視性の再現テスト結果: **現ビルドではクライアント側のアバター生成・姿勢受信・追従・ツリー在籍・メッシュジオメトリ全て正常**。RemotePlayer_1はホストの実位置を正しく追従していた。ユーザー回答(「キャラが全く存在しない」「ホストからは見える」)を現ビルドで再現する経路は特定できず、旧中間ビルド起因の可能性が高いと判断。新パッケージでの再確認待ち。
- 併せてリプレイ収束も再検証: 30秒ランで補正発火は初期落下中の4〜5回のみ、静止後は完全停止。ハードスナップ/Spiral of Deathゼロ。前セッションの「収束しない」観測は解消(速度リストアの効果と推定)。
- ビルド+回帰テストは各ステップで130 passed/4 failed(既知4件)を維持。

### 未解決・保留

- **ユーザーによる再テストが必要**: 旧パッケージは中間ビルドのため、`python build.py build`→再パッケージの上で (1)ジャンプ可否 (2)補正の不快感 (3)相手キャラの可視性 を確認してもらう。
- **スポーンのすり抜け(シーン起因、エンジン未対応)**: NetworkTestシーンでは全キャラ(ローカル/プロキシ共)が生成直後に上のPlate(Y=0,厚1)をすり抜けて地下のPlate1(Y=-45)に着地する。テンプレートRoot(Y=-0.411,高さ4)がPlateにめり込んでおり、PhysXの重なり解消の最短方向が下向き(2.09stud < 2.91stud)のため。同期自体は一貫しているが、スポーン体験として意図通りか要確認。直すならエンジン側スポーン位置補正かシーン側テンプレート配置修正。
- 再テストでも「見えない」が再現する場合、次はRenderer側(collectInstCubes/フラスタムカリング)にRemotePlayer限定の診断を入れて切り分ける。

### 暗黙仕様の発見

- FallingSafe.luauc(ユーザーシーンのスクリプト)は`workspace`の`PlayerCharacter`のRoot Y座標をHeartbeat監視して救済する構造(バイトコードの文字列から推定)。**名前が`PlayerCharacter`固定のためRemotePlayer_Nは救済対象外**。この種のシーンスクリプトはリモートアバターに効かない前提で考えること。
- Luauバイトコード(.luauc)は`python`の正規表現で可読文字列を抽出するだけでも挙動の当たりが付けられる(今回`workspace`/`PlayerCharacter`/`Heartbeat`等を確認)。

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

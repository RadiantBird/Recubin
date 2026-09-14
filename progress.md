# 開発進捗ログ

セッションごとの作業記録。新しいセッションを開始する際はまず一番下（最新）のセッションを読むこと。

---

## 2026-07-29: IPv4 NAT越え実装・ローカル検証

- STUN Binding、8文字ルームコードのランデブー、Local/ServerReflexive/PeerReflexive候補交換、
  双方向UDPホールパンチから同一ソケット上のENet接続までを実装。
- 128-bit admission token、protocol version、room epochでHelloを検証し、候補更新と既存ソケットを
  用いたHost Promote/再パンチをホスト移行経路へ統合。
- Python標準ライブラリのみのIPv4 UDPランデブーサービスと、C++ NAT codec回帰テスト、
  Python統合テストを追加。ローカル3ピアでルーム接続、PeerId 2のUDP 41002据え置き昇格、
  PeerId 3のUDP 41003据え置き再接続を確認。移行時の旧Host TTL競合も再現テストを加えて修正。
- 未完了: 公開STUN/ランデブーを用いた異なる2回線での接続、およびNAT越し3ピアのホスト移行実地検証。
- macOSでPhysXや描画を起動せず同じ`NetworkManager`を検証できる独立CLI
  `tools/network_probe`を追加。別回線での候補、チャット、Roster、PeerId/UDP port維持を表示する。

## 2026-08-03: ネットワークテストCI/CD

- pushとPull RequestでPythonランデブーテストを実行し、Windows/macOSのRelease版
  `RecubinNetworkProbe`をビルドするGitHub Actionsを追加。
- direct Host/Clientの2ピアloopback疎通、双方の`READY expected-peers=2`と双方向チャットを
  標準ライブラリのみで検証し、OS別zipをartifactとして保存するようにした。
- `v*`タグではテスト成功後にGitHub Releaseを作成または再利用し、OS別zipを更新する。

## 2026-08-09: Viewport QoL改善

- LockedをLuau Read/Writeへ公開し、最前面Lockedの遮蔽・クリック解除・矩形選択開始を維持した。
- 通常クリックを最前面Cubeから最上位Modelへ昇格し、Select／Move共通の表面ドラッグとModel子孫AABBによる衝突フィットを追加した。
- Editor選択表示を塗りなしの実形状外枠へ統一し、旧画面矩形／Model AABB線を廃止。ギズモモードのクリック候補へ白い事前外枠を追加した。
- Resizeを初期サイズ非依存のワールド単位加算式へ変更し、`User::gizmoSize`（既定0.20）をEditor設定として保存するようにした。
- Primaryカメラのキーボードズームとホイール許可を分離し、Viewport外スクロールを消費のみとしてカメラへ適用しないようにした。
- Viewportヘルパー回帰を34項目へ拡張し、フルビルドと全項目の成功を確認した。

## 2026-08-10: エディター内マルチクライアントテスト

- エディターのPlay方式に通常プレイ、カメラ位置から初回Characterを生成する「ここでプレイ」、
  localhost専用の「ローカルサーバー」を追加し、選択方式と1〜8人のクライアント数を
  `editor_settings.yaml`へ保存するようにした。
- ローカルサーバーではエディターを非プレイヤー専用Hostとして動作させ、通常`Script`のみを実行する。
  未保存内容を含むPlayスナップショットから`RecubinEngine`を1〜8個起動し、各クライアントでは
  `LocalScript`のみを実行する。接続数表示、個別終了ログ、通常終了要求から強制終了への
  非同期フォールバック、起動失敗時のネットワーク解体とシーン復元も追加した。
- `PeerInfo::isPlayer`をRosterへ追加して専用HostのUser/Character生成を除外し、protocol versionを更新。
  Direct接続にもversion検証を適用し、ローカルCharacterを持たないHostからのアバター、ワールド、
  シミュレーションクロック同期に対応した。
- `User::spawnCharacter`へ任意の初期位置を追加し、Play Hereの`CharacterAdded`発火時点で位置が
  反映済みになるようにした。`IPlatform`には子プロセス起動・監視・通常終了・強制終了APIを追加し、
  Windows/macOS/Mock実装とランタイムの`--window-title`対応を追加した。
- 検証: `cmd.exe /d /c py build.py build`はRecubin/RecubinEngine/RecubinTestの3ターゲットすべて成功。
  `--network-core-regression`と`--runtime-launch-args-regression`も成功した。
- `py run_regression.py Release`は実行済みだが、既存の無関係なシーンにより140 passed / 3 failedのまま。
  `pathfinder.yaml`がUser/CharacterChangerコンテキストで1件、`test_bindings.yaml`が音声欠落と
  IsPlayingで2件失敗する。
- 未確認: macOS子プロセスbackendの実機ビルド、および通常／Play Here／2クライアント／
  8クライアントのGUI手動スモークテスト。

## 2026-08-14: SpawnLocationと共通BaseCube生成・複製

- Cube派生の`SpawnLocation`を追加し、既定の8x1x8白色・固定・衝突あり・Enabled状態を
  YAML、Luau、Properties、Hierarchy Insert、Cubeツールバーへ公開した。
- active Workspace全子孫の有効なSpawnLocationをfull path順で決定的に選び、PeerIdに応じて
  分散するCharacter配置を追加した。Spawnのfull CFrameとRoot/Spawnの半高を使い、Model全体の
  相対姿勢を維持したまま`CharacterAdded`前に配置する。Play Here初回位置を優先し、respawnと
  専用Host上のリモートAvatarも同じ選択処理を使用する。
- `Named`へ派生型の`IsA`連鎖を自動化し、全BaseCube派生の設計状態・子ツリーclone処理を共通化した。
  native physics body、Workspace所有、collision group等の実行時状態はcloneしない。SceneLoaderと
  Luau `Instance.new`は全BaseCube具象型を扱う共通factoryを使用する。
- Floating worldの`baseplate.yaml`は既存Spawnの位置・外観・Decalを保ったままClassNameを
  `SpawnLocation`へ変更し、旧`Spawner`子Scriptだけを除去した。名前による暗黙移行は追加していない。
- `--spawn-location-regression`で既定値、IsA、全factory型、clone、Luau、YAML、full-path/PeerId選択、
  pitch/yaw/rollを含むfull CFrame、CharacterAdded観測、assembly相対姿勢、Play Here、respawn、
  候補なし原点を検証し、Box3D/PhysXともPASSした。
- 検証: `cmd.exe /d /c py build.py build`はRecubin/RecubinEngine/RecubinTestの3ターゲットすべて成功。
  `--starter-root-spawn-regression`、`--starter-accessory-weld-regression`、
  `--humanoid-rig-collision-regression`はBox3D/PhysXともPASSし、`--network-core-regression`もPASSした。
- `run_regression.py Release`は既存と同じ140 passed / 3 failedで、新規失敗はない。外部baseplateも
  SpawnLocationを含めてロードでき、同シーン固有の既存Scriptエラー3件のみを確認した。
- SpawnLocationでリモートAvatar Modelが非identityになった場合、ローカルCFrameから作ったRoot相対値を
  world poseとして直接書き戻してModel変換を二重適用していた問題を修正した。Root/Partのworld CFrameから
  相対値を生成し、受信姿勢も`setWorldCFrame`で適用するため、wire、補間、Host physics proxyは変更していない。
- `--remote-avatar-spawn-transform-regression`で実際の`spawnRemoteAvatar`/`applyAvatarPoses`を通し、
  translationとpitch/yaw/rollを含む非identity Spawn、2 PeerのRoot/body/Weld accessory、2回目補間、
  User identityとModel分離、Spawnなしidentity Model互換を検証し、Box3D/PhysXともPASSした。
- 修正後のRelease全3ターゲット、Network core、Weld accessory回帰もPASSし、全体回帰は
  従来と同じ140 passed / 3 failedで新規失敗はなかった。
- 未確認: SpawnLocation追加・配置・回転のGUI手動スモークテスト。

## 2026-08-14: 今回セッションの総括

- エディター内でNormal／Play Here／localhost専用Hostと1〜8クライアントを切り替えて検証できる
  マルチクライアントPlay環境を完成させた。専用Hostの非プレイヤーPeer、Script／LocalScript分離、
  子プロセスの起動・監視・終了、未保存Playスナップショット共有、設定保存と失敗時rollbackを含む。
- Editorテストクライアントにlocalhost限定の`--editor-test`を追加し、通常パッケージのAssetGuardは
  維持したまま、未パッケージの外部アセットをクライアント間で読み込めるようにした。MeshCube読込失敗時は
  magenta／black checkerの箱を描画し、欠損モデルを論理的・視覚的に判別できるようにした。
- StarterCharacterのWeldアクセサリー問題を調査し、一時的なHumanoid全assembly移動は物理破損を招くため撤回した。
  Box3D／PhysX側でWeld compoundをnative body単位に一度だけ同期する方式へ修正し、Hair／Glassesの追従、
  複数キャラクター間のassembly分離、長時間移動時の有限な速度を回帰で保証した。
- cloneされたStarterCharacterのRootがテンプレートの`Anchored=true`を引き継いで浮く問題を修正し、
  `CharacterAdded`前にRootだけを`Anchored=false`、`CanCollide=true`へ正規化した。
- `SpawnLocation`を導入してNormal／respawn／ネットワークAvatarの生成地点を統一し、Play Here初回だけは
  カメラ位置を優先した。同時にBaseCube派生の`IsA`、clone状態転送、SceneLoader／Luau factoryを共通化し、
  Floating worldのbaseplateを旧Spawner ScriptからSpawnLocationへ移行した。
- 非identityのCharacter Modelへ受信Root world poseをローカル値として書き込み、リモートCharacterが遠方へ
  二重移動していた問題を修正した。Root相対値と姿勢適用をworld CFrame規約へ統一し、身体とWeldアクセサリー、
  複数Peer、補間、Spawnなし互換を専用回帰で確認した。wire protocolとHost physics proxyは変更していない。
- 最終検証ではReleaseのRecubin／RecubinEngine／RecubinTestがすべてbuild成功。Network core、runtime args、
  SpawnLocation、remote Avatar、Starter Root／Weld accessory／Humanoid collision等の関連回帰はPASSした。
  全体回帰は既存と同じ140 passed / 3 failedで、新規失敗はない。
- 残作業はmacOS子プロセスbackendの実機buildと、Normal／Play Here／2・8クライアントでのGUI手動スモーク。

## 2026-08-14: AnimationClip／R6 Walk回帰と保存仕様

- 内蔵R6 WalkのAnimationClip、`.rcanim` round-trip、破損／型違い／新version拒否、旧Animation YAML
  import、Sceneヘッダーなしversion 0、migration decision round-trip、新version拒否を
  `--animation-clip-regression`へ追加した。
- Animation、Humanoid、SceneLoader、AnimationEditorPanelの仕様文書へAnimationClipと
  `.rcanim`／Scene migrationの契約を追記した。GUI移行確認は手動スモーク対象として残る。
- Release build成功。`--animation-clip-regression`、`--asset-path-regression`、
  `--starter-root-spawn-regression`、`--starter-accessory-weld-regression`、
  `--humanoid-rig-collision-regression`、`--remote-avatar-spawn-transform-regression`は
  すべてfailures=0。全回帰は140 passed / 3 failedで既知baselineと一致した。
- GUI migration確認とAnimation Editorの手動スモークは未実施。

## 2026-08-14: R6 Walk migration UX改善検証

- Editor R6 Walk migration UXを更新。Sceneロード後にのみ判定し、Scene親基準のabsolute/normalized生成先を表示する。
  生成後の即時再読込・System適用、成功/失敗/上書きモーダル、snapshot復元時の確認抑止を追加。
- Release build成功。`--animation-clip-regression` はfailures=0。
- Full regressionは140 passed / 3 failed（既知のPathfinder 1件・Sound 2件）で、新規失敗なし。

## 2026-08-16: Character Animationの明示参照化とmigration再設計

- `Animation`をScene Tree上の正式な資産Instanceとし、トラックデータを単一の`AnimationClip`へ統合した。
  `Humanoid.WalkAnimation`／`JumpAnimation`／`EquipAnimation`はAnimationを明示参照し、
  StarterCharacterからPlayerCharacterへのclone時にclone側へ再接続する。
- 標準Walkは`assets/anims/r6_walk.rcanim`へ配置した。ユーザー指定Animationを優先し、欠損・破損時も
  参照とContentPathを変更せず実行時だけ内蔵Walkへフォールバックする。参照自体がない旧Characterでは
  PlayerCharacter側だけに可視なBuiltIn Animationを追加する。
- 旧Scene migrationは`character_animation_bindings.version: 1`未記録かつ空欄の参照だけを一度補完する。
  非空の未解決参照を保持し、移行後の削除・差し替えには再挿入しない。旧生成headerはread-only互換とし、
  新規保存ではcharacter binding markerだけを出力する。明示的な`Restore Default Animations`だけ再設定を許可する。
- GUI非依存のmigration helperと構造回帰を追加し、Animation可視性、3参照のclone remap、custom優先、
  欠損／破損fallback、Scene参照round-trip、legacy path変換、migration一回性、無題／非R6非変更、
  Packagerの`assets/anims`同梱を検証した。
- 検証: ReleaseのRecubin／RecubinEngine／RecubinTestはbuild成功。`--animation-clip-regression`、
  `--asset-path-regression`、`--starter-root-spawn-regression`、`--starter-accessory-weld-regression`、
  `--humanoid-rig-collision-regression`、`--remote-avatar-spawn-transform-regression`はすべてfailures=0。
  全体回帰は既知baselineと同じ140 passed / 3 failed（Pathfinder 1件、Sound 2件）で新規失敗なし。
- 未確認: PropertiesでのWalk／Jump／Equip差し替え、Restore Default Animations、Animation Editor
  import/export、Play／Local ServerのGUI手動スモーク。

## 2026-08-21: Sceneロードのトランザクション化

- `SceneLoader`のプロセス全体singleton表を廃止し、呼び出し単位の`LoadContext`へ置き換えた。
- `SceneRuntime`へ隔離System/Userを使うStageと、live System/Userのidentity・入力・Signal・Cameraを
  保持して子ツリーと保存対象値だけを移植するCommitを追加した。
- EditorのOpen SceneはStage成功後にだけUndo、Terrain、Physicsを解放してCommitするため、
  `User`を未知クラスとして事前検査でスキップせず、NotFoundやYAML変換失敗でも現在シーンを維持する。
- `--scene-load-transaction-regression`を追加し、StageのUser/Inventory/Tool構築、live不変、失敗保持、
  Commit identity保持、既定補完、NotFound、LoadContext非漏洩を検証対象にした。
- PhysicalFileInstanceRegistry化に関する既存の未コミット変更は保持して統合した。
- 検証: ReleaseのRecubin／RecubinEngine／RecubinTestの3ターゲットはbuild成功。
  `--scene-load-transaction-regression`はfailures=0、`--inventory-tool-sync-regression`はPASS、
  `--animation-clip-regression`はfailures=0。全体回帰は既知baselineと同じ140 passed / 3 failedで、
  新規失敗はない。GUIはユーザーからアプリ動作良好の報告あり。

## 2026-08-21: 外部画像変更時のテクスチャキャッシュ更新

- `Renderer::loadTexture()`が正規化パスごとに保持する最終更新時刻とファイルサイズを比較し、
  外部画像の変更時は既存のOpenGLテクスチャを破棄して再読み込みするようにした。
  ファイル情報を取得できない場合は既存キャッシュを維持する。
- `doc/Core/Renderer.md`へ画像キャッシュの更新検知仕様を追記した。
- 検証: `cmd.exe /d /c py build.py build`はRecubin／RecubinEngine／RecubinTestの3ターゲットすべて成功。
  `git diff --check`は成功。`--asset-path-regression`はWSLの`UtilBindVsockAnyPort: socket failed 1`
  により起動できず未実施。

## 2026-08-21: TextFileとSystem拡張権限

- TextFileのEditor挿入・永続seed、System拡張設定、RuntimeFileSystem接続、Packager ApplicationId出力と
  通常ランタイムの拡張権限同意モーダルを追加した。
- ReleaseのRecubin／RecubinEngine／RecubinTest 3ターゲットbuild成功。
- `--system-extension-regression` PASS、`--physical-file-instance-regression` failures=0、
  `--asset-path-regression` failures=0、`--scene-load-transaction-regression` failures=0。
- 全回帰は140 passed / 3 failedで既知baseline（Pathfinder 1・Sound 2）と一致し、新規失敗なし。
  GUI自動スモークは未実施。

## 2026-08-22: SystemExtensionSmoke手動検証

- `tools/capture_window.ps1`と`click_window.ps1`を追加した（Title/Class filter、timeout、NoActivate、click hold対応）。
- SystemExtensionSmoke二列fixtureと`RecubinTest --package-system-extension-smoke`を追加した。
- ReleaseのRecubin／RecubinEngine／RecubinTest 3ターゲットbuild成功。
- `--system-extension-regression`、`--physical-file-instance-regression`、`--asset-path-regression`、
  `--scene-load-transaction-regression`は全てPASS。
- GUI確認: 通常runtime初回警告でIO+IPC列挙を確認。Studio Playは警告なしでIO/IPC/TextFile全PASS。
  Packager生成実package内EXEでも全PASS。
- PNG artifacts: `artifacts/SystemExtensionSmoke/runtime-warning.png`、
  `artifacts/SystemExtensionSmoke/editor-results.png`、
  `artifacts/SystemExtensionSmoke/package-results.png`。
- 自動全回帰は今回未実施。

## 2026-08-24: SurfaceMark Editor integration

- Effects の挿入メニュー、階層アイコン、テストシーン生成へ SurfaceMark を追加した。
- Properties に Width/Height/Projection Depth の説明、schema Color、画像 Browse/Clear（project-relative + undo）を追加した。
- View メニューと editor_settings.yaml Preferences に RenderingDebug を追加し、ViewportRenderDesc へ伝播するようにした。
- `cmd.exe /d /c py build.py build`でRecubin／RecubinEngine／RecubinTestのReleaseビルド成功（既存NOMINMAX/APIENTRY警告のみ）。
- `--surface-mark-regression`は継承、既定値、Forward、親Transform、volume、clone、factory、YAML、Luau Source／Color／read-onlyを全てPASS。
- `--asset-path-regression`、`--viewport-helper-regression`はPASS。
- `py run_regression.py Release`は140 passed / 3 failedで既知ベースライン一致（Pathfinder 1、欠落音声のSound 2）。SurfaceMark新規失敗なし。
- RecubinEngine Releaseで一時Sceneを6秒runtime smokeし、texture load成功、shader compile／FBO incomplete／OpenGLエラーログなし。一時fixture／logは削除済み。
- 手動GUIでの視覚比較（正面・斜面・角・重なり、debug toggle見た目）は未実施。

## 2026-08-24: SurfaceMark SCALE gizmo origin fix

- SurfaceMark の Position が near-plane 原点で volume 中心と異なるため SCALE ギズモがずれる問題を修正した。
- SCALE ギズモ中心を local `(0,0,-Depth/2)` へ補正し、`fixedFaceResizeOrigin` で near/far の反対面固定を保証した。通常 Spatial の挙動は維持した。
- Release 3ターゲットのビルド成功（既存NOMINMAX/APIENTRY警告のみ）。
- `--viewport-helper-regression`（centered／far／near／90度回転を含む）と `--surface-mark-regression` はPASS。
- 手動GUIドラッグ確認は未実施。

## 2026-08-24: SurfaceMark投影フィルター

- SurfaceMarkへExclude/IncludeとInstance参照配列を追加。Model/Folder子孫BaseCubeに一致し、Exclude対象は深度生成からも除外して奥の許可対象へ投影できる。
- YAML相対パス、未解決保持、保存時パス更新、Luau配列getter/setter、Editor Add/Remove/Clear Undoを実装。
- `--surface-mark-regression`へ空モード、祖先一致、重複・期限切れ、clone、YAML round-trip/rename/unresolvedを追加。
- 回帰項目にはLuau setterの無効要素に対する原子性、未解決entryの保持、clone時の内部参照remapも含めた（この項目の実行結果は別途記録する）。
- ReleaseのRecubin／RecubinEngine／RecubinTest 3ターゲットbuild成功（既存NOMINMAX/APIENTRY警告のみ）。
- `--surface-mark-regression`はfilter modes、Model/Folder、expired、clone remap、YAML rename/unresolved、Luau atomicを含め全PASS。
- `--scene-load-transaction-regression`はfailures=0でPASS。全回帰は140 passed / 3 failedで既知baseline（Pathfinder 1、Sound 2）一致、新規失敗なし。
- GUIのInclude/Exclude picker、Undo、奥への投影確認は未実施。

## 2026-08-24: 検索付きクラス選択とInstance置換

- 挿入・グループ化・置換を共通の検索付きクラス選択UIへ統合し、Container／File／Script分類を追加した。
- Instance置換で互換プロパティと子要素identityを維持し、typed参照は互換時に更新、非互換時は警告表示して解除するようにした。
- ObjectValue、SurfaceMark、制約、Humanoid Animation、Tool Handle等の参照をUndo/Redoで復元可能にした。
- ReleaseのRecubin／RecubinEngine／RecubinTest 3ターゲットbuild成功。`--scene-hierarchy-grouping-regression`はfailures=0。
- constraint／tool／humanoid／surface-mark／physical-file各回帰はPASS。既存NOMINMAX/APIENTRY等のwarningのみ。
## 2026-08-24: GUI automation documentation

- `--ui-automation` のコマンド形式、Explorer target ID、back framebuffer capture契約を `doc/Editor/GuiAutomation.md` に整理した。
- `--gui-automation-regression` にPNG signature/IHDR/invalid buffer回帰を追加した。
- ReleaseのRecubin／RecubinEngine／RecubinTestビルド成功。`--viewport-helper-regression`はPASS。手動GUI確認は未実施。

## 2026-08-25: v0.999 リリース前コード監査完了

- Editor: 置換確認を選択クラス別に管理し、ImGui popupの自動close、TextFile group cleanup、
  unrelated class間の同名同型PropertyRegistry移送（型不一致破棄）、Highlight Luau dispatch、
  CommandHistoryの責務別分割を実施した。
- Runtime: SoundのWorldCFrame位置計算、AudioServiceの段階的初期化rollbackと冪等teardownを実施した。
  startup/editor settings/terrain YAMLは共通結果型で失敗をログ出力し、破損時の保存を遮断した。
- GUI automation: parserをtokenize/validation/queueへ分割し、Windows/macOS/MockのIPlatformで
  main-thread非ブロッキングstdinへ統一した。automation専用の未保存danger cooldownは0秒、通常は3秒。
- 回帰基盤: 42-entry registry、`--list-regressions`、manifest一致検査、timeout、PhysX/Box3D両backend、
  fixture、Python標準ライブラリ生成PCM WAV、回帰sceneの相対化を整備した。
- 検証: Windows ReleaseのRecubinCore、Recubin、RecubinEngine、RecubinTest build成功。
  42 dedicated regressionsはPhysX/Box3Dで全PASS、両backend performance guard PASS、GUI smoke PASS、
  9 scenesで228 passed/0 failed、Regression OK。PropertyRegistry不可視、Unknown User、Lighting重複、
  Sound fixture欠落の警告なし。
- macOS静的確認: IPlatform純粋仮想メソッド一致、MacPlatform実装、Windows API漏出なし、CMakeの.mm選択、
  自動sceneのWindows絶対パスなしを確認。macOS実機buildは未実施。
- リリース作成・公開・バージョン変更は未実施。
- 追加finding（severity: test false-state/fixture warning、リリースruntimeクラッシュではない）を修正。
  headless UserがInventoryを先に生成してシーンInventoryをInventory1へrenameしていたため、ロード済みInventoryを
  採用し不足時のみ補完するよう変更した。自動sceneの欠落Prox/FallingSafe bytecodeと空Scriptは同梱source/無効fixtureへ置換し、
  欠落警告と常駐timeoutを解消。最終`run_regression.py Release`は42 x PhysX/Box3D、performance、GUI、9 scenes 228/0でexit 0。

## 2026-08-25: User入力制御・F1-F12・MouseLock

- Userへ保存対象の入力カテゴリ、F1-F12 raw input、Direct API、script移動、listener-aware ExitRequested、
  primary viewport中心を使うMouseLockを実装した。
- 検証: ReleaseのRecubin／RecubinEngine／RecubinTest 3ターゲットbuild成功。
  `--user-input-controls-regression`は32 assertions PASS。
  `--frame-rate-invariance-regression`、`--scene-load-transaction-regression`、
  `--network-core-regression`はPASS。
  `py run_regression.py Release`は45 dedicated regressionsと9 scenesで234 passed / 0 failed、Regression OK。
- 実GLFW/ImGuiでのviewport中心座標の視覚smokeは未実施。

## 2026-08-25: 複数Cube変形・Rename対応

- 複数選択Resizeへ個別Resize／Group Scale切替を追加し、反対面固定の軸別変更、集合中心基準の
  ワールド軸倍率、倍率スナップ、設定保存へ対応した。
- Propertiesで全Instanceの衝突回避付き連番Renameと、複数SpatialのワールドPosition／Size／CFrameを
  統合入力または成分別に編集できるようにし、各操作を一括Undo/Redo対応にした。
- `--viewport-helper-regression`へGroup Scaleの軸別／一様倍率、倍率スナップ、選択全体の最小Size clamp、
  回転Cubeの反対面固定を追加した。
- `--scene-hierarchy-grouping-regression`へ複数Renameの未選択兄弟衝突とruntime name lock、二段階renameの
  undo/redo children key復元、および異なるSpatial親を跨ぐMultiSpatialTransformCommandのworld CFrame／Size
  適用とundo/redoを追加した。
- ReleaseのRecubin／RecubinEngine／RecubinTestビルド成功。`--viewport-helper-regression`と
  `--scene-hierarchy-grouping-regression`はPASS。全回帰は45 dedicated regressionsと9 scenesで
  234 passed / 0 failed、Regression OK。手動GUI確認は未実施。

## 2026-08-26: 複数Resizeの固定ピボット修正

- Individual／Group Scaleの両モードで、複数Resizeドラッグ開始時の集合AABB中心を固定ピボットへ保存するよう修正。
  Individualのサイズ・位置変更後もAABB中心を毎フレームImGuizmoへ渡さないため、リサイズ中のギズモの暴れを防止する。
- ReleaseのRecubin／RecubinEngine／RecubinTestビルド成功。`--viewport-helper-regression`はPASS。
  `git diff --check`も成功。手動GUIでのドラッグ確認は未実施。

## 2026-08-26: ExplorerのShift範囲選択・子選択

- Explorerの展開状態を反映した可視行順で、Shiftによる置換範囲選択とCtrl+Shiftによる重複なしの追加選択を実装した。
- 通常／Ctrlクリックのアンカー更新、無効・非表示アンカーの単一選択フォールバック、右クリック対象の直下の子だけを選ぶ「子をすべて選択」を追加した。
- 可視範囲と直接の子の抽出をUI非依存ヘルパーへ分離し、`--scene-hierarchy-grouping-regression`へ前後方向、折りたたみ、併合、フォールバック、直接の子の回帰を追加した。
- ReleaseのRecubin／RecubinEngine／RecubinTestビルド成功。`--scene-hierarchy-grouping-regression`はPASS。手動GUI確認は未実施。

## 2026-08-26: Viewport複数選択のCtrl統一

- Select／Move／Resize／Rotateの全ツールで、Ctrl+クリックだけで対象を複数選択へ追加・解除できるよう統一した。
- 非Selectツールで必要だったShift併用を廃止し、Primary自身のCtrl+クリックも再問い合わせして解除できるようにした。
- ReleaseのRecubin／RecubinEngine／RecubinTestビルド成功。`--viewport-helper-regression`はPASS。手動GUI確認は未実施。

## 2026-08-26: 非Select変形ツールの空間クリック選択解除

- Move／Resize／Rotateで修飾キーなしに空間をクリックした場合、Primaryと複数選択を解除するようにした。
- 現在選択中の対象、未選択対象、Locked対象へのヒットは従来どおり扱い、空間クリックによる解除対象から除外した。
- ReleaseのRecubin／RecubinEngine／RecubinTestビルド成功。`--viewport-helper-regression`はPASS。手動GUI確認は未実施。

## 2026-08-26: Viewport境界のテクスチャノイズ修正

- FBOカラーテクスチャのS/Tラップを`GL_CLAMP_TO_EDGE`へ固定し、線形補間時に反対側の端が
  レターボックス境界へ混ざる1pxノイズを防止した。
- ReleaseのRecubin／RecubinEngine／RecubinTestビルド成功。`--viewport-helper-regression`はPASS。手動GUI確認は未実施。

## 2026-08-26: Userカスタムマウスカーソル

- `User.CursorType` の Default／Type1〜10、各画像ContentPath・Hotspotを実装し、PropertiesのBrowse／Clear／Hotspot編集、Luau Enum、YAML／Packager、GLFW適用へ対応した。
- SVGは今回対象外。ReleaseのRecubin／RecubinEngine／RecubinTestビルド成功、対象3回帰成功、9シーン全回帰は236 passed / 0 failed、GUI自動smoke成功。カスタムカーソル固有の手動GUI確認は未実施。

## 2026-08-28: GUI整理と日本語パッケージ対応

- Resize横の常設▼メニュー、BaseCubeカテゴリ整理、PackagerのUTF-8 filesystem境界、
  日本語gameName/outputDir/scene/startup、RecubinEngineの実行ファイル基準content root（`--scene`時は維持）を実装した。
- ReleaseのRecubin／RecubinEngine／RecubinTest 3ターゲットbuild成功（既存NOMINMAX/APIENTRY警告のみ）。
  全回帰初回はテスト自身のnarrow日本語pathでasset-pathがPhysX／Box3D各1 FAILしたが、テスト修正後は
  `--asset-path-regression` が両backendでPASS。全回帰中の`--viewport-helper-regression`両backend、GUI automation smokeはPASSし、
  他の新規失敗はなかった。日本語名の実パッケージを外部CWDから4秒起動し、startup／scene／script読込と非即時終了を確認した。
- 一時package／processは削除済み。手動GUIの見た目確認は未実施。
- 修正後の最終全回帰は236 passed / 0 failed、GUI automation smokeを含めRegression OK。

## 2026-08-28: カスタムマウスカーソルの論理サイズ対応

- UserのType1〜10へ`Size`を追加した。既定32px、設定範囲1〜512論理pxとし、画像の縦横比を維持して
  bilinearリサイズする。
- GLFWのcontent scale／DPIを物理サイズとHotspotへ反映し、Luau、YAML、Properties、Undo/Redo、
  PackagerでのSize保持へ対応した。
- ReleaseのRecubin／RecubinEngine／RecubinTest 3ターゲットbuild成功。対象3回帰はPASS。
  全回帰は9 scenesで236 passed / 0 failed、GUI automation smoke成功。
  カスタムカーソルサイズの手動目視確認は未実施。

## 2026-08-28: カーソル画像加工と入力バックエンド責務分離

- `CursorImageData`／`CursorImageProcessor`を新設し、画像の読込・加工と成功／失敗／mtimeキャッシュを
  User側の共通処理へ分離した。
- `IInputBackend`／GLFWからSize、ContentPath、DPI処理を除去し、完成RGBA、物理寸法、物理Hotspot、
  revisionだけを受け渡す構成へ変更した。
- ReleaseのRecubin／RecubinEngine／RecubinTest 3ターゲットbuild成功。対象3回帰はPASS。
  全回帰は9 scenesで236 passed / 0 failed、GUI automation smoke成功。
  カスタムカーソル固有の手動目視確認は未実施。

## 2026-08-28: ポータブル保存root

- `LOCALAPPDATA`／`APPDATA`／`Application Support`およびRuntime／Editor／ApplicationId階層を廃止。Windowsはexe隣、macOSは`.app/Contents/Resources`、Editor／`--scene`は起動CWDをrootとし、IOは直下、TextFileは`textfiles/<StorageId>.txt`、receiptはroot直下へ保存する。External等の制限は維持し、旧データ移行は行わない。
- ReleaseのRecubin／RecubinEngine／RecubinTest build成功（既存警告のみ）。`--system-extension-regression`と`--asset-path-regression` PASS、全46 dedicated + GUI automation + 9 scenes 236/0 Regression OK。

## 2026-08-28: 死亡中のカメラ操作

- Character死亡中はCharacterモードでその場回転、Freeモードで自由移動を可能にし、Lキーによるモード切替を有効化した。Free移動で死亡ラグドールを上書きしないようにした。
- ReleaseのRecubin／RecubinEngine／RecubinTest 3ターゲットbuild成功（既存APIENTRY警告のみ）。`--user-input-controls-regression`は追加3ケースを含め failures=0 / 1 passed 0 failed。対象差分の`git diff --check` PASS。

## 2026-08-28: 物理制約基底クラスとEnabled Editor配線

- `PhysicsConstraint` 基底クラスを追加し、Enabled、Cube参照、Constraint handle、Workspace登録・解除、共通プロパティ処理を集約した。
- Rope/Rod/BallSocket/Weld/Motor/NoCollisionを基底から派生させ、EditorのEnabledチェックボックスを共通Undo/Redoコマンドへ接続した。
- 対象差分の`git diff --check`は成功。ReleaseビルドはWSL↔Windows通信エラー（`UtilBindVsockAnyPort`）で未実施。

## 2026-08-28: 単体Attachmentの移動ギズモ表示修正

- SizeがゼロのAttachmentにゼロスケールのモデル行列を渡していたため、単体選択時も単位スケールでImGuizmoを描画するよう修正した。

## 2026-08-28: シーン読み込み時のようこそタブ自動クローズ

- 有効なシーン読み込み要求を受け付けた時点で、ようこそタブを自動的に閉じるよう修正した。ファイル選択のキャンセル時は表示状態を維持する。
- ReleaseのRecubin／RecubinEngine／RecubinTestビルド成功。全回帰はWSL↔Windows通信エラー（`UtilBindVsockAnyPort`）で未実施。

## 2026-08-29: 物理制約の複数選択Enabled編集

- 複数選択した全項目がPhysicsConstraintの場合、PropertiesからEnabledを一括変更できるようにした。変更は1件の複合Undo/Redo操作として記録する。
- ReleaseのRecubin／RecubinEngine／RecubinTestビルド成功。全回帰はWSL↔Windows通信エラー（`UtilBindVsockAnyPort`）で未実施。

## 2026-08-29: 実行時スキーマによる物理制約共通プロパティ

- `PropertyRegistry::collectApplicableSchema()`を追加し、具象クラスの個別登録がなくても`IsA()`に一致する基底スキーマをPropertiesへ提供するようにした。
- PhysicsConstraintのEnabledを副作用付きスキーマへ登録し、単一・複数選択とも共通のSetPropertyCommandでUndo/Redoする。複数選択はスキーマの和集合を表示し、対応する型だけへ適用する。
- ReleaseのRecubin／RecubinEngine／RecubinTestビルド成功。`--property-schema-regression`はWSL↔Windows通信エラー（`UtilBindVsockAnyPort`）で未実施。

## 2026-08-29: スキーマ駆動Propertiesの共通Editor種別

- `PropertyDesc`へ`EditorWidget`、ファイルダイアログ設定、複数選択互換キーを追加した。PhysicalFileInstanceのPathは手書き専用UIを廃止し、共通のFilePath入力・Browse・Clear・Undoへ移行した。
- ReleaseのRecubin／RecubinEngine／RecubinTestビルド成功。`--property-schema-regression`はWSL↔Windows通信エラー（`UtilBindVsockAnyPort`）で未実施。

## 2026-09-03: Character所在WorkspaceへのPlay中自動追従

- Play中のCharacter所属Workspace自動追従、User API、Pキー／Explorer共通移動、非推奨ラベル削除、関連docs/testsを実装した。
- ReleaseでRecubin／RecubinEngine／RecubinTestビルド成功。`--multi-workspace-regression`はPhysX／Box3DともPASS。
- `workspaceShift.yaml`／`Portal.luau`による往復、Primary Viewport・Explorer・カメラ追従、Secondary Viewport固定表示はユーザー確認済み。

## 2026-09-09: Scene Autosave / Crash Recovery

- AutosaveManager（1秒Recovery、5分・5世代snapshot、atomic replace、session lock、正常終了cleanup）とEditor lifecycle／transactional Recovery modalを実装した。`.rcbn` migrationとPackagerの`.autosave`除外も反映した。
- ReleaseのRecubin／RecubinEngine／RecubinTest 3ターゲットbuild成功。対象3回帰PASS、GUI smoke PASS。F12一時abortの実機検証PASS後、hookは完全削除した。
- 全回帰は49 dedicated両backend＋9 scenesで242 passed / 0 failed Regression OK。Windows `py`不在のため、同等のVS CMake direct buildを使用した。

## 2026-09-11: Autosave候補走査の不要エラー抑制

- 現在のactive sessionをCrash Recovery候補から除外し、lockだけでRecovery未作成・欠落のディレクトリは
  正常な非候補としてエラーログなしで無視するよう修正した。実際のfilesystem検査失敗と破損Recoveryの
  path付きエラーログは維持した。
- Releaseビルド成功。`--autosave-regression`へactive session自身の除外とorphan lockの非候補判定を追加し、全件PASS。

## 2026-09-11: Autosave保存先の実行ファイル基準化

- Windows版EditorのAutosave保存・Recovery探索rootを起動CWDから`Recubin.exe`の親ディレクトリへ変更した。
  実行ファイル位置は`GetModuleFileNameW`を優先し、失敗時は`argv[0]`の絶対化、起動CWDの順でfallbackする。
- EditorManagerへ保存rootを明示注入し、AutosaveManagerの内部命名からproject root前提を除去した。
  旧CWD配下のAutosaveは移行・走査・削除しない。
- Autosave回帰へ保存rootとCWDの分離・CWD変更後の候補探索を追加し、GUI Recovery fixtureもexe隣へ変更した。
  Release 3ターゲットbuildとAutosave回帰に成功し、全回帰は49 dedicated両backend＋9 scenesで242 passed / 0 failed。

## 2026-09-11: macOS Studio平坦packageと内部I/O通知

- macOS Studio配布を`.app`から平坦な`RecubinStudio/`＋zipへ変更し、Windows/macOSとも`.autosave` markerを
  作成する。再packageでは既存Autosaveを保持し、archiveには空markerだけを含める。
- macOS portable配布は実行ファイル隣のmarkerと必須resourceから判定し、CWD、設定、Autosave rootを
  同じ場所へ揃える。開発実行とゲーム用macOS App Bundleは維持する。
- `IPlatform`へ同期エラーダイアログを追加し、設定とAutosaveの実I/O失敗をoperation/path単位で一度だけ
  通知する。成功後の再失敗は再通知し、欠落・parse・serialization障害はログだけに残す。
- Windows Release 3ターゲットbuild、Python layout回帰、対象C++回帰、GUI smokeを含む全回帰に成功し、
  49 dedicated両backend＋9 scenesで242 passed / 0 failed。Mac実機でのpackage、署名、Finder起動、
  権限エラー通知確認は未実施のため、`TODO.md`のMac release項目は未完了のまま維持する。

## 2026-09-12: SceneLoaderのlocal CFrame復元

- Sceneロード中の`addChild()`がreparentのworld姿勢維持を行い、YAMLから設定済みのModel・子Spatialの
  local CFrameを変換していたことが原因。接続前に子サブツリーのlocal CFrameを収集し、接続後に
  deserialization batchで復元するよう修正した。通常のreparent仕様は維持する。
- `--viewport-helper-regression`で物理同期後のModel/子姿勢、保存YAML、再読込後のlocal/world姿勢を
  検証予定。ビルド・回帰テストは未実施。

## 2026-09-12: Gyroの軸別独立制御への移行

- GyroをQuaternion目標からX/Y/Z各軸の独立設定へ再設計し、単一Partをworld角度へ制御する形へ変更した。
- Box3D fixed step前に応答率20/sのcritical PDトルクを軸ごとに計算し、compound member offsetを含むworld回転と
  world inverse inertiaを使って制御する。`MaxTorque`だけを1/400変換し、角速度上限とトルク上限を個別に適用する。
- `CharacterRig`がX/Z直立軸を初期化し、HumanoidとReplicationの共通Gyro helperはY方位だけを更新する。
  Humanoidは水平入力方向から方位を直接算出し、無入力時は最後のY目標を維持する。
- `PhysicsConstraint::endpointsReady()`を二端constraintの既定判定とし、Gyroは一端判定をoverrideする。
  ancestor変更時も`registerIfReady()`へ統一し、Gyroの設定変更はnative objectを再生成せずbodyをwakeする。
- 異常ログにはGyroとPartのfull pathを含め、正常復帰時に重複抑制keyを解除して再発を観測可能にした。
- 独立した`--motor6d-gyro-regression`へ角度helper、軸独立性、Humanoid入力、既定Rig、compound Motor6D、
  Scene round-tripの検証を追加した。
- Windows Releaseビルドはconfigure起動時に`FileNotFoundError: [WinError 2]`で停止した。
  ビルド成果物を更新できなかったため、対象回帰は未実施。
- `--motor6d-gyro-regression`の初回実行でHumanoid fixtureがactor reconcile前にbodyを確認していたため、
  `initPhysics()`後に`Physics::update(..., 0)`を行うよう修正した。さらに実Box3D stepによる各軸・四象限収束、
  inertia補償、torque差、外乱復帰、disabled軸、角速度制限、Weld compound、scene load・handle維持、
  default R6の方位・直立復帰を追加した。再ビルドと回帰再実行は親エージェント側で実施予定。

## 2026-09-13: Character GroundHeight hover controller

- 既定R6 Rootを通常の`Cube`のまま`Size=(2,2,1)`へ戻し、Root原点とMotor6Dのbind poseは維持した。
  R6脚のbind poseからRoot中心と足裏の距離を2 studと定義し、SpawnLocation上でも同じ中心高度を使う。
- Humanoidの既存下向きraycastをGroundHeight controllerとgrounded判定で共用する。床が3 stud以内のとき、
  Workspace重力の相殺とPD補正（stiffness 120、damping 20、上向き加速度上限1200）を計算する。
- Rootだけで身体を吊らないよう、共通のR6 body列挙をjumpとhoverで使い、各dynamic bodyのmassに比例した
  additive `CharacterHoverForce`をbody中心へ適用する。Box3Dのread-only mass取得APIを追加した。
- jump開始時は全hover Forceをzero/disabledにし、全bodyへ従来のlaunch velocityを設定する。上昇中は
  hoverを再開せず、下降して3 studのlanding captureへ入った時点で再開する。死亡・着席・Truss中も無効化する。
- Windows ReleaseのRecubin／RecubinEngine／RecubinTest 3ターゲットbuild成功。限定回帰
  `--character-hover-regression`は1 passed / 0 failed。静止高度1.913、jump最高高度7.579、再着地高度1.916、
  7 bodyのmass比例加速度一致、jump上昇中のhover停止と下降時の再開を確認した。
- 実際の入力による前後左右移動、Yaw旋回、移動・旋回中jump、連続jumpの手動操作確認は未実施。

## 2026-09-13: Character Rig v2 RootJoint yaw overshoot diagnostic

- RootJoint spring無効化は姿勢保持を失わせるため撤回し、Motor6Dのspring設定を従来の
  `enableSpring = true`へ戻した。
- 次の診断として、既存の `getChildren()` 経由でYawForceを取得し、Box3D fixed stepごとにRoot bodyの
  Value.y、Enabled、Torque、MaintainVelocity、AxisMask、pre-step、solver直後、post-MaintainVelocityの
  Y angular velocityを同じ1行へログする変更へ切り替えた。物理値の設定変更はない。
- 追加診断として、実際に処理されたYawForceのowner Modelから既存のR6 body列挙を使い、制御状態が有効な
  fixed stepだけ全R6 physical bodyへ同じY angular velocityを適用する一時変更を行った。RootJoint springは維持し、
  X/Z angular velocityは保持する。post-stepのRoot Y angular velocity変化を確認する。
- `git diff --check`は成功。指定の `cmd.exe /d /c py build.py build` はコンパイル前に
  `UtilBindVsockAnyPort: socket failed 1`で失敗し、ビルド結果は未検証。
- 次の判定: commandに対する符号反転がpre-step以前、solver直後、post-MaintainVelocityのどこで
  発生するかをログで切り分ける。

## 2026-09-13: CharacterSmoothing yaw配線とInstance schema監査

- CharacterSmoothingのdt補間状態をHumanoidへ追加し、通常移動は既存のcurrentMoveDir、CtrlLockは
  平滑化済みcamera headingをYawForceの目標方向へ渡すよう再配線した。Replication replay用に補間状態も保存・復元する。
- 基準コミット`2bf9f2f`以降のInstance差分を確認した結果、追加Instanceそのものはなく、差分は
  Box3D/CharacterRig/Humanoidの変更だった。一方、既存のPhysics/Character InstanceにはPropertyRegistry・
  PropertiesPanel・SceneLoader・Luau dispatchの配線漏れが残っていたため、実在するクラスを監査対象に含めた。
- BallSocket/Rod/Rope/Motor/NoCollision/Weld、MeshCube、Toolのスキーマを追加し、BaseCube派生の
  Cube/Sphere/Cylinder/TriangularPrism/Truss/Seat/SpawnLocation/LiquidCube/Skybox/Sun/Moonの継承鎖を登録した。
  物理制約の手書きProperties UI・保存・Luau getter/setterをschema駆動へ寄せた。
- Task 1/2の変更はまだコミットされていない。Gitは`.git/index.lock`作成時にRead-only filesystemとなり、
  別コミット化できなかった。
- Release buildは`cmd.exe /d /c py build.py build`のconfigure起動で`FileNotFoundError: WinError 2`により停止。
  WSL側の限定C++ syntax checkはconstraint/Tool/PropertyRegistryで成功したが、Windows buildとruntime回帰は未実施。
- SeatのSteer/Throttleはエンジン更新のライブ値としてeditor非公開（Luau読取専用・YAML非保存）に整理し、
  `--property-schema-regression`相当のschema/setter検証コードへRope/Motor/Tool/BaseCube派生の確認を追加した。
- MeshCubeと二体PhysicsConstraint群のcloneも登録schemaの`cloneFields()`を通すようにし、clone対象のメタデータを
  手書きコピーと分離しないよう整理した。参照weak_ptrの再結合処理は既存のremap経路を維持する。
- 最終局所syntax checkはSeat/Tool/制約群/PropertyRegistryとHumanoid/Replicationで成功。指定Release buildの再試行も
  同じ`WinError 2`でconfigure前に停止したため、Task 1/2のruntime確認は未完了。

## 2026-09-13: Directional Light shadow coverage boundary

- `shaders/fragment.glsl` の shadow 判定を修正。`fragPosLightSpace.w <= 0`、投影後 depth の
  `[0,1]` 外を影なしとして扱い、3x3 PCF の各 sample UV も明示的に範囲内だけ sample するようにした。
  shadow map 外の border 値に判定を依存しない。
- 調査の結果、sampler は `GL_CLAMP_TO_BORDER`、`GL_COMPARE_NONE`、border depth `1.0` であり、
  範囲外 sample を暗い影にする設定ではなかった。主因は `±80` の固定 orthographic coverage の端が
  地面へ直線境界として現れることと判断した。
- `src/Core/Renderer.cpp` の Directional Light shadow projection をカメラ位置中心・カメラ向き非依存のまま
  `±160`、light depth `800` へ拡大。`doc/Rendering.md` の記述も更新した。
- `git diff --check` は成功。`cmd.exe /d /c py build.py build` はコンパイル前に
  `UtilBindVsockAnyPort: socket failed 1` で停止し、Windows Release build と実機の視覚確認は未検証。
## 2026-09-13: ControlModeとCharacterHoverの責務分離

- `Humanoid::updatePhysicsState()`を追加し、接地raycast、CharacterHoverForce、Truss中の重力設定を
  `move()`から分離した。UserのFree/Character/Programに関係なく、PlayerCharacterの物理状態を毎入力フレーム更新する。
- ControlModeをprivate保持へ変更し、`getControlMode()`/`setControlMode()`を追加した。User、SceneRuntime、SceneLoader、Luau、Editor、起動停止処理、テストの直接代入をaccessor経由へ統一した。
- CharacterHover回帰へFree中の接地hover維持と、jump後Freeへ切り替えた際のhover抑制継続・着地復帰を追加した。NPCの`moveToward()`後も物理状態更新を行う。
- `spec.md`と`doc/Instances/Humanoid.md`へ、操作モードと物理更新が独立し、Free/Programでも重力・衝突・CharacterHoverForce・LiquidCube浮力を維持する仕様を追記した。
- `git diff --check`は成功。指定Release buildはconfigure時の`WinError 2`、既存buildのtarget buildはWSL/Windows CMake cacheと`Visual Studio 18 2026` generatorの不一致で停止したため、Windows buildと`--character-hover-regression`/`--user-input-controls-regression`は未実施。
- 次の一手: Windows側でRelease buildを再実行し、対象2回帰を実行する。失敗時はmode切替回帰とhover状態ログを確認する。
## 2026-09-13: Free切替時のCharacter水平速度停止

- Character入力中にFreeへ切り替えると、既存R6 bodyのX/Z速度が残って移動し続けるため、Free/Program遷移時に全bodyの水平速度だけをzero化する処理を追加した。Y速度、hover、jump抑制は維持する。
- Lキーによる同一フレーム切替と、外部setterによるフレーム間切替の両方を`User::processInput()`で処理する。
- `Humanoid::stopHorizontalMovement()`を追加した。ビルド・回帰は未実施。
## 2026-09-13: Free切替時のCharacter角速度停止

- Character操作中にFreeへ切り替えるとGyro・接触等で設定された角速度が残り、キャラクターが回り続けるため、既存のFree/Program遷移停止処理で全R6 bodyの角速度もzero化するよう修正した。
- 垂直速度、hover、jump抑制状態は維持する。CharacterHover回帰へ角速度停止確認を追加した。
- ビルド・回帰は未実施。次の一手はWindows Release build後に`--character-hover-regression`と`--user-input-controls-regression`を実行すること。
## 2026-09-13: Physics角速度取得APIの追加

- `Physics::getAngularVelocity(const BaseCube&)`を追加し、`IPhysicsBackend`、Box3D backendから汎用取得できるようにした。
- Box3Dはnative body値を返し、backend既定実装はゼロ値。PhysXレガシーコードは変更していない。
- `git diff --check`は成功。Release buildと対象回帰テストは未実施。
## 2026-09-13: Free切替後のYawForce再適用停止

- native角速度をzero化しても、Box3D fixed stepの`MaintainVelocity`処理とCharacter専用YawForce再適用で回転が再発していた。
- Free/Program遷移時に`YawForce`のValueをzero化しEnabled=falseへ変更し、Characterへ戻った後は既存`Humanoid::move()`で再有効化するよう修正した。
- CharacterHover回帰へYawForce無効化確認を追加した。ビルド・回帰は未実施。

## 2026-09-13: Humanoid HipHeight

- 固定 `CharacterGroundHeightSettings::targetDistance` が `Humanoid::updateGroundHover()` のRoot-to-floor
  setpointとして使われ、初期Root距離が異なる場合にhover加速度でRootを動かしていたことを確認した。
  `CharacterRig`側にTorso/body geometryからRootを補正する処理は存在しない。SpawnLocationの同じ固定値利用は
  hoverとは別の初期配置用途だったため、明示HipHeight時だけその値を使い、未設定時はRoot自身の既存配置を維持する。
- `Humanoid::HipHeight`を追加し、未設定/明示設定をprivate stateで区別した。初回有効ground detectionではRootからの
  実測距離だけをHipHeightへ保存し、Root位置は補正しない。明示設定は自動初期化で上書きしない。
- `PropertyRegistry`へ条件付きYAML出力とclone/copy metadata hookを一般化し、HipHeightをgeneric float editor、
  YAML、clone、instance copy、Luau dispatchへ接続した。未設定かつ未測定のHipHeightは保存せず、明示値または
  groundから自動初期化された値は保存する。
- `spec.md`と`doc/Instances/Humanoid.md`を新しいRoot基準の仕様へ更新し、property schema/hover回帰へ未設定初期距離、
  明示値変更、YAML、clone/copyの確認を追加した。RootJoint、Motor6D、Gyro、YawForce、full-body yaw、timestepは変更していない。
- 対象4 translation unitのWSL `g++ -std=c++23 -fsyntax-only -I. -Iinclude`は成功した。指定Release buildは
  コンパイル前のconfigureでWindows側 `FileNotFoundError: [WinError 2]`により停止し、runtime回帰は未実施。
- 次の一手: Windows側のconfigure/build環境を復旧後、`--character-hover-regression`、
  `--property-schema-regression`、spawn/scene YAMLの実機回帰と、Root/Torso同一初期Yの手動確認を行う。

## 2026-09-13: HipHeight接地/初期Root補正の修正

- `isGrounded`の`floor.distance <= landingCaptureDistance`という絶対距離判定を廃止し、
  `HipHeight`近傍かつRootの垂直速度がsettled threshold内の場合だけ接地とした。Jump開始時は即座に
  `isGrounded=false`へ遷移し、上昇中の再Jumpを許可しない。
- HipHeightが高いCharacterでもhover/landingを行えるよう、raycast範囲を`HipHeight + landingCaptureDistance`
  まで拡張した。`landingCaptureDistance`は目標からの着地許容幅として残し、最大検出距離の用途とは分離した。
- 未設定HipHeightの`User::placeCharacterAtSpawn()`ではSpawnLocation選択時にRootの初期Yを変更しないようにした。
  明示HipHeightの場合だけ指定距離へ配置し、SpawnLocationがない場合も現在Root CFrameを維持する。
- SpawnLocation/CharacterHover回帰の期待値と仕様文書を新しいRoot基準へ更新した。`include/windows26.h`の既存ユーザー変更は
  変更せず保持している。
- 対象C++ translation unitのsyntax checkは再度成功。Release buildとruntime回帰は前段のWindows configure
  `FileNotFoundError: [WinError 2]`で未実施。
- 次の一手: Windows build復旧後、特にHipHeight高値でのjump/landing、SpawnLocationなし・ありの初期Root Y保持、
  明示HipHeightのspawn配置を実機確認する。

## 2026-09-13: RootJoint bind不整合と接地状態の安定化

- `triangle.rcbn` の Root/Torso が同じYなのに RootJoint C0.y が `0.749998` だったため、C0.yを0へ修正した。
  Root/Torsoの同一中心をデフォルト生成R6のbind poseにも反映し、既存の実配置からのC0算出で同じアンカーになるようにした。
- `isGrounded` はHipHeightの距離誤差を毎フレームの判定条件にせず、HipHeight captureで接地状態へ入り、
  その後は床の微小距離揺れで反転しない。床が消えるかRootが上昇した場合に解除する。
- RootJoint spring、Yaw/Gyro、HoverForceの構造は変更していない。対象C++のsyntax checkと`git diff --check`は成功。
- Release buildはconfigure前のWindows側`FileNotFoundError: [WinError 2]`で停止し、runtime回帰は未実施。

## 2026-09-13: 接地判定診断ログ追加

- 挙動を変更せず、`Humanoid::updateGroundHover()`にRoot/Humanoid pointerとfull path、Root Y/垂直速度、HipHeightと初期化状態、raycast最大距離、hit距離/Y/法線、hit Instance、接地状態の前後、jump suppression、HoverForceの実効状態/値を1行で記録する診断ログを追加した。
- Box3DのRootJoint生成時に、Part0/Part1、body ID、C0/C1、実world anchor位置と差分、spring設定を記録する診断ログを追加した。RootJointやground hoverの処理内容は変更していない。
- `git diff --check`は成功。指定Release buildはconfigure前のWindows側`FileNotFoundError: [WinError 2]`で停止し、runtimeログの取得は未実施。
- 今回追加した`Humanoid.cpp`と`Box3DPhysicsBackend.cpp`のWSL `g++ -std=c++23 -fsyntax-only`は成功（Box3D include pathを指定）。
- 次の一手: Windows build復旧後、`CharacterGroundDebug`の`hasFloor`/`floorDistance`/`floorNormalY`/`rootLinearVelocityY`/`groundedBefore/After`と`CharacterJointDebug`のanchor差分を時系列で採取する。

## 2026-09-13: Root footprint shape castによる接地判定

- `Humanoid::updateGroundHover()`のRoot中心raycastを、Root下端直下に配置した薄いRoot footprint boxの真下shape castへ置換した。XZはRoot.Size、回転はRoot CFrameを使い、cast距離は既存の最大検出距離とHipHeight capture距離の大きい方を維持する。
- Box3D `b3World_CastShape()` callbackは既存の親子self除外を再利用し、query filterが無効なshape、CanCollide=false、query開始位置以上のhit、上向きでないnormalを除外する。複数候補は最も近い床面を採用する。
- floorDistanceはshape castのtravel距離を使わず、Root中心Yと採用surface hit pointのY差で算出する。grounded解除から上昇速度条件を削除し、速度はHoverの減衰およびJump suppressionのlanding判定だけに残した。
- `CharacterGroundDebug`はshape-cast-box、cast距離/travel距離、full normal、hit instanceを記録し、同一physics tickでの重複出力を抑制した。CharacterHover回帰に正のRoot Y速度で接地維持、および中心rayを外すedge platformのfootprint検出を追加した。
- Game側4 translation unitのWSL syntax checkと`git diff --check`は成功。`test_main.cpp`のsyntax checkはWSL環境に`GL/glu.h`がないためinclude段階で未実施。Release buildはconfigure前のWindows側`FileNotFoundError: [WinError 2]`で停止し、実機/回帰実行は未実施。
- 次の一手: Windows buildを復旧後、`--character-hover-regression`と広い床・edge platform・壁際・段差・Jump landingの実機`brun`を実行する。

## 2026-09-14: ジャンプ時の肩回転経路の固定

- `prompt.md`のCharacter Rig v2ジャンプ姿勢を調査し、`Humanoid::applyBodyAnimation()`が左右肩へ厳密な`+180°`を毎フレーム渡していたことを確認した。`+180°`/`-180°`は同じ物理姿勢でQuaternionの半回転境界が経路を一意に表現できず、さらに歩行中の現在角が負側でも固定`+180°`へ遷移していたため、Motor6Dの球面補間が反対側へ回る余地があった。
- ジャンプ肩ターゲットだけを左右共通の`+179°`へ変更した。半回転境界を避け、左右の肩が同じ符号のローカルX方向へ回転する。RootJoint、他Motor6D、物理構造、yaw、Hover、接地判定は変更していない。
- 既存のAnimationClip回帰へ、ニュートラル状態と左右が逆符号の角度からジャンプへ遷移しても、肩ターゲットが同符号になる確認を追加した。
- `Humanoid.cpp`、`CharacterRig.cpp`、`AnimationClip.cpp`のWSL構文チェックは成功。`test_main.cpp`はWSL環境に`GL/glu.h`がないため構文チェック未実施。指定Release buildはconfigure前のWindows側`FileNotFoundError: [WinError 2]`で停止し、回帰テストおよび実機`brun`は未実施。
- 次の一手: Windows build復旧後、既存Animation回帰とJump→landingを実機で確認し、左右腕が常に意図した前方経路を通ることを確認する。

## 2026-09-14: ジャンプ肩角度のスカラー状態化

- `+179°`の境界回避と、肩ごとに`Motor6D::Transform.Rotation.toEuler()`から符号を読み直す処理を削除した。
- `Humanoid`に左右共通の`m_jumpShoulderAngle`を追加し、`applyBodyAnimation()`内でdeltaTimeに基づき、airborne中は`angle += rate * dt`、grounded復帰後は`angle -= rate * dt`として`0..180°`へ進めるようにした。180°到達中および着地後に0°へ戻るまで、同じスカラーから両肩のMotor6D Transformを生成する。
- `move()`、Animation fallback、remote avatar pose適用から実deltaTimeを渡すようにし、直接呼び出しには既定の1/60秒を残した。full-body yaw、Hover、HipHeight、接地、Root lock、Motor6D物理構造は変更していない。
- Animation回帰へ、ジャンプ開始から180°到達、左右同一角度、着地後の負方向復帰を追加した。
- 対象ゲームTUのWSL構文チェックと`git diff --check`は成功。`test_main.cpp`はWSL環境の`GL/glu.h`不足で構文チェック未実施。Release buildはconfigure前のWindows側`FileNotFoundError: [WinError 2]`で停止し、回帰テストと実機`brun`は未実施。
- 次の一手: Windows build復旧後、Animation回帰とJump→landingを`brun`で確認する。

## 2026-09-14: Lighting shadow distance と Custom PostEffect

- `Lighting`へ`ShadowDistance`（160）と`ShadowFadeDistance`（20）をPropertyRegistry経由で追加し、Directional shadow projectionとfragment shaderの3D camera-distance fadeへ接続した。ShadowDistanceが0以下ならshadow passを実行せず、不正な正射影を作らない。
- `PostEffectKind::Custom`と`FragmentShaderFile`を追加した。PostEffectの保存・Properties editor・Luau dispatchはschema-drivenへ統一した。
- Rendererは既存fullscreen vertex shaderと外部fragment shaderをlinkし、パスとmtimeごとに成功programをcacheする。失敗時は既存成功programを維持し、初回失敗時はpass-through。コンパイル診断は`RCBN_ERROR`でConsoleへ出力する。
- Packagerが`FragmentShaderFile`を収集、同梱、パス書換えする。property schemaとpackage asset pathの回帰ケースを追加し、関連文書を更新した。
- `Lighting.cpp`、`PostEffect.cpp`、`Packager.cpp`のWSL syntax checkと`git diff --check`は成功。Release buildはconfigure前にWindows側`FileNotFoundError: [WinError 2]`で停止。Renderer/Editor/SceneLoader/Luau/test TUのWSL syntax checkは環境の`GL/glu.h`不足で未実施、runtime回帰も未実施。
- 次の一手: Windows build環境を復旧後、`--property-schema-regression`と`--asset-path-regression`を実行し、Custom shaderの成功・compile failure・mtime reload、shadow fadeを実機確認する。

## 2026-09-14: FilePath editor layout

- generic `drawFilePathField()` の入力幅をProperties panelの幅へ上限420px付きで収め、長いパスでパネル幅が拡張されないようにした。編集バッファは4096 byteへ拡張し、hoverで完全なパスを表示する。
- 既存のschema-driven file property共通の `Browse`（参照）ボタンをそのまま使用するため、`PostEffect.FragmentShaderFile`にも個別UIを追加せず参照・Undo・project-relative path化を統一した。
- `git diff --check` は成功。Windows Release buildおよびUI実機確認は既存のWindows configure `WinError 2`により未実施。

## 2026-09-14: FilePath参照ボタンの行分離

- generic `drawFilePathField()` のFragmentShaderFile等で、入力欄を単独行（最大640px）にし、`Browse`/`Clear`ボタンを次行へ移動した。長いパスでも操作ボタンと入力欄が横幅を奪い合わず、hover時の完全パス表示も維持する。
- `git diff --check` は成功。UI実機確認は未実施。

## 2026-09-14: Properties schema-driven migration

- `prompt.md`に従い、PropertyRegistryへ実行時適用schemaのYAML load/save入口、schemaのYAML serialize/deserialize callback、Lua hidden/editor read-only/multi-only metadataを追加した。Luau dispatchも基底→派生schemaを走査するよう統一した。
- SpatialのPosition/Size/Rotation/CFrame、BaseCubeの通常propertyとLockFlags、Workspace、System、User、Terrain、Script、Sound、Decal、Texture、SurfaceMark、Weather、GUI画像/FilePath、PhysicalFileの通常propertyをschema経由へ寄せた。LockFlagsの既存YAML配列表現とScript.Path→ContentPath、SurfaceMark.TexturePath→Textureのaliasは維持した。
- SceneLoaderの通常property手書き保存を`saveApplicableProperties()`へ置換し、読込も`loadApplicableProperty()`を先に通すよう変更した。BaseCube/Script/Decal/Texture/SurfaceMark/Terrain/Sound/Spatial等の通常保存分岐を削減した。
- PropertiesPanelの通常rendererをschema集合の共通rendererへ統一し、FilePath/InstanceReference/Enum/Bool/Int/Float/String/Vector/Color/CFrame/Quaternionとmulti-editをdescriptor metadataから処理するようにした。旧multi-transform、旧Vec3 renderer、旧constraint参照renderer、User通常propertyの専用commandを削除した。
- cloneは派生BaseCubeのschema二重適用を避け、各cloneでschemaを一度だけ適用するよう整理した。Script metadataは`copyStateWith`へ移し、replacementも`collectApplicableSchema()`同士で互換propertyをコピーするよう変更した。Spatial Size/Decal Mode/UV/transform commandの変更はsetter経由へ寄せた。
- 残るPropertiesPanelのclass-specific処理は、SurfaceMark.FilterInstances、MeshCube UV再生成/Decal配置、Sound再生操作、Script restart/open、Decal親依存UI、User.CursorImages、Terrain folder/regenerate/randomize、Skybox faces、Humanoid animation references、AnimationClip status、GUI Font adapter、ObjectValueなどのspecial adapter/actionに限定した。これらは通常property rendererへ混ぜていない。
- 対象変更translation unitはWSL `g++ -std=c++23 -DGLEW_NO_GLU -fsyntax-only`で検査成功。`git diff --check`も成功。指定Release buildは`cmd.exe /d /c py build.py build`がcmakeをWindows PATHから起動できず`WinError 2`で停止したため、リンク済みbuildとruntime回帰は未実施。既存の`src/Core/Box3DPhysicsBackend.cpp`ユーザー変更は保持した。
- 次の一手: Windows側cmake環境を復旧し、schema/YAML round-trip、clone/replacement、multi-edit、Luau dispatch、FilePath/InstanceReferenceの既存回帰とUI実機確認を実行する。

## 2026-09-14: テクスチャ alpha の描画修正

- stb_image は従来どおり要求出力4チャンネル、OpenGL upload も `GL_RGBA`/`GL_RGBA` で、ロード時のalpha欠落は確認されなかった。
- sampled `texColor.a` は最終 `FragColor.a` ではなく、Cube色と画像RGBを合成する係数として扱う。これにより透明画素では背後のシーンではなく親Cubeの色が残る。
- `Texture` の面描画は親Cubeの `Color` を合成先とし、Textureの `Color` は画像RGBとalphaのtintとして適用するよう修正した。`Decal`も同じtint alphaを合成係数へ反映する。
- BaseCube描画開始時にtexture tint uniformを既定値へ戻し、直前のCube faceの値が他の描画クラスへ漏れないようにした。デフォルト白テクスチャのalphaは0のまま維持する。
- `git diff --check` は成功。WSLの構文チェックは環境に `GL/glu.h` がなく未実施。指定Release buildはWindows側 `WinError 2`、`brun` はWSL vsock エラーで未実施。

## 2026-09-14: User.ControlMode の YAML enum 文字列表現修正

- `triangle.rcbn` の `User.Properties.ControlMode: Free` が `yaml-cpp ... bad conversion` になる原因を調査した。`User` schema の `ControlMode` が `PropType::Enum` でありながら `yamlEnumAsString` 未設定のため、`PropertyRegistry::valueFromYaml()` が `n.as<int>()` を実行していた。
- `User.ControlMode` schema に `yamlEnumAsString = true` を設定し、`Free` / `Character` / `Program` の保存・読込を文字列表現へ統一した。
- DefaultCameraMode 回帰へ User.ControlMode の YAML string save/load round-trip 確認を追加した。
- `User.cpp` と `test_main.cpp` の `g++ -fsyntax-only`、`git diff --check` は成功。Windows Release build は CMake が Windows PATH にないため configure 前の `WinError 2` で停止し、リンク済み回帰実行は未実施。既存の `Box3DPhysicsBackend.cpp` ユーザー変更は保持した。

## 2026-09-14: RootJoint 分離と schema 回帰の修正

- 物理駆動キャラクターの Root/Torso 分離を調査し、`Box3DPhysicsBackend::createMotor6D()` のRootJoint診断分岐に、同一body用だった`return`だけが残っていたことを確認した。RootJointが実Box3D joint生成へ到達しない不具合を修正し、`idsEqual(bodyA, bodyB)`の場合だけlogical constraintとして終了する従来の制御へ戻した。実機でのキャラクター動作は正常であることを確認済み。
- schema 保存前に`PhysicsConstraint::refreshRefNames()`を実行するよう修正し、rename後のWeld/Motor等が古い参照パスをsceneへ保存する問題を修正した。
- Animation embedded sceneの`PartName`/`JointName`両形式を受理し、旧`Animation: { Tracks: ... }`YAMLを`Animation::importFromFile()`で読む互換adapterを追加した。
- 不明な文字列enumはPropertyRegistryで警告して現在値を保持するようにした。ShadowModeだけは既存仕様をschema adapterとして明示し、未知値を`Normal`へ戻す。
- Seatのlive input `Steer`/`Throttle`を`noClone()`化した。Decal/TextureのFace/Modeがschema上`Enum`であること、Root高さ2の床上中心Yがおよそ1であること、SpawnLocationなしではauthored Root poseを保持することに合わせ、回帰の期待値を更新した。
- `PropertyRegistry.cpp`、`SceneLoader.cpp`、`Animation.cpp`、`Seat.cpp`、`BaseCube.cpp`のWSL syntax checkと`git diff --check`は成功。Windows側Release buildはこのWSL環境ではCMakeがPATHに無く`WinError 2`で停止したが、ユーザー側では実機動作正常を確認済み。
- 次の一手: Windows環境で必要になった場合に`--animation-clip-regression`、`--property-schema-regression`、`--shadow-mode-regression`、`--starter-weld-rename-regression`を再実行する。残るGyro/Motor/collision-filter回帰はschema修正とは別のBox3D物理課題として扱う。

## 2026-09-14: Directional shadow PCF stability

- 既存の 2048×2048 shadow map を監査し、深度テクスチャを明示的な 24-bit depth + `GL_NEAREST` に変更した。PCFは補間済み深度ではなく、`texelFetch`で有効範囲内の最近傍深度を3×3（最大9回）比較する手動PCFへ整理した。
- Directional shadow projectionはライト空間のright/up基底でカメラ中心を求め、`2*ShadowDistance/2048` world unitsのtexel gridへsnapしてからview matrixを構築するようにした。ライト方向の変更は毎フレーム基底を再計算して反映する。
- 固定800だったライト空間depth rangeを`0.1..(2*ShadowDistance+32)`へ縮小し、light cameraを`ShadowDistance+16`離した。これにより既定ShadowDistance=160で深度精度を改善し、近距離casterのnear-plane clippingに余裕を残した。
- biasは`max(0.00035, 0.0012*(1-clamp(dot(normal, lightDir),0,1)))`へ変更。無効なLighting.Directionは警告して既定方向へフォールバックし、main/shadow passで同じ正規化方向を使う。ShadowDistance/ShadowFadeDistanceの既存距離fade接続は維持した。
- `git diff --check`と`g++ -std=c++23 -DGLEW_NO_GLU -fsyntax-only -I. -Iinclude src/Core/Renderer.cpp`は成功。`cmd.exe /d /c py build.py build`はCMakeがWindows PATHに無く`WinError 2`でconfigure前に停止したため、Release build / `brun` / 実機での斜めedge・移動shimmer・距離fade確認は未実施。
- 次の一手: Windows側で`cmd.exe /d /c py build.py build`を再試行し、可能なら`brun`で長い斜めedge、cube、細いobject、水平/斜面、カメラ移動/回転、light direction変更、ShadowDistance/ShadowFadeDistanceを確認する。
## 2026-09-14: Shadow acne diagnosis and caster offset

- `shadowCalc()`を再監査し、main passで`normalize(Normal)`/`normalize(-lightDir)`済みであること、depth passのLookAtが`lightDir`（light→surface）方向、shaderの`-lightDir`がsurface→lightであることを確認した。shadowCalc内でもNormal/Lightを明示normalizeして呼び出し側依存を除いた。
- 広い平面へ広範囲に出るacneの主因として、shadow map生成passに`GL_POLYGON_OFFSET_FILL`が無く、rasterizerの面勾配を深度書き込み側で補償していなかった点を特定した。`glPolygonOffset(1.0, 1.0)`をshadow passだけに適用し、pass後に無効化する。
- shadow pass開始時に`GL_DEPTH_TEST`、`GL_LESS`、`GL_TRUE`を明示し、前の半透明描画でdepth writeが無効化された状態やstale depth clearを防止する。受け側biasの式・値、PCF、texel snapping、ShadowDistance/ShadowFadeDistanceは維持した。
- receiver-plane `fwidth` biasは今回は追加していない。まず正規化、caster-side polygon offset、既存slope biasで原因を分離し、過剰biasによるpeter-panningを避けるためである。
- `g++ -std=c++23 -DGLEW_NO_GLU -fsyntax-only -I. -Iinclude src/Core/Renderer.cpp`と`git diff --check`は成功。`cmd.exe /d /c py build.py build`は今回もCMakeがWindows PATHに無く`WinError 2`でconfigure前に停止したため、新バイナリの`brun`と実機でのacne/peter-panning目視は未検証。

## 2026-09-14: Cascaded directional shadow map

- directional shadowを単一projectionから3 cascadeへ移行した。`ShadowDistance`を終端とするpractical split（linear/logarithmic混合、lambda=0.7）を毎フレーム計算し、既定160では実splitはおよそ16.8 / 41.6 / 160となる。
- shadow mapを2048×2048の`GL_TEXTURE_2D_ARRAY`（24-bit depth、3 layer）へ変更し、同じshadow caster sceneをlayer 0/1/2へ3回描画する。FBOは`glFramebufferTextureLayer`でcascadeを切り替え、既存のdepth test/write・`glPolygonOffset(1,1)`・caster判定を維持した。
- 各cascadeはカメラの実frustum sliceの8頂点をlight-spaceへ変換し、XY bounds + 8、depth bounds + 32のbounded marginでtight-fit orthographic projectionを構築する。light eyeをslice最小depthより32だけlight側へ置き、near=0.1 / far=range+64としてcaster marginをnear clipしない。XY中心はcascadeごとの`projectionWidth/2048`・`projectionHeight/2048`でsnapし、単一の広い`ShadowDistance`投影を使わない。
- vertex shaderはview-space depthを出力し、fragment shaderはその値でcascadeを選択する。split前後の約8%だけ隣接cascadeを追加で3x3 PCFし、境界をblendする。既存のnormalize、受け側slope bias、手動3x3 PCF、`ShadowDistance`/`ShadowFadeDistance` fade、light→surfaceの方向規約は維持した。
- `Renderer.cpp`のWSL syntax checkと`git diff --check`は成功。GLSL validatorはWSL環境に存在しなかった。Release buildは`cmd.exe /d /c py build.py build`がWindows側CMakeをPATHから起動できず`WinError 2`でconfigure前に停止したため、CSMを含む`brun`と実機での境界線・shimmer・acne/peter-panning目視は未実施。
- 次の一手: Windows側のCMake環境を復旧し、Release build後に近距離cube/character、長い斜めedge、cascade境界、遠距離ShadowDistance、fade、カメラ移動/回転、light direction変更、水平/斜面を`brun`で確認する。
- 2026-09-14: Character Ragdoll

  - `Humanoid`へ`Normal/Ragdoll`状態と`ImpactRagdollThreshold`（既定45）、`RagdollRecoverySpeed`（既定1.5）、
    `RagdollRecoveryDelay`（既定1.0）をschema経由で追加した。YAML、clone、PropertiesPanel、Luauは既存schema dispatchを利用する。
  - Box3Dのhit eventでcontact normal impulseを優先し、body massでstud/s相当へ換算した値を、同一physics update内のCubeごとの最大impactとして蓄積するAPIを追加した。impulseが無い場合はBox3Dのnormal approach speedを利用する。
  - impact閾値超過でmovement/jump/hover/full-body yaw/Gyro/Root AngularX・AngularZ lock/Motor6D姿勢制御を停止し、既存R6 BallSocketを有効化する。BallSocketは対応Motor6DのC0/C1をfallback anchorとして使い、別のCFrame補正やMotor6D作り直しは行わない。
  - Ragdoll中だけR6 body collisionを有効化し、character collision groupで内部self-collisionを抑制して外部world collisionを許可する。Root lockと元のcollision stateは復帰時に復元する。低線速度・低角速度・body support・delay成立で自動復帰し、位置teleportは行わない。
  - AvatarBatchのvisual flagsへRagdoll bitを追加し、Host権威のNormal/Ragdollをremote/localへ反映する。Ragdoll中のremote表示は受信Root poseで上書きしない。
  - `Humanoid.cpp`、`Box3DPhysicsBackend.cpp`、`Physics.cpp`、`Replication.cpp`と関連headerのWSL `g++ -std=c++23 -DGLEW_NO_GLU -fsyntax-only`は成功。`cmd.exe /d /c py build.py build`はWindows側CMake未配置による`WinError 2`でconfigure前に停止し、WSLからの既存Visual Studio buildもCMake cacheのWindows/WSLパス不一致で停止した。Release `RecubinTest`/`brun`/実機確認は未実施。
  - 次の一手: Windows側CMake/PATHを復旧後、`--humanoid-rig-collision-regression`、`--character-hover-regression`、Ragdoll impact→recovery限定回帰を実行し、通常jump着地・高所落下・壁衝突・動くCube衝突・坂転がり・復帰直後の移動/jump・remote stateを実機確認する。

## 2026-09-14: Hybrid Ragdoll recovery

- Ragdoll復帰を`Normal`へ直接遷移させず、`Normal`/`Ragdoll`/`Recovering`の3状態へ変更した。Ragdoll中の低線速度・低角速度・support・delay成立時はRecoveringへ入り、Recovering中はmovement/jump/hover/通常animationを停止する。
- Recovering開始時にBallSocketを無効化してMotor6DのTransformをbind poseへ戻し、Root lockとYawForceを無効化したまま、既存RootGyroのX/Y/Zを一時的に有効化する。X/Zは0度、YはRagdoll中の水平headingを目標にして物理的にuprightへ戻すため、BallSocketとMotor6Dの強い同時拘束を避ける。
- Rootのupright error、Pitch/Roll errorが3度以下、角速度が0.2以下の状態を0.1秒安定確認した後、現在位置と保持YawからPitch/Rollだけを除去したCFrameを一度だけ適用する。その直後にRoot角速度、Root lock、collision、Gyro/YawForce、hover、movement/jumpを順序どおり復元してNormalへ戻す。
- YawはForwardのXZ投影を優先し、投影不能時はRight、最後にRagdoll直前の有効Yawへfallbackする。倒立時にForward投影が約180度反転するケースは直前Yawを維持する。Recovering中の診断ログへstate、upright/pitch/roll error、angular speed、timer、final normalizationを追加した。
- `doc/Instances/Humanoid.md`と`spec.md`を3状態・ハイブリッド復帰仕様へ更新した。
- `Humanoid.cpp`、`Humanoid.hpp`、および既存Ragdoll関連実装のWSL `g++ -std=c++23 -DGLEW_NO_GLU -fsyntax-only`は成功。`git diff --check`は既存のCRLF変換警告のみで、Whitespace errorはなし。Release build、RecubinTest、brun、実機の14ケース確認は未実施。
- 現在の未解決事項: Windows側CMake/PATHが未復旧のためMSVCリンクとruntime確認ができない。次の一手はWindows Release build後、前後/左右/逆さま/坂/壁際/転がり/Recovering・Yaw維持・復帰直後の移動jump・flip/oscillationを実機確認すること。

## 2026-09-14: Ragdoll recovery speed fallback

- Gyroがuprightへ到達できずRecoveringが停滞するケースに、speed-fallbackを追加した。Recovering開始から1.0秒経過後、Root線速度が`RagdollRecoverySpeed`以下、角速度が0.5 rad/s以下、supportが存在する状態を0.1秒継続した場合、Gyro姿勢誤差に関係なく最終CFrame正規化へ進む。
- Fallbackでも現在位置と保持Yawを使い、Pitch/Rollのみを除去するCFrame適用は一度だけ。高速回転中・空中では適用せず、通常のgyro復帰経路と同じcontroller復帰順序を使用する。診断ログの最終化modeは`gyro`/`speed-fallback`で区別する。
- `Humanoid.cpp`のC++23 `g++ -fsyntax-only`は成功。`git diff --check`は既存CRLF変換警告のみでWhitespace errorなし。Release build、brun、実機再確認は未実施。
- 次の一手: Windows側CMake復旧後、画像のような静止横倒し、前後/左右倒れ、逆さま、坂・壁際、転がり後のspeed-fallback、復帰後の移動/jump、flip/oscillationを確認する。

## 2026-09-14: Practical recovery relaxation

- Gyro成功条件をupright/Pitch/Roll 10度以下、角速度1.0 rad/s以下へ緩和した。0.1秒安定後に`gyro-success`として最終化する。
- Recovering開始0.75秒後から、線速度3.0以下、角速度2.0 rad/s以下、接地0.15秒継続で`speed-fallback`を許可した。1.5秒経過後は、接地0.15秒継続していれば速度条件を問わず`timeout-fallback`を許可し、永久待機を防ぐ。
- 接地継続は`m_recoveryGroundedTime`で管理し、単一frameのRaycast結果を復帰条件にしない。最終CFrame処理とcontroller復帰順序は共通経路のまま維持した。
- `Humanoid.cpp`、`Replication.cpp`、`Physics.cpp`、`Box3DPhysicsBackend.cpp`をC++23 `g++ -fsyntax-only`で検査し成功。対象変更の`git diff --check`もexit 0。
- `cmd.exe /d /c py build.py build`はWindows CMake未配置の`WinError 2`でconfigure前に停止。`cmd.exe /d /c py build.py brun Release`もWSL vsock環境エラーで起動できず、brun/実機確認は未実施。

## 2026-09-14: Ragdoll recovery support height correction

- Recovering専用のsupport scanを追加した。Rootの現在YawでRootのX/Z footprint（各軸0.15 stud余白）を薄いboxとして、`RootY + max(0.25, HipHeight + 0.25)`から下向きにshape castする。cast長は想定Root高さとRoot高さの大きい方に1 studの下方余裕を加え、Character階層を除外する。
- shape castへ`minimumNormalY`引数を追加し、通常時ground detectionは従来の閾値を維持した。Recovering scanだけ`0.5`を要求して壁を除外し、Box3D callbackが複数候補から最も高い有効support面を選ぶ既存動作を利用する。
- Recovering中のsupportを0.15秒継続して検出し、最後の有効support Yを保存する。最終CFrameは`targetY = max(currentRootY, supportY + HipHeight)`で計算し、X/Z・Yawを維持したまま必要な上方向だけ補正する。scanが一時的に失敗しても最後の有効supportを使い、未取得時はエラーを記録して現在Yを維持する。
- 最終化ログへ`supportY`、`currentY`、`targetY`、`yCorrection`を追加した。既存の`gyro-success`/`speed-fallback`/`timeout-fallback` modeログとcontroller復帰順序は維持した。
- `Humanoid.cpp`、`Physics.cpp`、`Box3DPhysicsBackend.cpp`のGCC C++23構文検査と対象差分の`git diff --check`は成功。Windows Release buildはCMake未配置による`WinError 2`、`brun`はWSL vsockエラーのため未実施。前後/横倒れ、壁際、段差、坂、沈み込み、実機のめり込み・跳ね上がり確認は未実施。

## 2026-09-14: Practical Ragdoll recovery thresholds

- Recoveringのgyro成功条件をupright/Pitch/Roll 3度から10度、角速度0.2から1.0 rad/sへ緩和した。0.1秒の安定維持は継続し、成功ログは`gyro-success`を出す。
- speed-fallbackをRecovering開始0.75秒後から利用し、Root線速度3.0以下、角速度2.0 rad/s以下、接地継続0.15秒で最終CFrame正規化を一度だけ行う。接地は単一frameではなく専用timerで判定する。
- Recoveringが1.5秒続き、接地が0.15秒継続していれば、速度条件を満たさなくても`timeout-fallback`で最終CFrame正規化を行う。これによりRecoveringの永久待機を防止する。復帰順序とPosition/Yaw維持、Pitch/Rollのみ除去のCFrame処理は共通経路で維持した。
- `Humanoid.cpp`の対象TU構文検査を実行予定。Release build、brun、実機確認はWindows側CMake未配置のため未実施。

## 2026-09-14: Ragdoll recovery full-rig finalization diagnosis

- Rootだけを最終CFrame移動していたため、R6 bodyが兄弟として旧Ragdoll位置に残り、次の物理更新でMotor6Dが旧bodyとの拘束を再適用する構造を確認した。既存の`supportY=2.5`、`currentY=3.03805`、`targetY=5.5`ログはRootの移動だけを示しており、他bodyの追従を保証していなかった。
- `Humanoid`へfinalization前・全body整列直後・次のphysics update後の診断ログを追加した。Root/Torso/Head/LeftLeg/RightLegのposition/bottomY、support instance/Y/normal、HipHeight、各Motor6D/BallSocketのEnabled、Humanoid/Physics pointer、physics tick、updateAll invocationを記録する。
- Ragdoll開始時にR6 Motor6DのC0/C1からRoot基準のbind poseを保存する。finalization時はBallSocket無効化、Motor6D無効化、Rootと全bodyを`targetRootCFrame * bindPoseRelativeToRoot`へ一度だけ配置、全body速度ゼロ化、Motor6D再有効化、Root lock/collision/Gyro/YawForce/hover/Normalの順に復帰する。
- `targetRootY=max(currentRootY,supportY+HipHeight)`を基本値とし、bind poseの最下端がsupportY+0.02未満になる場合だけその差分を上方向へ追加補正する。Yawは保存済みRagdoll yaw/fallbackを維持する。
- `updateAll`のトップレベル invocation IDを追加し、固定timestepで同じphysics tickが続く場合の誤検出を避けて同一invocationの二重更新だけを警告する。ソース上、呼び出しはRecubin/RecubinEngine各1箇所で、同一実行ファイル内の二重呼び出しは確認できない。
- `Humanoid.cpp`、`Physics.cpp`、`Box3DPhysicsBackend.cpp`のGCC C++23 syntax checkは成功。`git diff --check`はexit 0。Windows `cmd.exe /d /c py build.py build`はCMake起動不能（WinError 2）、`py build.py brun Release`はWSL vsockエラーで未実行。実機でのbefore/after/nextログおよび14ケースのめり込み確認は未検証。
- 次の一手: Windows側で再build後、追加診断の3 phaseログを取得し、全bodyがbind poseへ移動したこと、次physics update後もbottomYがsupport以上を保つこと、同一`updateAllInvocation`の重複有無、前後/横/逆さ/坂/壁際/段差/転がり後を確認する。

## 2026-09-14: BallSocket per-axis angular limits

- `BallSocket`へ`AngularX/Y/ZMode`（`Free`/`Limited`/`Locked`）と各軸のMin/Max角度（度）を追加し、既定値は全軸Free、Min=-180、Max=180とした。値変更時は既存native jointを再生成し、invalid mode/非有限角度はfull path付きwarningで保持値を維持する。
- Box3D spherical jointへlocal joint frameの相対quaternion軸別constraintを追加した。Limitedはlower/upperの一方向impulse、Lockedは0度のbilateral impulseとしてsolverで解き、毎frameのEuler角clampは行わない。constraint torque/errorとrecording replayにも統合した。
- BallSocket生成時はAttachmentのlocal frame、R6 ragdollのAttachmentなし経路はMotor6D C0/C1 bind frameをBox3Dへ渡す。制限軸はこのframe基準で評価し、`collideConnected=true`にしてBallSocketがcollisionを抑止しないようにした。collision無効化はNoCollision側の責務のまま維持した。
- schemaへ9プロパティを登録し、generic Properties/YAML/clone/Luau経路へ統合した。PropertiesPanel固有分岐は追加していない。R6のNeck/Shoulder/Hipへ有限な初期制限を設定し、復帰時は既存順序でBallSocketを先に無効化してMotor6Dと競合しない。
- `BallSocket` schema回帰、Box3D C/C++ syntax check、Box3D単体CMake build、Box3D全unit tests（local frame基準のX Limited/Y Lockedを含むAll Box3D tests passed）、対象差分の`git diff --check`を成功。runtime変更時にsleep中bodyも再評価できるよう新APIでwakeする。
- Windows `cmd.exe /d /c py build.py brun Release`は`UtilBindVsockAnyPort:309: socket failed 1`で起動できず、Recubin実機のRagdoll/NoCollision/Character方向別確認は未検証。次の一手はWindows側再build後、Neck/Shoulder/Hipのlimit到達時のsolver安定性、向き変更、collisionペア、Ragdoll復帰を実機で確認する。

## 2026-09-14: Relaxed Ragdoll recovery fallback

- Ragdoll入口の既定Root線速度上限を2.5 stud/s、角速度上限を2.0 rad/sへ緩和し、低速タイマーはsupport scanの失敗だけではリセットしないようにした。supportが無い場合も`RagdollRecoveryDelay`後さらに0.75秒の猶予で`Recovering`へ進めるため、空中で静止したケースが永久にRagdollへ残らない。明示された`RagdollRecoverySpeed`値はそのまま尊重する。
- Recoveringのgyro成功条件をupright/Pitch/Roll 15度以下、角速度1.5 rad/s以下へ緩和した。speed-fallbackは開始0.5秒後、線速度4.0以下・角速度2.5 rad/s以下でsupportなしでも実行でき、timeout-fallbackは1.25秒後にsupport・速度条件なしで実行する。
- support scanが得られた場合は従来どおり最終Root Yの補正へ使い、得られない場合は現在Root Yを維持する。fallbackを許可したことはwarningで観測可能にした。BallSocket/NoCollision、Motor6D復帰順序、全身bind pose正規化は変更していない。
- `spec.md`と`doc/Instances/Humanoid.md`へ新しい復帰条件を反映した。`Humanoid.cpp`のGCC C++23構文検査と`git diff --check`は成功。`cmd.exe /d /c py build.py brun Release`はWSL vsockの`UtilBindVsockAnyPort:309: socket failed 1`で起動できず、実機の復帰挙動は未検証。

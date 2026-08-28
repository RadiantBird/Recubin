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

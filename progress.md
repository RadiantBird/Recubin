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

## 2026-09-15: Explorer hierarchy sorting

- Explorerの各親直下の表示を共通ソートへ変更した。`Workspace`、`Folder`、`Model`、`Script`、`LocalScript`、`ModuleScript`、
  `PhysicalFileInstance`系、`ValueBase`系、`BaseCube`系、その他の優先順位でグループ化し、同じ具体クラス内の名前はASCII自然順で比較する。
  数字列は数値比較するため`Cube1`、`Cube2`、`Cube10`の順になる。未定義クラスは最後尾でクラス名を決定キーにする。
- `SceneHierarchyPanel`の表示と`SceneHierarchySelection::collectDirectChildren`を同じソート経路へ統一した。Shift範囲選択と直下子選択の順序もExplorer表示と一致する。
- 名前変更後は既存のInstanceポインタ／ImGui IDを維持したまま子配列を再収集するため、表示順だけが更新され、選択状態・展開状態・所有関係は失われない。
- `src/Editor/SceneHierarchySelection.cpp`、`src/Editor/SceneHierarchyPanel.cpp`、`src/test_main.cpp`のGCC C++23構文検査と`git diff --check`は成功。並び順、自然数値順、rename後の再ソート回帰を追加した。
- Windows正式Release buildは`cmd.exe /d /c py build.py build`がCMakeをPATHから起動できず`WinError 2`でconfigure前に停止した。既存`build/Release/RecubinTest.exe`は2026-09-14生成で変更より古いため、追加回帰は未実行。
- 次の一手: Windows側CMake/PATH復旧後に再buildし、`--scene-hierarchy-grouping-regression`でExplorerソート回帰を実行する。ImGui実機で名前変更後の選択・展開状態も確認する。

## 2026-09-15: BallSocket connected collision filter fix

- 原因はBallSocketの`collideConnected`設定ではなく、Character Modelの`m_characterCollisionGroup`をBox3D nativeの負の`groupIndex`へ変換していたことだった。Box3Dはshape filterをjointの`collideConnected`判定より先に評価するため、同じCharacter内のBallSocket接続pairも常時除外されていた。
- Character groupのself-collision判定をnative groupIndexからBox3D custom filterへ移し、BallSocket接続pairは通すようにした。NoCollision snapshotは先に評価するため、明示的なNoCollisionはBallSocketより優先される。CanCollide=falseのcategory/mask除外は維持した。
- Character collision regressionへBallSocket接続pairの衝突確認を追加し、従来の未接続self-collision抑制、reparent後の外部衝突、NoCollisionの責務分離を維持する設計にした。
- `Box3DPhysicsBackend.cpp`（`-Itemp_libs/box3d/include`指定）、`test_main.cpp`のGCC C++23構文検査と対象差分の`git diff --check`は成功。`brun Release`はWSLの`UtilBindVsockAnyPort:309: socket failed 1`で起動できず、更新後の回帰実行と実機接触確認は未実施。

## 2026-09-15: BallSocket chain cross-collision fix

- 追加調査で、BallSocket直結pair以外の同一Character bodyが、Character collision groupのcustom filterで拒否されていることを特定した。BallSocketに管理される両bodyは、異なるBallSocket chain間でもcustom filterを通すようにした。
- NoCollision snapshotは引き続き最初に評価するため、明示的なNoCollisionはBallSocket chain間の許可より優先する。通常Characterの未管理body間self-collision抑制、Character外のcollision、native groupIndex=0、BallSocketの`collideConnected=true`は維持した。
- Character collision regressionへ、同一Ragdoll内の別BallSocket pair間のcontact確認を追加した。`Box3DPhysicsBackend.cpp`と`test_main.cpp`のGCC C++23構文検査、対象差分の`git diff --check`は成功。Windows `brun Release`はWSLのvsockエラーで起動できず、実機接触・NoCollision優先は未検証。

## 2026-09-15: Weather cloud color and fixed world height

- `Weather`へ`CloudColor`（`Color4`、既定値`0.92/0.93/0.95/1.0`）を追加し、PropertyRegistry経由でProperties、YAML、clone、Luauへ接続した。雲のアルファは`CloudDensity * CloudColor.a`とする。
- `CloudHeight`をカメラ基準ではなくワールド空間のY座標として扱うよう変更した。雲の水平クアッドは従来どおりカメラのX/Zを追従するが、Yは`CloudHeight`から変化しない。
- `doc/Instances/Weather.md`と`spec.md`へColor4とワールド高度の仕様を追記し、PropertySchema回帰へWeatherのColor4/高度のYAML・clone検証を追加した。
- `Weather.cpp`、`Renderer.cpp`、`test_main.cpp`のGCC C++23構文検査と対象差分の`git diff --check`は成功。Windows Release build、RecubinTest実行、Rendererの実機描画確認は未実施。
- 次の一手: Windows側のCMake/PATH復旧後に再buildし、`--property-schema-regression`でWeatherの回帰を実行し、カメラY移動時の固定高度・CloudColor/alphaを実機確認する。

## 2026-09-15: Character Truss climbing states and full-rig gravity

- 新しい複数body Character RigでTrussがRootだけの旧制御に依存していたため、Humanoidへ`ClimbingUp`/`ClimbingDown`状態を追加した。昇降状態はRagdoll/Recoveringとは別扱いにし、Ragdoll判定・死亡遷移・replication判定が昇降状態を誤認しないようにした。
- Truss接触中は`collectCharacterBodies()`で得られるRoot/Torso/Head/両腕/両脚の全dynamic bodyについて重力を無効化する。各bodyへ`CharacterClimbForce`を作成し、`MaintainVelocity`で水平ストレイフ速度と`ClimbSpeed`の上昇/下降速度を適用する。垂直入力がない場合はTruss上で速度0を維持する。
- Trussから離れたら昇降Forceを無効化し、全bodyの重力を復帰して既存GroundHeight hoverへ戻る。Motor6Dや既存のRoot yaw制御の構造は変更していない。
- `Humanoid.cpp`のGCC C++23構文検査と`git diff --check`は終了コード0。Windows正式Release buildはCMake実行ファイルがPATHに無く`WinError 2`でconfigure前に停止したため、RecubinTest/brun/実機でのTruss昇降確認は未実施。
- 次の一手: Windows側CMake/PATH復旧後、垂直TrussでW/Sの上昇下降、入力停止時の停止、A/Dストレイフ、Truss離脱後の重力復帰、坂・壁際・Ragdoll衝突、各bodyの速度と重力設定を確認する。

## 2026-09-15: Jump escape from Truss climbing

- `Humanoid::jump()`をClimbingUp/ClimbingDown中も受け付けるようにし、接地判定を要求せず全R6 bodyへ通常のJumpPowerを適用する。成功時は`Normal`へ遷移し、`CharacterClimbForce`を無効化して全bodyの重力を復帰する。
- Jump直後にRootがまだTrussのAABB内にあるフレームで、`updatePhysicsState()`や`move()`が再びTruss重力無効化へ戻らないよう、一時的な`m_climbJumpSuppressed`を追加した。Trussから離れた時点で自動解除する。Ragdoll開始時にも解除する。
- `spec.md`と`doc/Instances/Humanoid.md`へClimbing中Jumpの仕様を追記した。
- `Humanoid.cpp`のGCC C++23構文検査と`git diff --check`は終了コード0。Windows Release build、brun、実機でのTruss脱出とJump軌道は未検証。
- 次の一手: Windows側再build後、ClimbingUp/Down中のJump、同一フレームのTruss重なり、重力復帰、全bodyのJump速度、Truss離脱後の通常GroundHeight hoverを確認する。

## 2026-09-15: Truss descent ground escape

- Ragdoll復帰専用だったRoot footprint support scanを`findRootSupport()`へ共通化した。Character自身を除外する下向きbox castで、normal.y>=0.5の最も高いsupportだけを採用するため、Truss側面・壁面を地面として扱わない。
- `ClimbingDown`で下降入力中、`RootY - supportY <= HipHeight + 0.2`を満たすとTrussを自動脱出する。`Normal`へ遷移し、Truss Forceを止め、全body重力と既存GroundHeight hoverを復帰する。位置/CFrameは変更しない。
- 直前の`-ClimbSpeed`が次のphysics stepへ残らないよう、脱出時は各R6 bodyの負のY速度だけを0にする。上向き速度とX/Z速度は保持する。Trussが地面まで続く場合はRootがTruss AABBを離れるまでTruss制御を抑制し、同じ入力で再び下降へ入らない。
- `Humanoid.cpp`のGCC C++23構文検査と`git diff --check`は終了コード0。Windows Release build、brun、実機の床際Truss下降・段差・坂・壁際は未検証。
- 次の一手: Windows側再build後、地面まで伸びたTrussでの連続S入力、段差/坂/壁際、既に低いRoot位置、auto escape後のhover安定化と歩行/jumpを確認する。

## 2026-09-15: Regression failure migration, Gyro, compound pose fixes

- Animation migration treats a stale generated `R6Walk` path as an empty binding, inserts the standard Animation once, and validates the resulting child, Humanoid reference/path, and ContentPath before recording the metadata version. A genuinely custom unresolved path remains untouched and is recorded as before.
- Unknown YAML `System.DefaultCameraMode` now warns and falls back specifically to `Character`; generic enum fallback behavior for other properties is unchanged.
- Default R6 RootGyro now controls Y as well as X/Z. `Humanoid::move()` supplies its heading through the Gyro and the legacy `YawForce` remains zeroed/disabled, including after ragdoll recovery. The Box3D gyro uses a world-inertia, MaxTorque and MaxAngularSpeed bounded PD torque without the former temporary strength multiplier.
- Box3D teleports through `setMemberWorldCFrame()` now clear linear/angular velocity and wake the body. Collision filter refresh always reapplies the native filter so character reparent/group changes rebuild contacts even if filter bits are unchanged. Free-standing BallSockets suppress connected collision to preserve an overlapping initial anchor; same-character ragdoll BallSockets retain connected collision subject to customFilter/NoCollision. BallSocket failures report their full path and the migration regression prints its anchor error.
- PhysicsConstraint/Weld serialization now regenerates Cube0/Cube1 paths from live endpoints after rename/reparent, without Weld reintroducing the old empty-name guard.
- Remote avatar pose replication applies one world delta per native body, then synchronizes every logical native member immediately. This preserves compound accessories and Motor6D member poses without moving the Model spawn CFrame.
- GCC C++23 syntax checks passed for `Box3DPhysicsBackend.cpp`, `CharacterRig.cpp`, `Humanoid.cpp`, `System.cpp`, `PhysicsConstraint.cpp`, and `Replication.cpp`; targeted `git diff --check` passed (only pre-existing CRLF normalization warning for `System.cpp`). Windows Release build cannot start because CMake is unavailable to `build.py` (`WinError 2`), and the existing Windows executable cannot be invoked from WSL due to the vsock error.
- Next step: rebuild on Windows and run the requested limited regressions: animation clip/character rig/quaternion, default camera, motor6d gyro, starter weld/accessory, remote avatar spawn transform, humanoid rig collision, and physics migration. The legacy Motor compatibility-envelope assertions remain intentionally out of scope and should be reported separately.

## 2026-09-15: Restore authored Character yaw Angular Force

- 操作感低下の原因は直前のGyro Y一本化で、既存Rootの`YawForce`を`Humanoid::move()`が毎frameゼロ・無効化していたことだった。通常Character操作では既存YawForceを再有効化し、従来のheading errorから目標Y angular velocityを更新する処理を復元した。
- YawForceを持つRigではRootGyro Yを無効化して二重制御を避ける。YawForceを持たない生成RigではRootGyro Yをfallbackとして有効化する。Gyro X/Zのupright stabilization、Free/Program・Seat・Ragdoll中のYawForce無効化は維持する。
- `Humanoid.cpp`のGCC C++23 syntax checkと対象`git diff --check`は成功。Windows実機での操作感・Ragdoll復帰後の旋回は再build後に確認する。

## 2026-09-15: Truss jump landing diagnosis

- Truss climbing中のJumpは、直後に`Normal`、`m_trussControlSuppressed=true`、重力有効、Climb Force無効、hover停止へ遷移する。次にTruss overlapが消えればsuppressionを解除し、hoverは下降中かつHipHeight capture時まで停止する。RagdollはNormal状態で全R6 bodyのcontact impactが`ImpactRagdollThreshold`以上の場合だけ開始する。
- 症状の「RagdollでもNormalでもない」はState定義上`ClimbingUp`/`ClimbingDown`を意味する。ソースだけでは、Truss overlapの残留、hover capture未到達、landing impactのいずれかを確定できない。
- 一時`[TrussJumpDiag]`ログを`Humanoid.cpp`へ追加した。launch、Truss overlap解除、jump中landing impact（thresholdとRoot速度）、hover landing capture（Root/Floor/HipHeight/vertical速度）を出力する。物理値・状態遷移は変更していない。
- `Humanoid.cpp`のGCC C++23 syntax checkと対象`git diff --check`は成功。次の一手: Windows再build後に症状を一度再現し、`TrussJumpDiag`の連続行と直前後の`Ragdoll`/`CharacterGroundDebug`/Box3D contactログを取得する。原因確定後に一時ログを削除して修正する。

### 2026-09-15 diagnostic result

- 実機ログで`landing-capture`時のRoot-to-floor distance=5.53827、HipHeight=3.0、下降速度=-98.8073を確認した。差分2.53827は既定`landingCaptureDistance=3.0`内なのでcaptureが成立している。
- 既定max upward acceleration=1200、重力=-196.2のため最大実効制動加速度は約1003.8 stud/s²。98.8073 stud/sを止めるには約4.86 studs必要で、capture時の目標までの距離2.538 studsでは不足する。`updateGroundHover()`が実行されているため、この瞬間はRagdollでもTruss制御中でもない。
- 次の修正候補: 下降速度から必要制動距離を計算してcapture範囲とshape cast検出距離を拡張し、目標HipHeightより下へ通過する前にhoverを有効化する。高速度Truss jump/落下回帰でRoot floor penetrationとstateを検証する。

### 2026-09-15 implemented speed-aware landing capture

- `Humanoid::updateGroundHover()`は、Root下降速度と`maxUpwardAcceleration - |gravity|`から必要制動距離を計算する。既定3 studsと、制動距離+1/60秒の移動量+0.25 studの安全余裕の大きい方をjump suppression解除用のcapture範囲にした。下向きshape castの検出距離も同じ動的範囲へ広げるため、capture開始前に床を見失わない。
- 通常の接地状態は従来どおり3 studsの静的範囲でだけ`isGrounded=true`にする。高速fallではhoverを早く有効化するが、空中をgroundedとして扱わない。Root vertical velocityが非有限ならfull path/値をerror出力してhoverを停止する。
- 原因確定に用いた一時`TrussJumpDiag`ログは削除した。`--character-hover-regression`へ、全R6 bodyをRoot相対姿勢のままY=-100 stud/sの高速下降へ置き、hover再開・Root bottom非貫通・HipHeightへの復帰を検査する回帰を追加した。
- `Humanoid.cpp`と`test_main.cpp`のGCC C++23 syntax check（`-DGLEW_NO_GLU`）および対象`git diff --check`は成功。Windows Release buildと新しい限定回帰は未実行。次の一手: Windows再build後に`--character-hover-regression`、続けてTruss jump→着地を実機確認する。

### 2026-09-15: Preserve high landing energy for Ragdoll

- 動的hover captureは高速落下を接触前に止めるため、従来のcontact impulseだけでは強いTruss jump/落下のRagdoll判定が失われる状態だった。`Humanoid::getLandingImpactEquivalentSpeed()`を追加し、空中状態からcaptureへ入る時の全character bodyの下向き成分だけを質量加重した垂直運動エネルギーから、従来の通常`landingCaptureDistance`で最大上向き加速度が吸収できる制動エネルギーを引く。残余をmass-weighted等価速度へ戻して既存`ImpactRagdollThreshold`と比較する。
- そのため通常のJumpPower着地は通常captureで吸収できるエネルギーとして除外され、約100 stud/sの高速落下はhoverが事前減速する前に、従来と同じ既定45 stud/s相当の閾値でRagdollへ入る。値が不正、native body/mass不足、非有限速度の場合はfull pathと値をerror出力し、通常のhover安全処理を続ける。
- `--character-hover-regression`は高速fallを二段階に分けた。閾値1000でhoverの非貫通・HipHeight復帰を単独確認し、既定閾値へ戻した同じfull-rig fallがRagdollへ遷移することを確認する。
- `Humanoid.cpp`と`test_main.cpp`のGCC C++23 syntax check（`-DGLEW_NO_GLU`）、対象差分の`git diff --check`は成功。WSLから既存Windows `RecubinTest.exe --character-hover-regression`を起動すると`UtilBindVsockAnyPort:309: socket failed 1`で停止し、更新後のWindows限定回帰・実機Truss jump確認は未実施。
- 次の一手: Windows側で再build後に`RecubinTest.exe --character-hover-regression`を実行し、Trussから高くjumpして着地時にRagdollへ入ること、通常JumpPower着地ではNormal/hover復帰することを確認する。

## 2026-09-17: PropertiesPanel numeric list input

- スキーマ駆動の単一・複数選択インスペクタへ、Vector3の`x, y, z`、CFrameの`x, y, z, pitch, yaw, roll`（度数法XYZ内因Euler）、Color4のRGBA 0〜255数値列入力を追加した。数値列はEnter時だけ確定し、個数不一致・非数・無限値は既存値を保持してUI上でエラー表示する。
- Color4は4成分の0〜255編集、RGBA数値列、`Palette`ボタンのImGui ColorPickerを提供する。内部値・YAML・Luauの0〜1 float表現は変更していない。全経路は既存live setter/final setterとUndo/Composite Undoを通し、テキストEnterの履歴重複を防止する。
- `PropertyTextInput`ヘルパーを追加し、固定個数・カンマ/空白区切り・有限floatの解析/整形を共通化した。`--property-text-input-regression`へ3/4/6値、空白、過不足、nan/infの回帰を追加。PropertiesPanel/Color4文書も更新した。
- `PropertyTextInput.cpp`、`PropertiesPanel.cpp`、`test_main.cpp`のGCC C++23 syntax checkと`git diff --check`は成功。Windows `cmd.exe /d /c py build.py build`はCMake起動時の`WinError 2`でconfigure前に停止したため、新しい限定回帰と実機UIは未実行。
- 次の一手: Windows側で再build後に`RecubinTest.exe --property-text-input-regression`を実行し、単一/複数選択のVector3/CFrame/Color4のコピーペースト、Undo/Redo、範囲外Color、無効入力、Paletteを実機確認する。

### 2026-09-17: Numeric list trailing-value completion

- PropertiesPanelの数値列パーサーは、1個以上かつ指定数以下の有限値を受け、未指定の末尾成分を最後に指定した値で補完するよう変更した。例としてVector3の`1,`は`1, 1, 1`、CFrameの`4, 5`は残り4成分を5、Color4の`12`は全RGBAを12として確定し、欄も正規化表記へ更新する。指定数超過・空入力・非数・無限値は引き続き拒否する。
- `--property-text-input-regression`へ3/4/6成分の補完回帰を追加した。`PropertyTextInput.cpp`と`test_main.cpp`のGCC C++23 syntax check、対象`git diff --check`は成功。Windows build・限定回帰はCMake不在のため未実行。

### 2026-09-17: Properties integer rounding controls

- Vector3、CFrame、Color4の単一・複数選択プロパティへ、既存ローカライズ済みの`丸`（nearest integer）操作を復元した。Vector3はXYZ、CFrameは位置と度数法Euler角、Color4は0〜255表示のRGBAを`std::round`で丸める。変更は単一`SetPropertyCommand`または複数`CompositeCommand`で確定し、live setterを持つ対象も本来のfinal setterを通す。
- 全数値列の後に操作ボタンを独立した行へ置いた。Color4のPaletteも数値列の右隣ではなく別行とし、狭いPropertiesPanelの横幅で丸め／Paletteボタンが画面外へ押し出されないようにした。
- `PropertiesPanel.cpp`のGCC C++23 syntax checkと対象`git diff --check`は成功。Windows buildと実機の狭幅レイアウト・丸めUndo/RedoはCMake不在のため未実行。

### 2026-09-17: Viewport shadow corruption diagnosis

- EditorのViewport内でまれに大きな三角形／矩形状の誤影が出る症状を調査した。`renderViewport()`はImGuiのViewportPanelからも呼ばれるが、呼び出し元の`GL_SCISSOR_TEST`を保存・無効化していなかった。
- ImGuiの画面座標clip rectangleが、3Dのshadow FBO（2048×2048）へ継承されると、cascadeのclearとcaster描画が一部矩形だけに制限され、layer内に古いdepthが残る。これがshadow mapの部分更新と大きな誤影を作る経路と判断した。
- `renderViewport()`でscissor boxと有効状態を退避し、メインFBO clearからshadow pass、main/extrasまでscissorを無効化し、終了時に復元するよう修正した。CSMのprojection、PCF、bias、texture arrayは変更していない。
- `Renderer.cpp`のGCC C++23 syntax checkと対象`git diff --check`は成功。Release buildとViewport実機再現確認は未実施。
- `cmd.exe /d /c py build.py build`はCMake起動時の`WinError 2`でconfigure前に停止した。既存の実行ファイルは今回の修正を含まないため`brun`は実行していない。
- 次の一手: Windows buildを復旧してViewportPanelで複数回再描画し、shadow mapの三角形状破綻が消えること、ImGuiのclip stateが後段へ復元されることを確認する。

### 2026-09-17: CSM planar self-shadow receiver bias

- scissor 修正を含む実行結果では、画面を横切る暗い帯と、同じ床の一方の三角形だけが暗くなる症状が残った。これは二種類のライトの影ではなく、cascade ごとの投影縮尺差と固定 receiver bias の不足による平面の自己影と判断した。
- `fragment.glsl` の `shadowCalc()` は既存の最小／normal slope bias を維持した上で、投影済み UV/depth の `dFdx`/`dFdy` から receiver plane の `dz/du` と `dz/dv` を導出する。3x3 PCF の 1 texel footprint を覆う bias（scale 1.25、最大 0.0025）と既存 bias の大きい方を深度比較に使用する。全 cascade へ共通の定数を増やす方法ではない。
- shadow pass、cascade selection/blend、texel snapping、`ShadowDistance` と `ShadowFadeDistance` は変更していない。scissor state の保存・復元修正も維持する。
- 次の一手: Windows 側でこの shader を含めて再 build し、Baseplate の両三角形、cascade split 付近、広い平面、Cube/character の接地影を確認する。平面の帯と三角形状 acne が消え、接地影が浮かないことを確認する。

### 2026-09-17: CSM shadow-source colour diagnostic

- receiver-plane bias 後も改善を実機で確認できなかったため、原因を推測で固定しない。`fragment.glsl` に一時 `CSM_SHADOW_CASCADE_DEBUG_TINT` を有効化し、shadow が実際に掛かっている部分だけを cascade 0=青、1=緑、2=マゼンタで最大90%置換する診断表示を追加した。
- split blend 領域では shadow 値と同じ係数で色も補間する。色の付かない暗部は CSM 深度比較由来ではないため、通常ライティング・マテリアル・別パスを次に調査する。原因確定後はこのマクロを0へ戻すか診断コードを削除する。
- 実機の色分けでは cascade 0 は正常で、cascade 1（緑）と cascade 2（マゼンタ）だけが広い平面を誤って遮蔽していることを確認した。各 layer の attachment 後に FBO complete を検査し、layer 固有の深度 attachment を `glClearBufferfv(GL_DEPTH, ...)` で明示的に clear するよう変更した。不完全な layer は状態コードを error 出力し、shadow を有効化しない。
- 追加の実機観察では、遠方からは正常だが接近すると床の三角形境界で影が急に割れる。Cube上面の2三角形は同一法線であり、メッシュ不整合ではなかった。原因候補を `shadowCalc()` の非一様なcascade分岐内で使っていた `dFdx/dFdy` receiver-plane bias に絞った。この微分はGLSL上未定義になり得て、三角形・cascade境界で不連続になる。
- screen-space微分biasを削除した。各cascadeのlight-space matrixのXY scaleからworld-units-per-texelを復元し、受け側world positionを法線方向へ0.25〜1.5 texel分オフセットしてから投影する方式へ変更した。既存の小さいdepth-domain min/slope biasとshadow pass polygon offsetは維持する。中遠距離ほどworld offsetが自然に増え、三角形境界とカメラ距離に依存しない。

### 2026-09-17: Camera-relative CSM coordinates

- 実機では、カメラ回転で誤影が消える、同一平面が三角形単位で割れる、近づいてcascade 0へ入ると正常になる、sceneがworld原点から遠い、という条件を確認した。これは大きな絶対world座標とlight view translationのfloat相殺誤差が、広いcascadeほど増幅される症状と一致する。
- CSM frustum fittingとlight-space matrixをview camera位置基準の相対座標へ変更した。メインvertex shaderは絶対`FragPos`とは別に、頂点段階で`ShadowRelativePos = FragPos - uShadowOrigin`を作って補間する。depth shaderもcasterのworld positionから同じoriginを引くため、書き込み側と比較側のdomainは一致する。
- texel snappingはcamera-relative centerへ直接丸めず、camera originのlight-space座標をdoubleで加えて絶対world grid上で丸め、その結果だけ相対座標へ戻す。これにより従来のstable shadowを維持する。共用depth shaderを使うSurfaceMark passでは`uShadowOrigin=(0,0,0)`を明示して従来の絶対matrix domainを保つ。
- cascade色分け診断は実機確認まで有効のまま。次の一手: world原点付近と問題座標の両方で、カメラ移動・回転、cascade 0/1/2、床の対角線、実物体の接地影を比較する。
- `Renderer.cpp`のGCC C++23 syntax checkと対象`git diff --check`は成功。Windows Release buildは`build.py`がCMakeを起動できず`WinError 2`でconfigure前に停止したため、shader linkとbrunは未検証。

### 2026-09-17: Rotation-invariant CSM projection

- camera-relative化後もカメラ回転時の破綻が残った。従来は各frustum sliceをlight-spaceのtight AABBへfitしていたため、カメラ回転だけでprojection width/height、world-units-per-texel、receiver normal offsetが同時に伸縮していた。centerのtexel snappingだけではprojection scaleの変化を安定化できない。
- 各sliceの8 cornerを包むbounding sphereを使う正方形orthographic projectionへ変更した。sphere radiusはsplit距離/FOV/aspectだけで決まり、1/16 stud単位へ切り上げるため、カメラ回転では変化しない。depth rangeもsphere直径+bounded marginとし、回転によるnear/far精度変動を除去した。
- camera-relative座標、絶対world grid上のdouble texel snapping、3x3 PCF、normal/depth bias、cascade blend、色分け診断は維持する。次の一手: 問題地点でカメラを360度ゆっくり回し、緑/マゼンタの三角形状誤影、cascade境界、shadow swimmingを確認する。
- 実機ではbounding sphere projection後も三角形状誤影が残った。次の診断として`shadowCalc()`がPCF occlusionと同時に、bias適用前のreceiver depthと中央shadow texel depthの差を返すよう変更した。誤影は従来のcascade色を基本とし、depth差0.001〜0.01で赤へ遷移する。cascade色のままなら近接したself-shadow/PCF slope、赤なら大きく誤ったdepthまたはtransformと切り分けられる。

### 2026-09-17: Large triangle identified as scene geometry shadow

- depth-delta診断で問題領域が全面赤となり、receiverと中央shadow texelに0.01以上の大きなdepth差があることを確認した。bias、PCF slope、float丸めによる自己影ではない。
- 実行中`triangle.rcbn`の最新autosaveには、同じXY範囲を持つ`Baseplate`（Position Y=0、Size 128x5x128）と`Debugplate`（Y=-50、同サイズ）があり、双方`CastShadow=true`だった。Lighting.Direction=(0.75,-1,-1)なので、上板の影は50 studs下の板上でXへ約+37.5、Zへ約-50 studs投影される。巨大な投影矩形の境界がカメラ透視とviewport clippingで三角形状に見える。診断結果・回転/移動で見える領域が変わる挙動と一致する。
- rendererが生成した偽影ではなくscene内の実casterが原因。確認手順は上側`Baseplate.CastShadow=false`へ一時変更すること。下側`Debugplate.CastShadow`は下板自身が他へ影を落とす設定であり、上板からの受影を止める設定ではない。

### 2026-09-17: Shadow clipping investigation conclusion

- ユーザーの実機確認により、`ShadowDistance`を広げると三角形状の境界が解消することを確認した。表示されていた境界はCSMの破損ではなく、設定されたshadow描画距離による正常なクリッピングだった。
- 調査中に追加したcascade/depth色分け、scissor/FBO診断、receiver normal offset、camera-relative shadow座標、bounding-sphere projectionをすべて削除し、調査開始前のCSM、3x3 PCF、texel snapping、slope bias、cascade fadeへ戻した。
- scene/autosaveおよび`ShadowDistance`の保存値は変更していない。`Renderer.cpp`のGCC C++23 syntax check、復元対象ファイルのHEAD一致確認、対象`git diff --check`は成功した。Windows build/brunはコード変更を残していないため再実行していない。

### 2026-09-18: triangle R6 rig migration alignment

- `/mnt/c/Users/Ryarta/DeveloppingGames/The baseplate/triangle.rcbn` の `Storage\Model` StarterCharacter を基準に、`CharacterRig::r6JointBindings()` と既定リグ生成を修正した。Torso は 2x2x2、Head は Root から +1.5、腕は同じY、脚は -2 の bind pose とし、肩/股の joint pivot と Motor6D C0/C1 が一致するようにした。triangle の WalkSpeed=32、HipHeight=3、色、RootGyro Y 無効、Root の YawForce も反映した。
- `scripts/CharacterChanger.luau` は存在しない `workspace.Agent` を待つ処理から、spawn 済みの `User.Character` と `CharacterAdded` を検証する処理へ移行した。StarterCharacter template を直接 User.Character に差し替えず、clone 後の参照とアクセサリを保持する。
- `src/test_main.cpp` の animation clip regression に triangle bind pose、サイズ、YawForce 設定の検査を追加した。`CharacterRig.cpp` と `test_main.cpp` の GCC C++23 syntax check は成功。`git diff --check` は既存のリポジトリ全体のCRLF警告のみ。
- Windows Release build、更新後の `RecubinTest.exe --animation-clip-regression`、triangle.rcbn の実機起動は未実行。次の一手は Windows 側で再buildし、同回帰と triangle の spawn/歩行/アニメーション/ラグドールを確認すること。

### 2026-09-18: migrate_character_rig_v2 alignment follow-up

- `migrate_character_rig_v2.py` がSystem/Storage配下のStarterCharacterを見落とす問題を修正し、`Storage\Model\...` の参照パスを生成できるようにした。
- 移行時のRootGyroを現行軸別形式（X/Z有効、Y無効、旧TargetRotation/Frequency/DampingRatioなし）へ変更し、Root/YawForce、RootのAngularX/AngularZ lock、HumanoidのWalkSpeed=32/HipHeight=3をtriangle設定へ合わせた。生成後の検証にも追加した。
- C++既定リグのRootGyro有効状態・軸別最大トルクも移行後設定と一致させた。`python3 -m py_compile migrate_character_rig_v2.py` と triangle.rcbn dry-run（would writeのみ）は成功。GCC構文検査は次の一手で実行する。

### 2026-09-18: Attachment-backed Motor native-frame correction

- Character/assembly 移行後に Attachment 付き通常 `Motor` が不安定化する原因は、`Box3DPhysicsBackend::createMotor()` が joint 作成時に scene graph の `Attachment::getWorldCFrame()` を使用していたことだった。compound 再構築・native pose 更新と Spatial 同期の間では graph pose が古くなり、Box3Dへ大きく誤った anchor を渡して即時の拘束補正を発生させていた。
- `createMotor()` は各 Attachment の nested local frame を `attachmentFrame(compoundLocalOffset, ...)` で求め、現在の native body frame と合成して pivot を作るよう変更した。Axis は従来どおり Cube0 local のままで、両revolute frameには共通のworld hinge orientationを使う。
- `--physics-migration-regression`へ、Weld compound member配下のnested Attachmentを持つMotorで、native bodyだけを移動してgraph poseを意図的にstaleにする回帰を追加した。native binding handleの安定、有限pose、attachment separation <= 0.05を確認する。
- `Box3DPhysicsBackend.cpp`と`test_main.cpp`のGCC C++23 syntax check、対象`git diff --check`は成功。WSLから既存Windows `RecubinTest.exe --physics-migration-regression`は`UtilBindVsockAnyPort:309: socket failed 1`で起動できず、Windows Release buildと更新後の限定回帰は未実行。
- 次の一手: Windows側で再build後に`RecubinTest.exe --physics-migration-regression`を実行し、Floating worldのCarで静止、前後進、左右旋回、乗降中にwheel attachment・chassisが跳ねず、Motor handleが再生成されないことを確認する。

### 2026-09-18: Attachment local-frame loss during physics synchronization

- 実機で車輪が外側へ放り出されることから前項の作成時frame補正だけでは不十分と判断して再調査した。根本原因は`syncCube()`が`BaseCube::setWorldCFrame()`を呼ぶことだった。一般Spatialの「子のworld poseを維持する」規則により、physics tickごとにAttachmentのlocal CFrameが親bodyの逆変換へ書き換わり、Attachmentがbodyを追従しなくなっていた。
- `syncCubeWorldCFramePreservingAttachments()`を追加した。physicsがBaseCubeを同期またはWeld assemblyを移動する前に、配下すべてのAttachment（nested Attachmentを含む）のauthored local CFrameだけを保存し、BaseCube更新後に`Spatial::applyLocalCFrameBatch()`で復元する。任意のSpatial/BaseCube childを物理追従させず、Attachmentだけを拘束アンカーとして親bodyへ追従させる。
- `--physics-migration-regression`はnested Attachmentのworld frameが現在のowning body frameと保存済みlocal frameの合成に一致することも検査する。旧実装はphysics sync後にこの検査を満たさない。
- `Box3DPhysicsBackend.cpp`のGCC C++23 syntax checkと対象`git diff --check`は成功。`test_main.cpp`のGCC syntax checkは環境に`GL/glu.h`が無く未実行。Windows Release buildと更新済み限定回帰は未実行。
- 次の一手: Windows側で`py build.py build`後に`build\\Release\\RecubinTest.exe --physics-migration-regression`を実行し、Floating worldのCarを静止、前後進、旋回、Seat乗降で確認する。scene側の各Motor Attachment pairにも初期world anchor差（現在約1〜2 studs）が無いことを確認する。

### 2026-09-18: Motor regression result review

- Windows Release buildは成功した。`--physics-migration-regression`のプロセス失敗1件は内部で7件の既存Motor compatibility assertionが失敗したためで、ログ上の単独Motor anchor separation=5.57345、4輪car separation=654.373も含む。これは今回追加した回帰以前からprogressに「legacy Motor compatibility-envelope assertions out of scope」として残っていた失敗群であり、今回の合否を示す新規の1失敗ではない。
- 今回追加した`motor_nested_compound`もattachment_error=861.378で失敗したが、テストがanchored compound rootへnative pose移動を行い、次のanchored syncがauthoring poseへ戻すという無効な手順だったため。rootをdynamicにして先にWeld compoundを構築し、重力を止めた上でnative poseとgraph poseの差を作るよう回帰を修正した。アンカーは作成時に一致し、Attachment local frame、anchor separation、handle、有限poseを検査する。
- 変更後のWindows buildと限定回帰は未実行。次の一手: Windows側でrebuild後に同回帰を再実行し、新しい`metric=motor_nested_compound`が`attachment_error <= 0.05`かつPASSとなることを確認する。既存7件のMotor compatibility failureは別タスクとして原因を切り分ける。

### 2026-09-18: Low HipHeight ground detection

- `Humanoid::updateGroundHover()`の下向きfootprint shape castは、従来Root collider底面よりさらに下から開始していた。そのためRootの半高（既定1 stud）より低いHipHeightでは、物理collisionがRootを床上へ保持していてもcast開始位置が床下となり、下向きcastが床を検出できず`isGrounded=false`のままになっていた。これがアニメーションでJumping状態を維持する原因だった。
- castの薄いfootprintをRoot中心から上へ半thicknessだけ置き、常にsupport面より上から下方向へ探索するよう修正した。HipHeightの意味（Root中心からsupport面までの目標distance）、capture幅、hover PD制御は変更していない。
- `--character-hover-regression`へHipHeight=0.5（Root半高未満）の物理的に支持されたcharacterを追加し、60 physics tick後も`isGrounded`であり、Rootが床上にあることを検査する回帰を追加した。
- `Humanoid.cpp`と`test_main.cpp`のGCC C++23 syntax check（`-DGLEW_NO_GLU`）、対象`git diff --check`は成功。Windows `cmd.exe /d /c py build.py build`はCMake起動時の`WinError 2`でconfigure前に停止したため、更新済み`RecubinTest.exe --character-hover-regression`は未実行。
- 次の一手: Windows側でCMakeがPATHから起動できる状態にしてrebuild後、`build\\Release\\RecubinTest.exe --character-hover-regression`を実行する。LowHipHeightのPASSと、Editorで低いHipHeightのcharacterが静止中にJumpingを維持しないことを確認する。

### 2026-09-18: Windows CMake discovery

- `build.py`へ`find_cmake_executable()`を追加した。PATHの`cmake`を最優先し、無い場合は公式CMakeのProgram Files/LocalAppData配置、続いてProgram Files配下のVisual Studio同梱CMakeを検出し、その絶対パスをconfigureとbuildの両方へ渡す。ユーザーまたはシステムのPATHは変更しない。
- この環境では`C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\Common7\\IDE\\CommonExtensions\\Microsoft\\CMake\\CMake\\bin\\cmake.exe`を検出した。`cmd.exe /d /c py build.py build`は同パスを出力して正常終了（既存出力がup-to-dateだったためcompile/linkは省略）。`python3 -m py_compile build.py`と対象`git diff --check`も成功。
- 更新済みHipHeight回帰を強制的に含めるための`RecubinTest`直接buildは、WSL→Windowsの実行経路が`UtilBindVsockAnyPort:309: socket failed 1`で2回停止した。CMake未検出ではない。次の一手: Windows Terminalから`cmake --build build --config Release --target RecubinTest --parallel`後、`build\\Release\\RecubinTest.exe --character-hover-regression`を実行する。

### 2026-09-18: Motor torque-limit stabilization

- ユーザー提供の更新後`output.txt`は`--physics-migration-regression`の実行結果であり、HipHeightの`--character-hover-regression`は含まれていなかった。Motorでは新規`motor_nested_compound`を含む7 assertionが失敗し、Attachment local frameは`nested_attachment_frame_error=0`で保持される一方、anchor separationが発散していた。
- `motorTorqueToMks()`が`MaxForce`をper-tick angular impulseと扱って`/ FIXED_STEP`していたため、Box3Dへ60倍のtorqueを渡していた。例としてMaxForce=1000は150 N*m、carの5000は750 N*mとなり、revolute jointの位置拘束を上回ってwheel/anchorを発散させる。MaxForceをMotor driveのtorque上限としてstud²→m²変換だけを行うよう修正した。
- `motor_nested_compound` fixtureはnative移動後のnested Attachmentがx=17なのにRotor Attachmentをx=16へ置いていたため、初期anchorが一致していなかった。Rotorをx=17へ修正し、native poseからのframe計算だけを検証するfixtureにした。
- `Box3DPhysicsBackend.cpp`と`test_main.cpp`のGCC C++23 syntax check（Box3D includeと`-DGLEW_NO_GLU`）、対象`git diff --check`は成功。Windows Release buildも成功した（Visual Studio同梱CMake自動検出を使用）。WSLから`RecubinTest.exe --physics-migration-regression`を起動する試行は`UtilBindVsockAnyPort:309: socket failed 1`で停止したため、新しい結果は未取得。
- 次の一手: Windows側で`build\\Release\\RecubinTest.exe --physics-migration-regression`を実行し、`motor_nested_compound attachment_error <= 0.05`、axis/carのanchor separation、全体PASSを確認する。HipHeight確認は別途`build\\Release\\RecubinTest.exe --character-hover-regression`を実行する。

### 2026-09-18: Tool Model migration and Grip Weld

- `Tool` を `Model` 派生に変更した。Tool の `Position`/`Rotation` は通常のlocal Spatial座標となり、握り配置は保存対象の `GripC0`/`GripC1`（`armWorld * GripC0 == handleWorld * GripC1`）へ分離した。既定値は従来の手前1 studを表す`GripC0=(0,0,-1)`、`GripC1=identity`。
- 装備時の `Handle::setWorldCFrame()` を削除した。Userは非保存の`ToolGrip` Weldへframe overrideを設定し、Box3D assembly再構築時にHandleとその通常Weld連結部品をGrip frameへ剛体配置する。通常Weldの作成時相対姿勢・公開プロパティは変更していない。解除・Inventory移動・despawn・respawnでToolGripを破棄／再作成し、Handle/対象腕が解決できない装備はpath付き警告とInventoryへのrollbackを行う。
- SceneLoaderは`GripC0`/`GripC1`が無い旧Toolだけをlegacy形式と判定し、旧`Position`/`Rotation`を`GripC1`へ逆変換する。新形式の同名プロパティはModel local CFrameとしてロードする。Editorの旧Tool Position/Rotation undo commandもModelのlocal CFrame更新へ統一した。
- `--tool-weld-regression`はruntime ToolGrip frame、ToolのModel/Spatial継承、legacy YAML移行、新形式のlocal CFrameを検査するよう拡張した。既存のinput回帰は実際のHumanoid/RightArm/Handleを用意してからTool装備を行う。
- `Tool.cpp`、`Weld.cpp`、`User.cpp`、`SceneLoader.cpp`、`Box3DPhysicsBackend.cpp`、`test_main.cpp`のGCC C++23 syntax checkと対象`git diff --check`は成功。Windows `cmd.exe /d /c py build.py build`も`Recubin`/`RecubinEngine`/`RecubinTest`で成功した。
- WSLからの`build\\Release\\RecubinTest.exe --tool-weld-regression`は`UtilBindVsockAnyPort:309: socket failed 1`で起動できず未実行。次の一手: Windows Terminalから同コマンドを実行し、ToolGrip frame、legacy/new YAML、再装備・respawnを確認する。

### 2026-09-18: Tool Activated script-global isolation

- SMGとRPGの`Activated` SignalはToolごとに別インスタンスだったが、Luauの各Script coroutineは同じglobal tableを使っていた。SMGがglobal `shoot()`を定義した後にRPGが同名関数を定義すると、SMGの`startFiring()`内の`shoot()`呼び出しまでRPG版へ置換され、SMGの連射がRocketLauncherの`bom`を出す状態だった。
- `LuauEngine::execute()`は新しいScript coroutineへ`luaL_sandboxthread()`を、chunkをloadする前に適用するよう変更した。各Scriptはengine APIを親globalから読む一方、自身の`shoot`/`main`等は固有のwritable global tableへ書き込む。Tool固有のSignal経路を特別扱いせず、全Script間の同名global競合を解消する。
- `--tool-signal-isolation-regression`を追加した。SMG/RPGが同じglobal名`shoot`を使うfixtureでSMGのActivatedだけをfireし、SMG出力1件・RPG出力0件を検査する。
- Windows Release buildは成功。WSLからの更新済み`RecubinTest.exe --tool-signal-isolation-regression`は`UtilBindVsockAnyPort:309: socket failed 1`で起動できず未実行。次の一手: Windows Terminalで同限定回帰を実行し、Floating worldでSMG/RPGを交互に装備してSMG発射時に`bom`が出ないことを確認する。

### 2026-09-19: Workspace Raycast exclusion table

- Box3D backendは従来から単一`Instance` root（Modelを含む）とその子孫をray query callbackで除外できたが、Luau bindingは`BaseCube`一つしか受け付けず、`{User.Character}`のようなテーブルを無言で無視していた。
- `workspace:Raycast(origin, direction, maxDistance?, exclude?)`の`exclude`は、単一InstanceまたはInstance配列テーブルを受け取るようにした。Box3D query callbackへ全rootを直接渡し、各root自身と子孫`BaseCube`を除外する。除外Cubeに入った後にレイを進めて再試行する実装にはせず、大きなCharacter collider内で次のhitを失わないようにした。
- `--workspace-raycast-exclude-regression`を追加。二つのModel配下Cubeをテーブルで除外し、その奥のTargetをLuau Raycastが返すことを検査する。`doc/Instances/Workspace.md`に引数契約も記載した。
- Windows Release buildは成功。WSLから限定回帰を起動すると`UtilBindVsockAnyPort:309: socket failed 1`で停止したため結果未取得。次の一手: Windows Terminalで`build\\Release\\RecubinTest.exe --workspace-raycast-exclude-regression`を実行し、Floating worldでは`workspace:Raycast(ray.Origin, direction, rayRange, {User.Character})`が自キャラクターをhitしないことを確認する。

### 2026-09-19: Studio logo asset packaging

- 既存のルート`Recubin.png`を`assets/image/Recubin.png`へ移し、Windows専用の`RecubinResources.rc`とCMakeのRCDATA埋め込みを削除した。
- `WelcomePanel`はOS別の埋め込みリソースではなく、共通の`assets/image/Recubin.png`を`stbi_load`で読み込む。ロード失敗はパスとstb_imageの理由をログへ出す。
- `build.py package`のWindows/macOS Studioパッケージャーは、ロゴを`assets/image/Recubin.png`へ必須コピーし、元ファイルが無い場合はエラー終了する。
- `python3 -m py_compile build.py`、`WelcomePanel.cpp`のGCC C++23構文検査、`git diff --check`、一時ディレクトリへのロゴコピー検査、Windows Release buildは成功。GUIの実機表示とmacOS上のパッケージ起動は未検証。

### 2026-09-19: Viewport screen-space selection outline

- エディタのBaseCube/Mesh選択表示を、硬いmesh edgeのリボン描画からRenderer所有のR8 Selection Mask FBOを使うscreen-space outlineへ変更した。通常Viewportのdepthをmask FBOへcopyして選択geometryだけをdepth test付きでmaskへ描画するため、前景に隠れた部分は表示されない。
- primary/secondary選択およびModel配下のBaseCubeを同じmaskへ描画し、8近傍のoutline shaderでunion外周だけを2px幅のcyanとしてscene extras後・post-effect前に合成する。mask FBOはViewportサイズ変更時に再作成し、Renderer破棄時にtexture/renderbuffer/FBO/shaderを全解放する。
- エディタ選択のBaseCube edge highlightは除去した。ゲームプレイ用`Highlight`インスタンスとDecalのface選択表示はこの変更の対象外として既存動作を維持する。`doc/Rendering.md`を現行経路へ更新した。
- `Renderer.cpp`のGCC C++23 syntax checkと対象`git diff --check`は成功。Windows Release buildは成功（新規C4458 warningを解消済み）。実機での遮蔽・複数Mesh union・resize表示は未確認。

### 2026-09-19: Selection Mask hover unification and depth stability

- 実機ログで`primary=System\\Workspace\\Cube1`、`drawable geometry=1`を確認し、Selection Mask shader/FBO生成失敗ではないことを確定した。内側のcyan横線は、通常FBOの24-bit depthをcopy後に同じsurfaceをmaskへ再ラスタライズした際の量子化差による自己depth競合だった。
- 選択、通常ホバー、Weld候補ホバーを`Renderer::renderSelectionOutline()`の共通Selection Mask経路へ統一した。旧`drawTransientHighlight()`のedge/ribbon描画は削除した。mask geometry passだけに`GL_POLYGON_OFFSET_FILL(-1,-1)`を設定して自己depth競合を除去し、全GL状態を復元する。選択=cyan 2px、通常hover=白半透明1.5px、Weld hover=緑3px。
- `Renderer.cpp`と`ViewportPanel.cpp`のGCC C++23 syntax check、対象と新規shaderの`git diff --check`は成功。Windows Release buildは実装担当側で成功。次の一手: Windows側で`py build.py brun`を実行し、Cube/Mesh/Modelの選択、未選択hover、Weld hover、前景遮蔽、viewport resizeを実機確認する。
- 実機で毎フレーム表示された`Selection mask/outline OpenGL error: 0x500`は、pass開始前から未回収だったGL errorをSelection pass末尾で回収して誤帰属した診断だった。mask shader/FBO/geometry入力は正常だったため、このper-frame `glGetError()`診断を削除した。FBO不完全時の明示的エラーは維持する。

### 2026-09-19: prompt.md に基づくエディターGUI整理

- `EditorManager` の既存ネイビー基調を維持したまま、ツールバーの通常／hover／activeボタンへ控えめな青いガラス調（上明・下暗）を追加した。文字`|`ではなく2本線の区切りを使い、SnapのMove／Rotate／Resizeは単位付きのまとまり、Collision Fitは独立した`Fit`操作として表示する。
- `PropertiesPanel` の通常・複数選択インスペクタをProperty/Valueの2列表へ統一した。スキーマのカテゴリは該当プロパティがある場合だけ折りたたみ見出しとして表示し、InstanceのName/ClassName/Pathも同じ構造へ移した。既存のpicker、Undo、liveSet、複数選択一括編集は保持する。
- Consoleの両タブはClear、Filter、Copyを同じ操作列へ揃え、本文との間を区切った。タブ／Explorerの既存選択・hover表現、ドッキング構造、フォント／アイコン体系は変更していない。
- `UiHelpers.cpp`、`EditorManager.cpp`、`ConsolePanel.cpp`、`PropertiesPanel.cpp`のGCC C++23 syntax checkと`git diff --check`は成功（後者のCRLF通知は既存ファイル由来）。Windows `cmd.exe /d /c py build.py build`はRecubin、RecubinEngine、RecubinTestすべて成功。GUI自動スクリーンショット試験は方針により実施せず、見た目と操作感は手動確認待ち。

### 2026-09-19: ガラス表現と可読性の改善

- `EditorUi::glassButton()`は、ImGuiの標準クリック／ID／レイアウト処理をそのまま使い、標準frameを透明化した上で、backgroundだけをdraw-listの文字より前のchannelへ描画するよう変更した。このためglassのbase／sheenが文字とiconの上へ重ならず、themeのほぼ白いTextを常に高コントラストで表示する。
- glassは暗い現在色を基調に、上下gradient、上部32%の青白いsheen、top inner highlight、下辺・右辺の暗いborderを重ねる。hover、pressed、selected、selected+hoverをそれぞれ変化させ、pressed時はsheen／top highlightを弱める。selectedは既存色を明るくするため、Weldなどのgreen semantic colorをblueへ置換しない。
- 選択中toolbar category、Select／Move／Resize／Rotate、Terrain操作、Weld Modeを`selected`として共通helperへ渡すようにした。Dock tabはdark inactive、light active、blue-white overline（unfocused時を含む）で弱いglass表現にし、panel titleは本文をflatに保ったまま濃紺の控えめなbase／borderへ調整した。
- `UiHelpers.cpp`と`EditorManager.cpp`のGCC C++23 syntax check、対象`git diff --check`、Windows `cmd.exe /d /c py build.py build`は成功。GUI自動スクリーンショットは方針により実施せず、実機での見た目・操作感は手動確認待ち。

### 2026-09-19: Toolbar separator の行高基準化

- 既存のtoolbar separatorは`GetFrameHeight()`を使っていたため、58pxのtoolbar button行ではなくテキスト行に縮んでいた。`EditorManager.cpp`の`drawToolbarMajorSeparator()`へ置換し、左右10pxの余白、58px行中央の28px、暗い青線＋淡い青線を一部品として描くようにした。
- Play、Transform、Snap/Fit、Creation、Save/Loadの境界はMajor separatorを使用する。Snap内部のMove／Rotate／Resize／FitはMinor lineを置かず、12pxのinline gapに整理したため、細かい設定行が過密にならない。
- `EditorManager.cpp`のGCC C++23 syntax check、対象`git diff --check`、Windows `cmd.exe /d /c py build.py build`は成功。GUI自動スクリーンショットは方針により実施せず、実機表示は手動確認待ち。

### 2026-09-19: Major separator height follow-up

- Major separatorを28pxから40pxへ延長した。58pxのtoolbar button行に対して上下9pxずつのmarginを残し、Transform、Snap/Fit、Creation、Save/Loadの境界が小装飾でなく明確なgroup dividerとして見える高さにした。色、2本線の溝表現、Snap内部の余白区切りは維持する。

### 2026-09-19: Script / TextFile auxiliary editor

- `CodeEditorPanel` を追加し、Explorer の `Script` / `TextFile` ダブルクリックを `EditorManager::openCodeEditor()` へ接続した。同じ Instance はポインタIDで既存タブを再利用し、`MainDockSpace` に通常のDockタブとして表示する。weak pointerで対象削除・シーン切替後のdangling referenceを避ける。
- 本文は ImGui `InputTextMultiline` の標準編集機能（Tab、選択、Clipboard、Undo/Redo、縦横スクロール）を使い、同じスクロール領域へ行番号とsyntax overlayを描画した。横スクロールでは行番号欄を固定し、Luauのkeyword/literal/string/number/type/operator/commentと複数行comment/string stateを色分けする。
- Renderer起動時に`assets/fonts/JetBrainsMono-Medium.ttf`をFontAtlasへ一度だけ読み込み、コード本文・行番号だけに適用した。UI既定のDotGothic16は維持する。
- Ctrl+Sはコードタブへフォーカスがある場合にScriptの`Source`と元Path、TextFileのRuntimeFileSystem overlay (`StorageId`)へ保存し、成功時にScene dirtyを更新する。dirty tabを閉じる場合はSave/Discard/Cancelを表示する。
- `CodeEditorPanel.cpp`、`EditorManager.cpp`、`SceneHierarchyPanel.cpp`、`Renderer.cpp`、`main.cpp`のGCC C++23 syntax checkと対象`git diff --check`は成功。Windows Release `cmd.exe /d /c py build.py build`（Recubin/RecubinEngine/RecubinTest）は成功。GUIでの実機ダブルクリック・Dock配置・保存動作は未確認。次の一手はWindows上でScript/TextFileを開き、入力・スクロール・dirty/close確認・Ctrl+S・シーン切替を手動確認すること。
- Sceneロード時にLuauが差し替えるRuntimeFileSystemをEditorManagerへ再接続し、無題Sceneでは古いoverlayを参照しないようにした。JetBrains Monoのruntime側ロードは無効化し、追加後のRelease buildも成功した。

### 2026-09-19: Font Awesome merge target correction

- JetBrains Mono追加後にToolbar/ExplorerのFont Awesomeアイコンが`?`へフォールバックする原因を特定した。ImGuiのFont `MergeMode`はデフォルトで`Fonts.back()`へマージするため、追加されたJetBrains Monoへアイコンが入り、既定のDotGothic16から見えなくなっていた。
- `ImFontConfig::DstFont`をDotGothic16（または既定フォント）へ明示し、コードフォントを独立したままUIアイコンを正しいフォントへマージするよう修正した。RendererのGCC構文検査は成功。
- 修正後のRelease buildは、起動中の`build/Release/Recubin.exe`がロックされて`LNK1104`となり、リンク未完了。Studioを終了した後に`cmd.exe /d /c py build.py build`を再実行し、アイコン表示を実機確認する。

### 2026-09-19: Code editor gutter origin correction

- コードエディタで入力欄の一時的な`FramePadding`を解除した後、syntax overlayが現在の通常スタイル値から本文原点を計算していたため、本文が左端へずれ、行番号が画面外へ描画されていた。`CodeEditorPanel.cpp`でガター幅と入力欄の左右・上下paddingを定数化し、行番号・本文・ガター背景を`InputTextMultiline`と同じ原点へ揃えた。
- 変更対象の`CodeEditorPanel.cpp`は`g++ -std=c++23 -fsyntax-only -DGLEW_NO_GLU -I. -Iinclude -Isrc`を通過し、`git diff --check`も完了。Windows Releaseの`cmd.exe /d /c py build.py build`（Recubin、RecubinEngine、RecubinTest）は成功した。
- 左端への横スクロール、行番号の可視性、編集・選択操作はGUI実機で未確認。次の一手はStudioを起動し、補助エディタを開いて横スクロール位置を変更し、行番号が固定表示されることを確認すること。

### 2026-09-19: Code editor font size shortcuts

- `CodeEditorPanel`へコードフォントサイズ（10〜34px、1px刻み）を追加し、コードエディタにフォーカスがある間のCtrl+`+`／Ctrl+`-`で変更できるようにした。通常キー、テンキー、日本語配列の`Shift+;`による`+`を受け付ける。`PushFont`へ同じサイズを渡すため、InputTextのカーソル・行番号・syntax overlayも同時に拡縮する。行数に応じてガター幅も拡張する。
- `CodeEditorPanel.cpp`のGCC C++23 syntax checkは成功。Windows Release buildは`CodeEditorPanel.cpp`のコンパイルまで進んだが、起動中の`build/Release/Recubin.exe`がロックされ、リンクが`LNK1104`で停止した。Studioを終了後、`cmd.exe /d /c py build.py build`を再実行する必要がある。
- Ctrl+ショートカットの実機操作とサイズ変更後のスクロール位置は未確認。

### 2026-09-20: Code editor dirty state separation

- `CodeEditorPanel`の保存完了コールバックから`EditorManager::markDirty()`を外し、Script / TextFile の外部ファイル保存でScene dirtyが立たないようにした。Scriptのファイル保存状態とSceneの保存状態を独立させた。
- コードエディタがdirtyのままStudioを終了する場合、Scene用の未保存ダイアログとは別に、コード専用のSave / Quit Without Saving / Cancel確認を表示するよう`EditorManager`と`main.cpp`の終了経路へ追加した。タブを閉じるときの既存Save / Discard / Cancelも維持している。
- `Localization`へコード未保存ダイアログの日本語・英語文言を追加し、`doc/Editor/CodeEditorPanel.md`と`doc/Editor/EditorManager.md`を更新した。`CodeEditorPanel.cpp`、`EditorManager.cpp`、`Localization.cpp`、`main.cpp`のGCC C++23 syntax checkと`git diff --check`は成功。Windows Release build（Recubin、RecubinEngine、RecubinTest）も成功した（既存のC4458/C4005 warningのみ）。

### 2026-09-20: Confirmation UI and fullscreen toolbar consistency

- CodeEditorPanelのタブ閉じる確認をEditorManagerの終了確認と同じLocalization／`EditorUi::dangerButton`／`safeButton`経路へ統一した。英語固定の小さなボタンと日本語の終了確認で見た目が分かれる問題を解消し、保存失敗時は確認を維持する。
- Toolbarの`drawIconButton()`からウィンドウ全体へ作用する`SetWindowFontScale()`を外し、現在フォントだけを`PushFont(nullptr, baseSize * scale)`で一時縮小するようにした。DPI／フルスクリーン時にも既存の全体フォント倍率を壊さず、ボタン内に収まる倍率の下限を0.45へ調整した。
- `CodeEditorPanel.cpp`、`EditorManager.cpp`、`Localization.cpp`のGCC C++23 syntax checkは成功。Windows Release build（Recubin、RecubinEngine、RecubinTest）も成功した（既存のC4458/C4005 warningのみ）。GUI実機での最大化・フルスクリーン表示確認は未実施。

### 2026-09-20: Default character ground alignment and safe spawn fallback

- 既定R6はRootのSize `(2,2,1)` と`basePos`を維持しつつ、可視パーツのbind poseをRoot基準で2 stud上げた。Rootが接地したとき脚の底面がRoot下端と一致するため、見た目の足が浮く／沈む配置を解消する。Motor6D bind frameもこの姿勢から再算出される。
- enabled `SpawnLocation`が存在しない場合、ローカルとリモートのCharacter Rootは位置`(0,100,0)`へspawnし、テンプレートに設定されたRoot回転は維持する。SpawnLocationを選べる場合の既存の選択・HipHeight動作は変更しない。
- `spec.md`、`User.hpp`、local/remote spawn回帰、既定R6 bind pose回帰を更新した。`CharacterRig.cpp`、`User.cpp`、`test_main.cpp`のGCC C++23 syntax check、対象`git diff --check`、Windows Release buildは成功。WSLからの`RecubinTest.exe --spawn-location-regression`は`UtilBindVsockAnyPort:309: socket failed 1`で起動できず未実行。次の一手: Windows Terminalで`build\\Release\\RecubinTest.exe --spawn-location-regression`と`--animation-clip-regression`を実行し、既定キャラクターの足元とfallback spawnを実機確認する。

### 2026-09-20: Runtime PlayerCharacter scene persistence exclusion

- `assets/scenes/justBaseplate.rcbn`の調査で、既定の`StarterCharacter`自体は`Root=[0,0,0]`、`Torso=[0,2,0]`、`RootJoint.C0=[0,2,0]`と正しかった。一方、過去のPlay中に保存されたruntime `PlayerCharacter`が同ファイルに残り、ground sample由来の`HipHeight=5.021...`と有効な`CharacterHoverForce`を持っていた。再ロードでこれが明示HipHeightとして扱われ、床から浮く原因だった。
- SceneLoaderは予約済みruntime Model名`PlayerCharacter`および`PlayerCharacter_<PeerId>`を保存対象から除外し、既存scene読込時にも子孫を生成せずスキップする。スキップはログへ出る。`StarterCharacter`と通常Modelは保持するため、次回保存で古いruntime avatarはsceneから除去される。
- `--runtime-character-serialization-regression`を追加し、local/remote runtime characterのsave/load除外とStarterCharacter/通常Modelの保持を検査する。`SceneLoader.cpp`と`test_main.cpp`のGCC C++23 syntax check、対象`git diff --check`、Windows Release buildは成功。WSLからの限定回帰は`UtilBindVsockAnyPort:309: socket failed 1`で実行不可。次の一手: Windows Terminalで`build\\Release\\RecubinTest.exe --runtime-character-serialization-regression`を実行し、Studioで`justBaseplate.rcbn`を開いてskipログと非浮遊の新規PlayerCharacterを確認する。

### 2026-09-20: Airborne automatic HipHeight capture correction

- cleanな`justBaseplate.rcbn`での実機ログにより、残留キャラクターではなくautomatic HipHeightの捕捉条件が原因と確定した。fallback spawn後の落下中、`rootY=5.5004`／`floorY=0.5`／`distance=5.0004`を初期HipHeightとして保存し、HoverForceがその誤った距離を保持していた。
- 未設定HipHeightはRoot collider下面がsupport surfaceへ接触または0.1 stud以内となるまでfloor distanceを保存しない。捕捉前も高所spawnの安全な減速を維持するため、hover controllerはRoot半身高を一時目標とし、空中のdistanceをgrounded/landing captureへ使わない。明示HipHeightの挙動は維持する。診断用ログは原因確定後に削除した。
- `--character-hover-regression`を接地後のautomatic HipHeight／Root下端=床上面へ更新し、`spec.md`もsupport近接後に捕捉する契約へ更新した。`Humanoid.cpp`のGCC C++23 syntax check、対象`git diff --check`、Windows Release buildは成功。WSLから限定回帰は`UtilBindVsockAnyPort:309: socket failed 1`で実行不可。次の一手: Windows側で`build\\Release\\RecubinTest.exe --character-hover-regression`を実行し、`justBaseplate.rcbn`で新規default characterの脚底が床へ接地することを確認する。
- Windows実機でdefault characterが浮かずに接地することをユーザーが確認済み。今回の修正は完了。

### 2026-09-20: Character motion stops when viewport focus is lost

- `User::processInput()`はビューポートがfocusedからunfocusedへ遷移したフレームに、Characterの水平速度・角速度を`Humanoid::stopCharacterMotion()`で停止する。フォーカス喪失後にキー解放を受け取れず、直前の物理速度で歩き続ける問題を解消した。Y速度は維持するため、落下・ジャンプ等の垂直物理は中断しない。
- Luauから設定された`MoveDirection`はフォーカス外でも継続する既存仕様のため、script movement中には停止しない。`--character-hover-regression`へ通常入力のfocus loss停止を追加し、`doc/Instances/UserInput.md`へ契約を記録した。
- `User.cpp`と`test_main.cpp`のGCC C++23 syntax check、`git diff --check`、Windows Release build（Recubin、RecubinEngine、RecubinTest）は成功。その後、メインGLFW windowが非focus／iconifiedの場合もviewport入力をfalseとして同じ停止処理へ渡すよう`main.cpp`を追加修正し、同ファイルのGCC C++23 syntax checkは成功した。追加修正後のWindows Release buildおよびWSLからの`RecubinTest.exe --character-hover-regression`は、`UtilBindVsockAnyPort:309: socket failed 1`でWindowsコマンドを開始できず未実行。次の一手: Windows Terminalから`py build.py build`、`build\\Release\\RecubinTest.exe --character-hover-regression`を実行し、Play中に移動キーを押したままビューポート外をクリックして即停止することを確認する。
### 2026-09-20: Normal Play SpawnLocation height correction

- 実機診断で通常Playは`System\Workspace\SpawnLocation` (`[0,1.5,0]`)を正しく選択していたが、HipHeight未設定分岐がStarterCharacter Rootのauthoring Y `-1.5`を保持し、`rootAfter=[0,-1.5,0]`としていた。候補解決ではなく、SpawnLocation選択後の高さ計算が原因。
- `User::placeCharacterAtSpawn()`は、明示HipHeightでは`Spawn半高 + HipHeight`、未設定では`Spawn半高 + Root半高`のlocal Y offsetをSpawnLocation full CFrameへ合成する。候補なしの`(0,100,0)`fallbackは維持し、不正model/Humanoid/Rootと候補なしはwarningで観測可能にした。原因確定用の詳細ログと、未証明のpost-attachment二重配置は削除済み。
- `--spawn-location-regression`はStarter Rootのauthoring Yを負値にし、それを引き継がずSpawn上面へRoot半高で置くこと、full CFrameとrig相対姿勢、CharacterAdded時の最終姿勢を検査するよう更新した。矛盾していた`spec.md`のHipHeight未設定時契約も修正した。
- Windows Release buildは`Recubin`、`RecubinEngine`、`RecubinTest`すべて成功（既存のAPIENTRY macro redefinition warningのみ）。WSLからexeは起動できないため、限定回帰と実機Play表示はWindows側での確認待ち。

### 2026-09-20: Luau WorldPosition assignment

- `Spatial.WorldPosition` はgetterだけがLuauへ登録されており、代入がsetter不在のまま無視されていた。`Spatial::setWorldPosition()`を追加し、現在のworld rotationを保ったfull CFrameを座標親の逆CFrameでlocalへ変換して、virtual `setPosition()`へ渡すようにした。Workspace直下では指定world値がそのままlocal値となり、BaseCubeでは既存の`teleportTo()`とBox3D同期経路を通る。
- Luauの`Spatial.WorldPosition` setterを登録し、Vector3以外はLuauの型エラー、不正な非有限値はClassName・full path・値を含むエラーログで観測可能にした。`--spatial-coordinate-regression`へWorkspace直下と回転・移動したSpatial親配下の実Luau代入、local/world変換、physics更新後の位置保持を追加した。
- `Spatial.cpp`、`LuauEngine_Dispatch.cpp`、`test_main.cpp`のGCC C++23 syntax check、対象差分のwhitespace check、Windows Release build（Recubin、RecubinEngine、RecubinTest）は成功。WSLからの限定回帰は`UtilBindVsockAnyPort:309: socket failed 1`で起動不可。次の一手: Windows Terminalで`build\Release\RecubinTest.exe --spatial-coordinate-regression`を実行し、StudioでもWorkspace直下のBaseCubeへLuauからWorldPositionを代入して確認する。

### 2026-09-20: Play Here Root alignment correction

- Play開始処理はPlay Here選択時のカメラ位置を正しく`User::spawnCharacter()`へ渡していたが、明示位置分岐がHumanoid RootではなくCharacter Model原点を指定位置へ合わせていた。StarterCharacterのModel原点とRootにoffsetがあると、実際のキャラクターがカメラ位置からずれる原因だった。
- Play Hereの初回配置は、Rootの現在のworld CFrameから回転を維持したtarget Root CFrameを作り、rig全体へ同じworld deltaを適用する。HumanoidまたはRootが解決できない場合はCharacterのClassName・Name・full pathを含むwarningを出し、不正なModel原点への代替配置を行わない。通常PlayとrespawnのSpawnLocation選択は変更していない。
- `--spawn-location-regression`の旧Model.Position検査を、Model原点とRootが異なるfixtureでRootが明示カメラ位置へ一致し、Root-to-Head相対姿勢が維持される検査へ置換した。`User.cpp`と`test_main.cpp`のGCC C++23 syntax check、対象差分のwhitespace check、Windows Release build（Recubin、RecubinEngine、RecubinTest）は成功。WSLから限定回帰は`UtilBindVsockAnyPort:309: socket failed 1`で起動不可。次の一手: Windows Terminalで`build\Release\RecubinTest.exe --spawn-location-regression`を実行し、StudioのPlay Hereを実機確認する。

### 2026-09-21: Default R6 bind pose and explicit HipHeight restoration

- `/mnt/c/Users/Ryarta/DeveloppingGames/The baseplate/triangle.rcbn`のStarterCharacterと生成コード・履歴を比較し、`754b116`で旧triangle bind poseが意図的に変更されていたことを特定した。既定R6をRoot/Torso同中心、Head `+1.5`、腕中心Root同高、脚中心`-2`へ復元し、Motor6DのRootJoint/Neck/Shoulder/Hip C0/C1も参照リグと一致させた。Rootと既定Cube TorsoのSizeはともに`(2,2,1)`で、triangle内Mesh Torsoの保存Size `(2,2,2)`はMesh形状固有として流用していない。
- Humanoid.HipHeightは既定3 studの通常保存プロパティへ戻した。unset/explicit/ground-initializedの隠れ状態と、接地shape castからHipHeightを自動算出・上書きする経路を削除した。clone、instance copy、YAML、Luauの設定値は通常のPropertyRegistry経路で保持する。SpawnLocation配置は常に`Spawn.Size.y/2 + HipHeight`を使用する。
- `--animation-clip-regression`は左右全bodyのXYZ offsetと6つのMotor6D bind frame、Torsoの物理Size `(2,2,1)`、既定HipHeight 3を検査する。property schema、spawn、CharacterAdded、hover回帰も固定HipHeight契約へ更新した。対象C++のGCC C++23 syntax check、対象差分のwhitespace check、Windows Release build（Recubin、RecubinEngine、RecubinTest）は成功。WSLから限定回帰は`UtilBindVsockAnyPort:309: socket failed 1`で起動不可。次の一手: Windows Terminalで`--animation-clip-regression`、`--character-hover-regression`、`--spawn-location-regression`、`--property-schema-regression`を実行し、Studioで既定生成リグを実機確認する。

### 2026-09-21: BaseCube touch overlap events

- Box3DにCanTouch対応sensor shapeを追加し、Touched/TouchEndedを物理反発から分離した。CanCollide=falseでも形状overlapを検出し、CanTouch双方trueのペアのみ通知する。物理contact callbackは従来互換のまま維持し、touch callbackを分離した。
- `--touch-event-regression`へoverlap進入・退出、通過、CanTouch抑止、物理衝突独立性、Clone/schema/YAML/Luau signal検査を追加した。Windows Release build成功。WSLからの限定回帰起動は`UtilBindVsockAnyPort:309: socket failed 1`で未実行。

### 2026-09-21: Ragdoll後のMotor6D Animation復帰

- 復帰後のMotor6D native jointは再生成されていたが、`Humanoid::enterRagdoll()`が`stopAnimation()`を呼んで再生フラグと時刻を破棄していた。`updateAnimation()`にはRagdoll/Recovering中の評価停止が既にあるため、この重複停止を削除し、再生状態と時刻を保持してNormal復帰後に同じAnimationを再開するようにした。
- `--ragdoll-motor-recovery-regression`を追加した。実Box3D上のdefault R6でラグドール前の肩Motor6D応答、Ragdoll/Recovering/Normal遷移、Animation再生状態の保持、復帰後のAnimation出力による物理追従、さらに別Transformへの追従を検査する。復帰Animation目標誤差は`0.256409`度、別目標は`105.16`度から`0.573347`度へ収束した。
- Windows Release buildと専用回帰は成功。既存`--animation-clip-regression`は今回の対象項目を含む大半が成功したが、既存のcharacter animation migration期待1件が失敗した。`--motor6d-gyro-regression`もMotor6D項目は成功したが、既存のGyro期待4件が失敗しており、いずれも今回のAnimation一時停止変更とは別件として残る。

### 2026-09-21: CanTouchと物理contactのfilter分離

- キャラクターの`CanTouch=false`でラグドール後の動作制限が消える実機観測を受けて再調査した。Humanoid内部は`Touched`を購読していなかったが、Touch検出のため通常shapeを`CanTouch`で既定collision filterへ参加させ、物理contact候補を後段custom filterで拒否していた。この設計ではCanTouchが物理broad phaseへ混入するため、Motor6D bodyへ影響し得た。
- Box3D shape filterを物理用categoryとTouch sensor用categoryへ分離した。`CanCollide=false, CanTouch=true`の通常shapeはTouch sensorからだけ訪問可能で、通常shape同士はbroad phase段階でcontact候補にならない。sensor eventには従来どおり通常shapeをvisitorとして使うため、CanCollide=falseの領域侵入でもTouched/TouchEndedを維持する。
- 旧`--touch-event-regression`はbody生成前に速度を設定していたため通過試験が実際には動かず、途中の`CanCollide`もsetterを通さずnative filterを更新していなかった。fixtureを修正し、`CanTouch=true, CanCollide=false`で位置`-5`から`6.99999`へ速度`2.0`を完全維持して通過し、Touched/TouchEndedが各1回発生することを検査するようにした。
- Windows Release build、`--touch-event-regression`、`--ragdoll-motor-recovery-regression`、`--character-hover-regression`はすべて成功。ラグドール回帰はCanTouch=trueのまま復帰後Animationと別Transformの両方へ誤差0度で収束した。

### 2026-09-21: Touch判定のlistener駆動化

- Cube数に比例して常時作られていたBox3D Touch sensor shapeを、`Touched`または`TouchEnded`にLuau listenerが存在するBaseCubeだけが保持する設計へ変更した。最初の購読でsensorを生成し、両signalの最後の購読解除でsensorと関連する内部Touch記録を破棄する。通常shapeは購読のない相手をvisitorとして検出できるため、通知相手側の購読は不要。
- 両Cubeが購読する場合は両方向のsensor overlapを許可し、既存のCubeペア記録でbegin/endを各1回へ集約する。これにより片側だけが購読する場合もポインタ順序に依存せず検出できる。
- `--touch-event-regression`へnative sensor shape数の検査を追加した。未購読時0、Luauの`Touched:Once`/`TouchEnded:Once`接続後1、両イベント発火による最後のlistener解除後0を検証する。対象C++のGCC C++23 syntax check、`git diff --check`、Windows Release build（Recubin、RecubinEngine、RecubinTest）は成功。限定回帰はWSLのWindowsプロセス起動が`UtilBindVsockAnyPort:309: socket failed 1`となり未実行。次の一手はWindows Terminalから`build\Release\RecubinTest.exe --touch-event-regression`を実行すること。

### 2026-09-21: Editor Profiler panel

- ViewメニューへProfilerを追加し、中央Dockのタブとして描画・物理計算・スクリプト実行のCPU時間を別々の折れ線グラフで表示するようにした。各グラフは直近240フレームの現在値、平均、最大をms単位で示す。
- 既存`FrameProfiler`を1秒ログ集計だけでなく、動的確保を伴わない固定長リングバッファへ各フレーム値を保存する実装へ拡張した。`physics`と`luau`は既存の実処理境界を使用し、`render`は全ViewportとImGui描画を含みメインウィンドウのswap/VSync待ちは除外する新しい区間とした。
- `ProfilerPanel`、Localization、EditorManager、文書を追加・更新した。対象C++のGCC C++23 syntax checkと`git diff --check`、Windows Release build（Recubin、RecubinEngine、RecubinTest）は成功。GUI上のメニュー開閉、Dock配置、実負荷グラフは手動確認待ち。
- 実機計測でRenderingが平均35.77ms、Physicsが実行中約3ms、Scriptsが約0.01msとなり、主因が描画側と判明した。ProfilerへShadow、Main Geometry、Surface Marks、各Extra pass、Editor UI、Swap/VSyncの現在・平均・最大テーブルと、描画／カリング／インスタンシング／Shadow Cube数を追加した。親区間と子区間の重複はUIと文書で明示している。追加後のGCC C++23 syntax check、`git diff --check`、Windows Release build（Recubin、RecubinEngine、RecubinTest）は成功。内訳値の実機採取は次の手動確認事項。

### 2026-09-21: Shadow instancing and cascade culling

- Shadow depth passはTexture、Decal、Triplanar、alpha等の見た目状態を参照しないため、Cube/Cylinder/Sphere/TriangularPrismを形状クラス単位で専用バッチへ集約した。メインパスの見た目を伴うインスタンシング条件は変更していない。depth shaderまたは形状VAOが利用できない場合は個別描画へフォールバックする。
- 各shadow cascadeのlight-space frustumに対してcasterのbounding sphereを検査し、外側のインスタンス／個別casterを描画前に除外する。ProfilerのDraw Countersへcascade単位の`Shadow Cubes Culled`を追加し、バッチ化とカリングの効果を実測できるようにした。
- `Renderer.cpp`、`Renderer.hpp`、ProfilerのLocalization/UI/文書を更新した。対象C++のGCC C++23 syntax checkと`git diff --check`、Windows Release build（Recubin、RecubinEngine、RecubinTest）は成功。実機での影の欠落がないこととShadow時間の改善値はWindows側で手動確認待ち。

### 2026-09-21: GUI transparent/invisible draw skipping

- `GuiObject::hasRenderableContent()`を追加し、`Visible`、背景アルファ、TextColorアルファ、画像テクスチャ、GUI子孫から実際に可視ピクセルがあるかを共通判定する。透明な背景を持つSurfaceGuiでも、可視TextLabelなどの子があれば描画を維持する。
- SurfaceGuiは可視内容がない場合にFBOベイクを行わず、Cube側でもそのSurfaceGuiの合成を無視する。TextLabelなどのScreenGuiObjectは透明な背景・文字をImGui draw listへ追加しない。不可視要素の早期returnとボタンの入力経路は従来どおり維持する。
- `--gui-visibility-regression`を追加し、透明／不可視TextLabel、透明背景＋可視テキストのSurfaceGui、SurfaceGui非表示を検査する。`GuiObject.cpp`、`Cube.cpp`、`Renderer_GUI.cpp`、`test_main.cpp`、関連文書を更新。GCC C++23 syntax check、`git diff --check`、Windows Release build（Recubin、RecubinEngine、RecubinTest）は成功した。WSLからの回帰実行は`UtilBindVsockAnyPort:309: socket failed 1`でWindowsプロセスを起動できず、Windows側での実行待ち。
- 透明SurfaceGuiの親背景と子GUIを分離判定する`hasRenderableOwnContent()`を追加した。親背景が完全透明なら透明クリアへ切り替え、可視子だけをベイクする。親と子の両方に内容がなければ従来どおりFBO処理とCube合成を省略する。追加修正後のGCC C++23 syntax check、`git diff --check`、Windows Release build（Recubin、RecubinEngine、RecubinTest）は成功した。WSLからの`--gui-visibility-regression`は引き続き`UtilBindVsockAnyPort:309: socket failed 1`で起動できず、Windows側での実行待ち。

### 2026-09-21: SurfaceGui static bake cache

- 各SurfaceGuiへ最後に正常ベイクした内容署名を保持するランタイムキャッシュを追加した。キャンバスサイズ、自身の可視背景、直接子の既存描画順、class/name、Norm/Position/Size、可視背景、文字・有効フォントサイズ・フォント選択、画像path/texture IDから署名を計算する。入力専用`Active`や透明で描画されない色などは署名対象外。
- `Renderer::bakeSurfaceGui()`は有効なFBO/texture、同一寸法、同一署名が揃えばGL状態変更・FBO clear・ImGui draw-list生成前にreturnし、既存textureを再利用する。`Visible=false`や描画内容なしでは既存texture/cacheを保持するため、内容が同じまま再表示した場合も再利用できる。FBO完全性をベイク前に検査し、失敗はpath/status/size付きwarningとして観測可能にした。
- 1681 SurfaceGuiのhot pathで毎フレームvector確保・sortが起きないよう、既存unordered children走査順を署名と描画で共通使用する。既存の重なり順も変更しない。Cube合成側もSurfaceGui自身と実際にベイクされる直接子の可視判定へ揃えた。
- Profilerへ`SurfaceGui Bakes`時間、`SurfaceGui Baked`、`SurfaceGui Reused`カウンタを追加した。`--gui-visibility-regression`へ描画プロパティ変更時の署名更新、入力専用状態の除外、既定フォントサイズ追跡、cloneでのruntime cache非継承、非表示からの再利用契約を追加した。
- 対象C++ translation unitのGCC C++23 syntax checkと対象差分の`git diff --check`、Windows Release build（Recubin、RecubinEngine、RecubinTest）は成功。更新後の`--gui-visibility-regression`はWSLの`UtilBindVsockAnyPort:309: socket failed 1`でWindowsプロセスを開始できず、Windows側での実行待ち。
### 2026-09-21: dormant SurfaceGui instancing / FPS cadence / VSync diagnostic

- SurfaceGuiがCube面を実際に上書きする条件（Visible、有効なベイクtexture、自身または直接子の可視内容）を`contributesBakedVisualOverride()`へ共通化した。Cube個別描画とメインパスのインスタンシング判定が同じ述語を使うため、透明・非表示・未ベイクSurfaceGuiが存在するだけではCubeを個別描画へ落とさない。
- FrameProfilerへswap後の`endFrame()`間隔を保持する240フレーム固定長リングを追加した。Profiler上部は現在FPS、`1000 / 平均frame ms`による平均FPS、平均frame msを表示し、VSync/idle待機を含む実cadenceを確認できる。
- Settingsへ既定ONのVSync診断トグルを追加し、メインcontextへ即時適用する。`editor_settings.yaml`の`Preferences.VSync`へ保存・復元する。
- `--gui-visibility-regression`へSurfaceGui override述語のtexture/Visible/child可視性遷移とclone cache非継承を追加した。変更した9 translation unitのGCC C++23構文検査と対象差分の`git diff --check`、Windows Release build（Recubin、RecubinEngine、RecubinTest）は成功した。WSLからの`--gui-visibility-regression`はWindowsプロセス開始前に`UtilBindVsockAnyPort:309: socket failed 1`で停止したため、Windows側での実行待ち。

### 2026-09-21: Workspace描画対象キャッシュ

- Workspaceへ全Instanceと型別描画対象のraw pointer vector cacheを追加し、`Instance::setParent()`のsubtree attach/detachで登録・解除するようにした。BaseCube、SurfaceMark、LightSource、ParticleEmitter、ScreenGuiObject、WorldGuiObject、SurfaceGui、PostEffect、Highlight、Lighting、Weather、Terrainを保持する。
- Renderer / Renderer_GUIのLighting、Cube、SurfaceMark、Shadow/Main対象、制約、Physics debug、Particle、Terrain、Weather、PostEffect、ScreenGui、SurfaceGui、BillboardGui / ProximityPromptのWorkspace全体再帰収集をcache走査へ置換した。Sun/Moon/Skyboxの残存Workspace直下走査もBaseCube cacheへ置換した。SurfaceGui等の直接子走査と選択対象の局所走査は維持した。
- Profilerの`tree*Nodes`はcache entry処理数として継続し、報告書を実装後の意味へ更新した。`treeSurfaceMarks`のCPU計測区間がcache収集を含むよう修正した。
- 検証: `py .\\build.py build`でRecubin / RecubinEngine / RecubinTestのWindows Release build成功、`git diff --check`成功、Diagnosticsエラーなし。キャッシュのreparent/clone/delete回帰と実機Profiler値の確認は未実施。次の一手はWindows側で既存GUI/Scene回帰とProfilerのCastShadow・SurfaceGui比較を実行する。

### 2026-09-21: asynchronous GPU profiler timing

- Rendererが4フレーム分のOpenGL timestamp query ringを所有し、メインcontextの描画全体と、最初の有効ViewportのShadow、Main Geometry、Surface Marks、ExtrasをGPU側で計測する。最終queryの`GL_QUERY_RESULT_AVAILABLE`だけをpollし、全slotがpendingなら採取を省略してCPUを待たせない。query非対応、生成GL error、0 IDはwarningとProfilerのUnavailable表示で観測可能にした。
- FrameProfilerにCPU `endFrame()`から独立したGPU履歴を追加し、未解決queryを0msとして混入させない。ProfilerはGPU各区間のCurrent/Average/Peakと非同期・VSync非含有の説明を表示する。FPSヘッダーを固定し、長い本文だけをchild regionでスクロールする。
- `--frame-profiler-regression`を追加し、OpenGL contextを要しないGPU履歴のcurrent/average/peak、固定長wrap、CPU endFrameから0が混入しないことを検査する。FrameProfiler、ProfilerPanel、Localization、Renderer、test_mainのGCC C++23 syntax checkと対象差分の`git diff --check`は成功。Renderer/test_mainはWSLのGLU header不足を避けるため`-DGLEW_NO_GLU`で検査した。Windows Release build（Recubin、RecubinEngine、RecubinTest）は成功。WSLからの専用回帰はWindowsプロセス開始前に`UtilBindVsockAnyPort:309: socket failed 1`で停止したため、Windows側での実行待ち。

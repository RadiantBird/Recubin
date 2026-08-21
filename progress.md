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

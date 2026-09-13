# 仕様書
---

## アニメーション保存形式（v1）

`Animation` InstanceをScene Tree上の正式なアニメーション資産および参照主体とする。
`AnimationClip`は`Animation`が内部に所有する唯一のランタイムトラックデータであり、
Scene Treeへ直接公開しない。内蔵R6アニメーションと`.rcanim`は同じClip評価器を使い、
Rig固有のJoint/Pivotバインドオフセットとキーの姿勢変化を分離する。旧Scene埋め込み
Animationもロード時に`AnimationClip`へ変換し、別のトラック表現を併存させない。

`.rcanim`はYAMLで、`recubin.type: animation`および`recubin.version: 1`を必須とする。
`rig: R6`、`space: joint_delta`のキーはJointのローカル差分である。エンジン標準のR6
アニメーション資産は`assets/anims/`配下（Walk、Jump、Equipなど）に配置する。
Humanoidは`WalkAnimation`、`JumpAnimation`、`EquipAnimation`から対応する`Animation`
Instanceを明示的に参照し、StarterCharacterからPlayerCharacterへcloneする際はclone先の
Animationを参照するように接続し直す。ユーザーが指定した有効なAnimationを常に優先する。

参照先の未検出、破損、type不一致、未対応version、データ不正があっても、保存された
Animation参照、Animation Instance、`.rcanim`ファイルおよびScene Treeを変更しない。
SceneをDirtyにもせず、その実行中だけコンパイル済みの内蔵R6 Clipへフォールバックする。
ロード状態と使用中のフォールバックはPropertiesおよび警告で確認できるようにする。
Instance名やContentPathの文字列から標準Animationかどうかを推測してはならない。

Scene YAMLは`recubin.type: scene`、`version: 0`を使用する。ヘッダーのない旧Sceneは
暗黙のversion 0として読み込むが、version 1以上や別typeは拒否する。旧Sceneの自動移行は、
`migrations.character_animation_bindings.version: 1`が未記録で、かつHumanoidの対象参照が
空欄の場合に限り、標準Animation Instanceと参照を一度だけ補完する。移行versionを記録した後は、
参照が空欄、欠損または破損していても自動挿入や再設定を行わない。移行後にユーザーが標準参照を
差し替えたり削除した結果を尊重する。標準参照を再設定できるのは、ユーザーが明示的に
`Restore Default Animations`を実行した場合だけとする。

## 特殊なインスタンス
  `EnableIOAPI`が有効な場合のみLuauへ`IO.ReadText`/`ReadBytes`/`WriteText`/`WriteBytes`/`AppendText`/
  `AppendBytes`/`Exists`/`IsFile`/`IsDirectory`/`List`/`CreateDirectory`/`Copy`/`Move`/`Remove`/`RemoveTree`
  を公開する。読取りは値または状態、変更系の成功は`true`、権限不足・不正パス・I/O失敗はエラーとする。
  相対パスは起動時のポータブルroot直下、External許可時のみ絶対パスを許可し、`..`・symlink脱出を拒否する。RemoveTreeは
  ポータブルroot、ホーム、ドライブ／FS rootを保護する。TextFile.Contentを含むデータサイズ上限は128 MiB。
  `EnableIPCAPI`では`Connect`/`Send`/`Receive`/`Close`のstubを公開するが未実装エラーを返す。拡張同意receiptは
  構成versionとIO/IPC/External権限集合を保存・比較し、Editorと`--editor-test`では警告とreceiptをバイパスする。

## システム拡張API
- **System**: シングルトン。常に1つのみ存在。Insert Objectリストには登録しない。
  `ApplicationId`（UUID）と、`EnableIOAPI`、`EnableIPCAPI`、`EnableExternalFileAccess`の
  システム拡張フラグを保持する。これらはエディターでのみ変更でき、Luauからは読み取り専用である。
- **TextFile**: `PhysicalFileInstance`を継承する永続テキスト資産。`ContentPath`は配布時の
  初期seed、`StorageId`はユーザー領域のmutable copyを識別するUUIDである。`Content`は全文を
  読み書きでき、I/O API権限を要求しない。Luauの`Instance.new`では生成できず、エディターの
  Insert ObjectまたはSceneロードからのみ生成される。複製時は新しい`StorageId`を割り当てる。
- **Workspace**: 複数インスタンスを持つ。切り替え可能。
- **StarterCharacter**: System直下に置く、キャラクターのテンプレートを保持するだけのコンテナ。
  中にHumanoid・Root(Cube)・その他のCube/Sphereを通常のInsert Object操作で組み立てる。
  Play開始時、この子要素が新規ModelにcloneされてWorkspaceに追加される。Offlineでは
  `"PlayerCharacter"`、ネットワークHost/ClientではHost割り当てPeerIdに基づく
  `"PlayerCharacter_<PeerId>"`となる。StarterCharacterが存在しない場合は、既定のリグ(旧来
  ハードコードされていたもの)を持つStarterCharacterが自動的にSystem直下に生成される。
- **SpawnLocation**: `Cube`派生の出現地点。既定値は`Name=SpawnLocation`、`Size=[8,1,8]`、
  白、`Anchored=true`、`CanCollide=true`、`Enabled=true`とし、通常のCubeとして描画・衝突する。
  active Workspaceの全子孫にある`Enabled=true`のSpawnLocationをfull path昇順で選ぶ。
  ローカルpeer 0とpeer 1は先頭、peer 2以降は`(PeerId-1) % 件数`を用いる。
  CharacterのRootはSpawnLocationのfull CFrameを引き継ぎ、SpawnLocation上面へ
  `Spawn.Size.y/2 + Root.Size.y/2`だけ上げる。Model全体は元のRoot local CFrameの逆変換を
  合成して配置し、各パーツの相対姿勢を維持する。候補が無い場合はRootをワールド原点へ置く。
  `Name=Spawn`の通常Cubeを暗黙変換する旧形式互換は持たず、シーン側でClassNameを明示的に
  `SpawnLocation`へ変更する。

## 単位系
- **Roblox erik_stud(0.05 meterに等しい)**
- エンジン内部・PhysXともにstud値をそのまま使用する（座標/サイズ/速度の変換はしない）
- PhysXのPxTolerancesScaleをstud基準(length=20, speed=length*9.81)に設定し、
  1m=20studの比率をPhysX側に伝えることで内部の許容誤差・閾値を適切にスケールする
- 重力など、現実のSI単位の物理定数だけは個別にstud相当へ変換する
  (例: 9.81 m/s² -> 196.2 stud/s²、`include/Math/Units.hpp`参照)

## プロパティ操作
  - すべてのR/W/RWプロパティはエディターで表示される（理由があればその限りではない）
  - 複数選択時は共通プロパティの和集合を表示する
  - プロパティにはエディターで実行できる関数のボタンも含める(ScriptのRestartなど)
  
## 座標系
  座標はエディター上では親を中心にする

  Workspace/
    A/
      B/
        C/
          D/
  ---

  となっている場合、DはCを中心として、CはB,BはA、Aはワールド座標。
  この法則はModel配下のBaseCube、子Modelに適用される。
  「座標を再計算」ボタンを追加する。いったんワールド座標にすべてを展開し、
  それをこの座標階層ごとに再計算して代入する。
  また、

  ---
  A/
    Folder/
      B
  ---
  のように、Spatialを継承していないインスタンスが間にある場合、
  Bの座標系は親をたどってSpatialを継承した最初のインスタンスの座標系とする。
  この場合はBはAに属する。

  物理エンジンはワールド座標で計算。

  座標はCFrameとして扱う。Positionを直接計算しない。
  Positionはローカル、WorldPositionはグローバルとして扱い、Positionは座標階層を元に計算して代入される。

  実行時にローカル座標系はすべてワールド座標系に変換される。
  スクリプトの代入もローカル座標系で設定したらワールド座標系に変換してから代入する。

## Weld(溶接)
  Parentによる親子関係は座標系・インスタンス階層のみを表し、
  親が移動しても子は追従しない。
  BaseCube同士を追従させる場合はWeldを使用する。

  WeldされたBaseCubeはひとつのアセンブリとして扱う。
  Weldで接続されているBaseCube同士には当たり判定を発生させない。
  アセンブリ外のBaseCubeとは通常通り当たり判定を行う。

  Weldで接続されたグループ内にAnchored=trueのBaseCubeがひとつでも存在する場合、
  そのアセンブリ全体をAnchoredとして扱う。

  AnchoredされたBaseCubeが存在しない場合、
  アセンブリ全体をひとつの剛体として物理エンジン上で扱う。

  Weldによって座標系の意味を変更しない。
  Weldは追従関係のみを担当し、ローカル座標・ワールド座標の変換は座標系側で処理する。

## NoCollision
  NoCollisionは指定した2つのBaseCube間の当たり判定のみを無効化する。

  Weldと異なり、追従や剛体の結合は行わない。

  WeldされたBaseCube同士は元から内部衝突を行わないため、
  同一のペアにNoCollisionを設定した場合は実質的に効果を持たない。

## Character
  Characterも通常のBaseCubeとWeldによるアセンブリとして扱う。

  RootをCharacterの基準となるBaseCubeとする。
  Character自身による移動、ジャンプなどの操作はRootを基準として物理エンジンへ反映する。

  腕、脚、頭などの各部位はRootを基準としたアセンブリの一部として扱う。
  外部オブジェクトからの衝突は各部位に対して通常通り発生する。

  Character専用の物理挙動は必要以上に追加せず、
  通常のWeldアセンブリと同じ規則を優先する。

  Rootの直立と方位はGyroで制御する。Character用GyroはX/Zを0度へ固定し、Yだけを入力方向へ
  更新する。移動入力が無いフレームではY目標を更新せず、最後の方位を維持する。

  既定Rootの描画Sizeは`(2,2,1)`、中心は従来の`basePos`を維持する。接地時はRoot中心から下向きに
  一度floorをsampleし、Humanoid.HipHeightのdistanceをGroundHeight controllerで保つ。HipHeightが
  明示されていない場合、最初の有効なfloor distanceを初期値として採用し、Rootの初期高さを補正しない。
  controllerは各dynamic R6 bodyへ同じ上向き加速度をmass比例のadditive Forceとして与える。接地状態は
  HipHeightを接地状態の捕捉ゲートとして使い、捕捉後は微小なfloor distance揺れでは接地状態を反転させず、
  床が消えるかRootが上昇したときに解除する。HipHeightが高い場合も、その目標距離までraycast範囲を拡張する。
  重力相殺は現在のWorkspace.Gravityから算出する。遠距離の床へ吸着せず、jump上昇中は停止し、
  下降してlanding captureへ入った時だけ再開する。SpawnLocation上では明示されたHipHeightをRoot中心から
  地面までの距離として使う。未設定の場合はSpawnLocation選択でRootの初期Yを変更せず、その後の最初の
  floor sampleでHipHeightを初期化する。
  GroundHeight、接地、Truss中の重力設定は操作入力とは独立した物理更新として毎フレーム評価する。
  したがってFree/Program中もCharacterHoverForce、重力、衝突、LiquidCubeの液体浮力は維持される。

## Gyro
  Gyroは1つのPartへworld基準の角度制御を加える単一body constraintとする。X/Y/Zはそれぞれ
  Enabled、TargetAngle（度）、MaxTorque、MaxAngularSpeed（度/秒）を独立して持つ。無効な軸へは
  トルクを加えず、複数軸を有効にした場合も各軸の設定を独立して適用する。

  Characterの方位角はQuaternionのEuler分解を経由せず、水平な方向ベクトルから直接求める。
  これによりX/Zの傾きがY目標へ混入しないようにする。

## Tool
  ToolのHandleと、装備する腕の基準となる回転は一致させる。
  Toolを装備するためだけの特殊な回転補正をWeldや座標系に持たせない。

  Toolの装備による腕の姿勢変更はAnimation側で処理する。

## Animation
  装備、ジャンプなどの視覚的な姿勢変更は物理や座標系ではなくAnimationで処理する。

  装備時は通常姿勢から装備姿勢へ回転を補間する。
  ジャンプ時も腕や脚の回転を補間し、着地後に通常姿勢へ戻す。

  基本的に関節の回転を補間する単純な方式とし、
  見た目を成立させるためにRoot、Weld、CFrameへ特殊な補正を追加しない。

## 方角
- +Zが東
- -Zが西
- +Xが北
- -Xが南
のはず。

## インスタンス(Instanceクラスを継承したクラス)
- 基本的に親(Parent)を持つ
- 親が所有権を持つ
- 親が削除された場合、子も再帰的に削除

- 新規クラスは自動的にエディターに公開される
- エディターに基本的なプロパティを公開する(エンジン内部の状態、セキュリティ上公開してはいけないものなどを除く)
- Luau側に基本的なRead/Writeプロパティをバインディングする
- エディターの「Insert Object」リストに登録される(一部の抽象クラスは除外)

## ユーザー(Userクラス)
- clone後のキャラクター本体(`character`, Model)を持つ。個別の身体パーツへの参照は持たず、
  移動・ジャンプ・接地判定・歩行アニメーションは`character`内のHumanoidに委譲する
- Cameraを持つ
- 入力を管理する
- `MovementInputEnabled`/`CameraInputEnabled`/`HotkeyInputEnabled`/`ToolInputEnabled`は既定trueの保存対象で、
  対応する組み込み操作のみをgateする。`User.Input`の生ポーリングとLuau Direct APIは常に利用可能。
- `CursorType` は `Default`/`Type1`〜`Type10` を選択し、各スロットはPNG/JPEG/BMP/TGAの
  `ContentPath`、0以上の整数ホットスポット、長辺の論理サイズ`Size`を保存する。`Size`は既定32px、
  1〜512pxで、画像は縦横比を維持してcontent scale込みの物理サイズへbilinearリサイズする。
  共通CursorImageProcessorがAssetGuard確認、RGBA8読込、mtime／設定／DPIキャッシュ、Hotspot変換を担当し、
  入力バックエンドはrevision付きの完成RGBAデータからOSカーソルを生成するだけとする。
  ゲーム領域外ではUser指定カーソルを適用しない。
- F1-F12は`User.Input`へ固定名で通知し、組み込み動作はF8のMouseLock切替だけとする。MouseLockはprimary
  viewportの中心client座標を使い、フォーカス喪失で解除する。
- `CharacterSmoothing`は移動方向・向きの補間率（既定`0.15`）。`1`で補間なし、`0`で目標へ追従しない。
  YAML/Luauからの設定値は`[0,1]`へクランプし、非有限値は既定値に戻す。ネットワークの入力パケットには
  含めず、HostはリモートUser生成時にシーン権威のローカルUser値を引き継ぐ。
- ControlMode
    - エディターではデフォルトでFree
    - ゲームランタイムではSystem.DefaultCameraMode（Free/Character/Program、既定Character）を
      Userの起動カメラモードへ適用する。旧シーンや未知の値はCharacterへフォールバックする。
    - Humanoid死亡中もカメラ入力とLキーのモード切替を受け付ける。Characterではキャラクターを移動・追従させずその場でカメラを回転し、Freeではカメラを自由移動できる。Free移動中も死亡ラグドールの姿勢を上書きしない。
    - ControlModeは入力操作とカメラ制御だけを切り替える。PlayerCharacterの物理状態更新は全モードで継続する。
      CharacterからFree/Programへ移行した瞬間は、Character入力が残した水平速度と角速度を0へ戻し、Character専用のYawForceを無効化する。ジャンプ・落下の垂直速度は維持する。
- `CharacterAdded`(Signal): 新しいローカルCharacterがspawnされるたび発火する(初回spawn +
  死亡respawn全て)。Luau側にはspawn直後のcharacter(Model)が引数として渡される
  (この時点ではまだWorkspaceに未追加。Root等のパーツ参照はresolveParts済みで取得可能)。
  respawnを跨いで参照を使い続けたいスクリプトは、起動時の`WaitChild`/`FindChild`で一度だけ
  参照を取るのではなく、この signal で都度取り直すこと。
- StarterCharacterからcloneしたローカルCharacterは、保存されたテンプレート値にかかわらず
  Rootだけを`Anchored=false`、`CanCollide=true`へ正規化する。その後SpawnLocationで配置してから
  `CharacterAdded`を発火する。死亡respawnでもactive WorkspaceからSpawnLocationを再選択する。
  Play Hereの初回だけは明示されたModel.PositionをSpawnLocationより優先し、respawnは通常選択へ戻る。


## GUI（ScreenGui/SurfaceGui/BillboardGui）
- ScreenGuiObject と WorldGuiObject は共通基底 GuiObject（Active/Size/Norm/Visible/
  BackgroundColor/ZIndex/Transparency を保持）を持つ。GuiObject はファクトリ非登録の
  抽象基底で Instance.new 不可。
- BillboardGui は SizeMode（Screen/World）と Offset（親オブジェクトのローカルVector3）を持つ。
  Screen は既存どおり Size を画面ピクセルとして扱い、World は Size をワールド単位として
  投影する。World 時は子GUIも親パネルと同じ投影倍率で縮小する。
- TextLabel/TextButton は `TextContent`（Text/TextColor）、ImageLabel/ImageButton は
  `ImageContent`（Image）をコンポーネント（HasA）として保持する。描画・エディターは
  `GuiObject::textContent()`/`imageContent()` で問い合わせて分岐を一本化する
  （`Renderer_GUI.cpp`の`drawGuiContent`）。ボタン性は GuiButton 基底（Activated シグナル）
  が担う。クラス名・YAMLキー（Text/TextColor/Image）・Luauプロパティ名は変わらない。
- **SurfaceGuiの実際のベイク解像度は、SurfaceGui自身の`Size`比率ではなく、親BaseCubeの
  対象フェイスの物理サイズ比率に合わせて決まる**（`Renderer_GUI.cpp`の`computeSurfaceGuiLayout`）。
  例えば`Size=[200,100]`のSurfaceGuiを1x1x1の立方体に貼ると、実際のFBOは200x200になり、
  本来の200x100キャンバスはその中でレターボックス（上下に余白）されて焼き込まれる。
  UIをデザインする際は「SurfaceGuiのSizeがそのままアスペクト比になる」わけではないことに注意。
- **`bakeSurfaceGui`はベイク後のテクスチャを手動で左右反転している**（列方向のみ）。
  上下方向はOpenGLのFBO読み書き規約（`glGetTexImage`/テクスチャサンプリングは「行0=下端」）
  により暗黙に反転される。3D面へのUIテクスチャマッピングを新たに実装する際は、
  X軸（コード側の明示的な反転）とY軸（API側の暗黙の反転）を別々に検証すること。
  片方だけ検証して安心すると、もう片方で座標がズレるバグを埋め込みやすい。
- **GUIの`InvisibleButton`系のImGui IDはインスタンスポインタ由来で生成すること**
  （`Renderer_GUI.cpp`の`drawScreenGuiElement`/`drawWorldGuiChildren`）。
  インスタンス名（`Name`）をIDに使うと、同名インスタンスが複数存在する構成
  （コピペ量産、テンプレート的な使い方）でImGuiのID衝突（"conflicting ID"警告、
  クリック判定の誤動作）を起こす。
- Explorer の Insert Object / Group は検索付きクラスピッカーから選択する。分類は既存分類に加えて Container、File、Script を持ち、検索は大文字小文字を区別しない部分一致とする。
- Explorer の Replace Instance は単一ノードを同名の選択クラスへ置換する。共通プロパティは移送し、互換性のないプロパティは破棄する。子要素は同一オブジェクトを維持して新しい親へ移動し、Undo/Redo で完全に復元する。System、Workspace、親を持たないルートは対象外とする。

置換確認は現在選択中のクラスに紐づけ、選択クラスが変わった場合は再確認する。TextFileの
グループ化確定・取消では保留対象を破棄する。PropertyRegistryの移送は継承関係のないクラス間でも
同名同型を対象とし、型不一致は破棄する。子identity、typed参照、Undo/Redo契約は維持する。
Soundの3D位置は完全なWorldCFrameから求め、AudioServiceの段階的初期化失敗はrollbackし、
teardownは冪等にする。startup/editor settings/terrain YAMLの失敗は共通結果でログへ報告し、
破損データの空データ上書きを禁止する。

GUI automation専用モードの未保存変更確認danger cooldownは0秒、通常Editorは3秒とする。
Class Pickerのpopupは選択変更時に旧クラスの承認だけを破棄して維持し、確定・取消時に保留状態を閉じる。
## Play 中の複数 Workspace

Play 中はローカル User の Character が属する最寄りの Workspace をアクティブ Workspace とする。
Luau の `Character.Parent` 変更は同一フレーム内に検出し、カメラ、入力、物理、Terrain、Weather、Particle、
Replication、Luau の `workspace`、Primary Viewport、Explorer を追従させる。Character が未生成または
一時的に Workspace 外にある場合は現在のアクティブ Workspace を維持する。Free/Program カメラの変換は
移動先へテレポートせず、セカンダリ Viewport は開いた Workspace に固定する。

## Editor GUI automation

`--ui-automation` を指定したEditorだけがstdin操作と意味IDによるUI target登録を有効化する。
通常起動ではreader、入力注入、target登録、captureはno-op。captureはmain viewportの
default back framebufferをphysical pixel sizeでRGBA PNGとして保存する。

## Scene Autosave / Crash Recovery

Windows版Editorと平坦portable配布のmacOS Editorは実行中の`Recubin`と同じディレクトリの
`.autosave/<scene-name>/`へRecoveryと世代snapshotを保存する。実行ファイル位置は
`GetModuleFileNameW`で解決し、失敗時だけ`argv[0]`の絶対化、起動CWDの順でfallbackする。
macOSは実行ファイル隣に`.autosave` marker、`RecubinEngine`、`shaders`、`assets/fonts`が揃う場合だけ
portable配布と判定し、CWD、`editor_settings.yaml`、Autosave rootを実行ファイル隣へ揃える。
開発実行は従来の起動CWDを維持する。旧CWD配下のAutosaveは移行・走査・削除しない。
Recoveryは1秒debounce、
snapshotは5分周期で、保存は一時ファイルからatomic replaceする。異常終了時に残るlockと有効な
Recoveryは次回起動時に最新候補をモーダル表示し、Recoverは正式Sceneを変更せず復旧内容を未保存
状態で開く。候補走査では現在のactive sessionを除外し、lockだけが残ってRecoveryが未作成・欠落の
場合は正常な非候補としてログなしで無視する。filesystem検査失敗と破損Recoveryは具体的なpath付きで
エラーログを出す。正常終了・正常Scene切替・DiscardではlockとRecoveryを削除しsnapshotは保持する。
設定とAutosaveの実I/O失敗はOS標準ダイアログへ日英で操作、絶対path、理由を示す。同一操作/pathの
連続失敗は一度だけ表示し、成功後に再発した場合は再通知する。欠落・破損YAML・serialization失敗はログのみとする。

macOS Studio配布は`.app`ではなく`RecubinStudio/`直下に`Recubin`、`RecubinEngine`、assets、shaders、
文書、空の`.autosave/`を置く平坦zipとする。再packageはローカルのAutosave内容を保持するがzipへは含めない。
Studio用の両Mach-Oは個別にad-hoc署名する。ゲームPackagerが作るmacOS `.app`は変更しない。

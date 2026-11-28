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

`Animation`の`Source`は内部状態を保存するための任意のSceneプロパティである。組み込みClipを
所有するAnimationだけが`Source: BuiltIn`を出力し、`ContentPath`は空のまま保存する。読み込み時は
この値を復元し、組み込みClipと空のContentPathを持つユーザー作成Animationを区別する。
ファイル参照のAnimationおよび旧Sceneの埋め込みAnimationは従来通り`ContentPath`または
`Tracks`から復元され、Sourceキーを推測して補完しない。未知のSource値は無効な組み込み指定として
警告を出力して観測可能にし、別のSourceへ黙って変換しない。

Scene YAMLは`recubin.type: scene`、`version: 0`を使用する。ヘッダーのない旧Sceneは
暗黙のversion 0として読み込むが、version 1以上や別typeは拒否する。旧Sceneの自動移行は、
`migrations.character_animation_bindings.version: 1`が未記録で、かつHumanoidの対象参照が
空欄の場合に限り、標準Animation Instanceと参照を一度だけ補完する。移行versionを記録した後は、
参照が空欄、欠損または破損していても自動挿入や再設定を行わない。移行後にユーザーが標準参照を
差し替えたり削除した結果を尊重する。標準参照を再設定できるのは、ユーザーが明示的に
`Restore Default Animations`を実行した場合だけとする。

Scene YAML（`.rcbn`）の各`Children`配列は、Explorerと同じカテゴリ優先度、クラス名、
自然なInstance名の順で保存する。同じカテゴリ・クラス・名前のInstanceは、正規化した内容で順序を
決定する。RBXLX変換では、この順序で重複名を一意化する。これは通常のScene保存とRBXLX変換出力の
両方に適用する。プロパティは従来のschema定義順を維持する。

## アセット保存形式（.rcaet v1）

選択したInstance部分木を再利用可能なアセットとして書き出す。ExplorerでInstanceを右クリックして
「アセットを書き出し...」「アセットを読み込み...」、Content Browserで`.rcaet`を右クリックして
「アセットとして読み込み」を実行する。読み込みは右クリックしたInstance（Content Browserでは選択中の
Instance、無ければWorkspace）の子として追加し、1回のUndoで取り消せる。

- 形式: 1行目`#rcaet 1 yaml=<N>`（YAMLコメント。Nは2行目以降のYAMLのバイト数）、続けてYAML、
  埋め込みがあればその後ろに生バイトを連結する。構造のみの場合は末尾領域が無く、全体が通常のYAMLになる。
- YAMLは`recubin: {type: asset, version: 1}`、`Asset: {Name, Roots, Materials}`、`Embedded`を持つ。
  `Roots`/`Materials`のノード形（ClassName/Name/Properties/Children）はSceneと同じ。`type`が`asset`以外、
  versionが1以外は拒否する（SceneとAssetを相互に読み込めない）。
- `Embedded`の各要素は`Source`（YAML内の依存パス文字列と一致）、`File`（バンドル内の相対パス）、
  `Size`、`Hash`（`sha256:<hex>`）、`Offset`（末尾領域先頭からのバイト位置）を持つ。ディレクトリ依存
  （Terrain.DataPath）は`Dir: true`と`Files`の配列で、ディレクトリ内のファイルを個別に格納する
  （`.autosave`は含めない）。サイズ・範囲・ハッシュのどれかが不正なら何も変更せず読み込みを拒否する。
- 書き出しは対象を複製してからシリアライズし、元のツリーを変更しない。部分木の外を指す参照
  （Constraint、Attachment、ObjectValue、Model.PrimaryCube、Humanoidのアニメ等）は空にして警告する。
  例外として`BaseCube.Material`はMaterialServiceのMaterial（画像マップのFileRefを含む）を同梱する。
  System/Workspace/各Service/Users/User/Lighting/UserInput/Eventとランタイムキャラクターは書き出せない。
- 依存ファイルはYAMLのキー（`ContentPath`、`Texture`、`MeshFile`、`Image`、`SkyboxPaths`、`DataPath`等）から
  検出する。書き出しダイアログで「構造のみ」か「依存ファイルを埋め込む」かと、ファイルごとの埋め込みを選ぶ。
  実行形式（.exe/.dll/.bat等）は既定では埋め込まず、赤い警告付きで明示的に選んだ場合のみ埋め込む。
- 読み込みは埋め込みファイルを`assets/imported/<アセット名>/<File>`へ展開し、依存パスを書き換える。
  既存ファイルは上書きせず、同内容なら再利用、内容が違えば`_1`、`_2`の連番で別名にする。`..`・絶対パス・
  ドライブ文字を含む`File`は拒否する。実行形式が含まれる場合は、展開前に確認ダイアログで警告し
  「除外して読み込む」（既定、該当Programの`ContentPath`は空になる）か「展開して読み込む」を選ばせる。
  展開しても自動実行はしない。Undoしても展開済みファイルは残る。
- 同名のMaterialが取り込み先のMaterialServiceにあれば既存を使い、無ければ追加する。TextFileの
  `StorageId`は取り込みごとに新規採番する。ルート名が衝突した場合は連番で改名する。

## シーンのアセット基準ディレクトリ

Scene YAMLの`recubin.assetRoot`は、シーンファイルの親ディレクトリからアセットを解決する
ための相対ディレクトリである。画像、音声、スクリプト、メッシュ、`.rcaet`の展開結果など、
Sceneが参照する外部ファイルは同じ基準で扱う。シーンを別の作業ディレクトリから開いても、
シーンとアセットを同じ相対配置で共有すれば参照先を維持できる。

`assetRoot`が空または存在しない旧Sceneは、互換性のためプロセスのカレントディレクトリを
基準とする。`assetRoot`に絶対パスを指定したSceneは読み込みを拒否する。異なるドライブなど、
基準から相対パスを作れない場所は指定できない。

SceneRuntimeの隔離stage中は、読み込み中だけ新しい基準を適用する。stageに失敗した場合は
以前の基準を復元し、commitが完了した場合だけ新しい基準を残す。基準ディレクトリは実行時の
AssetGuardや外部ファイルアクセス許可を緩和しない。既存Sceneの旧基準を自動探索して修復せず、
修復時は利用者が基準ディレクトリを明示する。

Studio の File メニューから「Set Asset Directory...」を選ぶと、任意の既存フォルダを
基準として指定できる。シーンが未保存の場合は先にシーン保存先を確定する。指定フォルダは
シーンファイルの親からの相対パスとして保存し、別ドライブなど相対表現を作れない指定は拒否する。
「Repair Asset Paths...」は旧 Studio の基準フォルダを利用者に選ばせ、解決できた参照だけを
新基準へ変換する。解決できない参照は保持し、一覧で報告する。旧シーンの自動探索は行わない。

「Save Scene As...」では、アセットを新しいシーン側へコピーするか、元の基準を参照し続けるか、
キャンセルするかを確認する。コピーを選ぶと、参照されたファイルとディレクトリを新基準へコピーし、
参照を新基準からの相対パスへ書き換える。基準外の依存は新基準の`external/`へ収集する。
同名ファイルはバイト一致時だけ再利用し、内容が異なる場合は上書きせず保存を失敗させる。
欠落依存、コピー失敗、シーン書込み失敗では保存を完了せず、変更した参照とシーン状態を復元する。
コピーを拒否した場合は元基準への相対参照を維持し、新しいシーンからも同じ基準を記録する。

Scene の Instance node には、変換などで既知の破損を示す YAML 専用ラベル
`AssetRepairRequired: true`と、理由を列挙する`AssetRepairWarnings`を記録できる。これらは`Properties`には含めず、警告配列は空の場合に出力しない。
Scene 読み込み時には`AssetDependencies`が扱うファイル・sequence・directory参照を各 Instance ごとに検査し、
参照先が存在しない、または期待するファイル種別でない場合も、その Instance を修復対象として扱う。
動的な欠損検出は Instance 名と参照パスをログと Explorer の行ツールチップへ報告する。動的な検出状態は明示ラベルとは別の実行時状態であり、Scene YAMLへ保存しない。空の任意パスは欠損としない。
RBXLX 変換では、利用できない Roblox アセット（空の Texture / Decal の Texture を含む）、未対応 Script API、変換時に破棄した Instance など、
変換警告の対象を出力 Instance の`AssetRepairRequired`と`AssetRepairWarnings`へ記録する。Instance 自体を出力しない場合は、
placeholder Instanceを生成せず、その子を失った最寄りの出力親の警告詳細へ元のClassNameとNameを記録する。
Explorer では警告がある行を淡い黄色で強調し、hover時のツールチップへ変換理由と動的欠損参照を表示する。問題を持つ子孫がある場合は、祖先行のクラスアイコンより前に警告三角形を表示する。警告が3件を超えるか本文が長い場合はツールチップで右クリックメニューを案内し、コンテキストメニューからスクロール可能な警告ダイアログを開ける。ダイアログはOKで閉じる。

## 音声デバッグパネル
Viewメニューの「音声デバッグ」で開く、音声の異常（ノイズ・途切れ・小音量）の調査用パネル。開いている間だけ計測する。
- 出力タップ: `AudioService`がSFX/BGMグループとエンジン出力の間に置くパススルーノード。ブロックごとに
  ピーク・RMS・隣り合うサンプルの最大差（クリック指標）・NaN・クリップ・コールバック間隔を測る。再生音は変えない。
- Sound状態: 毎フレーム、再生位置・再生中・音量（`Volume`と距離減衰後）・リスナーまでの距離・パンを記録する。
  Play/Stop/Reset/Seekの呼び出しは、前後の再生状態とカーソル位置つきでイベントとして記録する。
- Waveform: 音声ファイルを解析し、連射の間隔で巻き戻したときの切断位置の波形の値（クリックの見込み）を示す。
- Test: 連射（Reset→Play）の再現、距離減衰・パンのバイパス、テストトーン、レポート（Markdown）のコピーと
  `logs/`へのMarkdown/CSV保存。
- Soundの距離減衰は`音量 = Volume / (1 + 距離[m] × 0.1)`で、ワールドのstudをメートルへ換算する（20 stud = 1 m、`Sound::STUDS_PER_METER`）。
- デバイス無しのエンジン（`ma_engine_config.noDevice`）を`ma_engine_read_pcm_frames`で駆動すれば、
  実機の音声デバイス無しで同じ計測をテストできる（`--audio-diagnostics-regression`）。

## スクリプトの言語（ScriptExtension）
- `Script` / `LocalScript` / `ModuleScript` は `ScriptExtension`（`Luau` / `Luar`）を持ち、実行時にLuarコンパイラを通すかはこの値だけで決まる（ファイル名や`Name`は見ない）。
- `Path`を設定すると`.luar`→`Luar`、`.luau`/`.lua`→`Luau`に追従する。`.luauc`は変更しない。YAMLに項目が無い旧シーンは`Path`から決まる。
- YAMLは`ContentPath`の後に`ScriptExtension: Luau|Luar`（文字列）で保存する。読み込み時は明示値が`Path`からの追従を上書きする。
- 新規スクリプト作成ダイアログは埋め込みソースまたは外部ファイルを選択できる。既存の`.luau`/`.lua`/`.luar`は、従来どおり外部参照として開くか、本文を埋め込みとして取り込める。
- 埋め込みScriptはScene YAMLに`Embedded: true`と`EmbeddedSource`で状態と本文を保存し、`ContentPath`は空にする。旧`Properties.Source`はPathの別名として維持し、埋め込み本文とは混同しない。埋め込みでない既存Sceneの`ContentPath`形式は変更しない。
- 埋め込みScriptの外部編集用ファイルはストレージルートの`temp/embedded-scripts/__Recubin_<一意ID>.<拡張子>`に排他的に生成する。Sceneにはその一時パスを保存しない。作業ファイルはScene保存、Autosave、終了前に同期し、Play開始前には同期しない。外部編集と未保存のCode Editorバッファが競合するときはどちらも上書きせず、保存と終了を止めて競合を報告する。
- 同期または正式Scene保存に失敗した場合、作業ファイルを保持し、Save/終了を中止する。現在のセッションで作成したファイルだけを、正常終了時かつScene保存成功後に削除する。前回の異常終了で残ったファイルは削除・上書きしない。
- 新規スクリプト作成では拡張子（`.luau`/`.luar`）を選べる。既存ファイルは拡張子から言語を決める。
- パッケージ時、`.luar`は`LuarCompiler`でLuau化してから`.luauc`に出力する（パッケージ済みゲームにLuar DLLは不要）。
- `luar_compiler.dll`は`python build.py`のビルド/実行時に`Luar Programming Language/luar-rs`を`cargo build --release`し、`dlls/luar_compiler.dll`へ自動反映する。cargoが無い環境はコミット済みDLLを使う。

## 特殊なインスタンス

### Process（C++ DLL）

`Process` Instance は `ContentPath`（C++ ソース）、`DllPath`（DLL）、`Enabled` を保持し、Play セッションの `ProcessRuntimeManager` が DLL のビルド・ロード・実行・停止を管理する。詳細な ABI と未実装範囲は `doc/Instances/Process.md` を参照する。

- `System.EnableExecutingProcess` が `true` の場合のみ実行する。配布ゲームではネイティブコード実行の初回許可を要求する。
- `System.UseNetwork` が `true` の場合は Process を実行しない。Process はローカル実行専用である。
- Studio は DLL の欠落または SDK ソース／ヘッダー更新を検出した場合、設定で選択した `cl.exe`、`clang-cl.exe`、`g++.exe`、`gcc.exe` のいずれかで DLL をビルドし、成功した場合だけ Play を開始する。コンパイラの選択は Studio Settings から変更できる。失敗時は古い DLL を実行せず、手動ビルド後の再試行を案内する。Play 開始前に Scene が dirty なら保存確認を行う。配布ゲームは同梱 DLL を検証して使い、ユーザー環境でのビルドは行わない。
- DLL の必須入口は `extern "C" void start()`。`stop()` と `bindHost()` は任意。継続処理は `task::wait()` でエンジンへ制御を返し、停止要求後は戻り値で通常のループを抜ける。
- `InstanceRef<T>` は Instance 実体を所有しない世代検証付き参照。`Instance::New<T>()` が型付き参照を返し、`Name` や `BaseCube` の値は `.Get()` / `.Set()` 形式で操作する。`FindChild<T>` は指定型へキャストする前提の `std::optional<InstanceRef<T>>`、`FindChildWithClass<T>` はクラス名が厳密一致した場合のみ `std::optional<InstanceRef<T>>` を返す。親は `Parent.Get<T>()` / `Parent.GetAs<T>()` / `Parent.Set(ref)` で扱う。`GetSelfProcess()` は実行中 Process 参照、`GetSystem()` は任意の System 参照を返す。
- `System.Heartbeat.Connect(function)`、`BaseCube.Touched.Connect(function)`、`TouchEnded.Connect(function)` は関数ポインタとラムダを受け取る。接続解除には `Connection::Disconnect()` を呼ぶ。InstanceRef は Scene 切替・削除・DLL 解放後は無効である。
- Play 停止時は停止要求、`stop()`、ホスト参照解除、DLL 解放の順に処理する。待機も API 呼び出しも行わない無限ループは watchdog が検出し、低水準ログ出力後にエンジンを終了する。`System.ProcessWatchdogTimeoutSeconds` は正の有限値を秒単位で指定し、既定値は 2 秒。不正値は警告を出して拒否する。
- SDK の任意プロパティ読み書き、汎用 Workspace 操作、物理・描画 API、ネットワーク API は未実装である。現在の値 API は bool、int、float、string、`Vector3`、`Color4`、`CFrame` と限定された System / Process / BaseCube プロパティである。
  `Weather`はWorkspace直下に置く天候システムで、`CloudColor`（`Color4`）と
  `CloudHeight`（ワールド空間の雲層Y座標）を保持する。雲の水平クアッドはカメラのX/Zへ
  移動して描画範囲を維持するが、Y座標は`CloudHeight`を使い、カメラのYには追従しない。
  雲のアルファは`CloudDensity`と`CloudColor.a`を乗算して決める。
  `EnableIOAPI`が有効な場合のみLuauへ`IO.ReadText`/`ReadBytes`/`WriteText`/`WriteBytes`/`AppendText`/
  `AppendBytes`/`Exists`/`IsFile`/`IsDirectory`/`List`/`CreateDirectory`/`Copy`/`Move`/`Remove`/`RemoveTree`
  を公開する。読取りは値または状態、変更系の成功は`true`、権限不足・不正パス・I/O失敗はエラーとする。
  相対パスは起動時のポータブルroot直下、External許可時のみ絶対パスを許可し、`..`・symlink脱出を拒否する。RemoveTreeは
  ポータブルroot、ホーム、ドライブ／FS rootを保護する。TextFile.Contentを含むデータサイズ上限は128 MiB。
  `EnableIPCAPI`では`Connect`/`Send`/`Receive`/`Close`のstubを公開するが未実装エラーを返す。拡張同意receiptは
  構成versionとIO/IPC/External権限集合を保存・比較し、Editorと`--editor-test`では警告とreceiptをバイパスする。

## Sun / Moon / Lighting
  `Sun`は平行光源の向きの**唯一の正**で、`Moon`と共に`CelestialBody`（BaseCubeでもSpatialでもない軽量
  Instance）を継承する。新規Workspaceには既定で`Sun`を1つ挿入する。
  - `Sun.Angle`は`Vector2(方角, 高度)`（度）。方角は北0°・東90°の時計回り（「方角」節: +Xが北、+Zが東）、
    高度は水平0°・天頂90°・負で地平線の下。光の進行方向は太陽方向の逆。非有限値は拒否する。
  - Workspaceの最初の`Sun`だけが光の向きと`Moon`の位置を決める。`Moon`は最初のSunの反対側に見える。
    **Sunが存在しないときは平行光源なし**（環境光とPoint/Spotのみ、影なし）。
  - `Lighting`は太陽光の`Brightness`/`Color`と影の設定だけを持ち、`Direction`は廃止された。
  - `Sun`/`Moon`の見かけの大きさは`Distance`（見かけの距離、既定1000）で決める。円盤は常にカメラから一定距離に
    描き、直径を`基準直径(Sun 200 / Moon 150) × 1000 / Distance`にする。`Size`は廃止された。
  - `CelestialBody`は逆光（グロー`GlowIntensity`/`GlowRadius`、ベール`VeilIntensity`/`VeilFalloff`）と光条
    （回折スパイク`SpikeIntensity`/`SpikeCount`/`SpikeLength`/`SpikeRotation`）を持つ。強さが0の効果は描かず、
    既定はすべて無効。効果は`sky_flare`シェーダーで不透明物の描画後に重ね、ビューポートの深度をコピーしたテクスチャ
    で遮蔽を判定する。光条は手前の物体に遮られる。グローは物体の上へも少しにじむが（縁のにじみ）、その強さは
    天体が見えている割合に比例し、天体が完全に隠れれば物体の上には出ない（物体を貫通しない）。ベールは天体が
    隠れているほど弱い。`HorizonFade`（既定true）が真なら天体が地平線の下に沈むにつれて効果を消す。
  - `Moon`は満ち欠け`Phase`（周期上の位置。0=新月、0.25=上弦、0.5=満月(既定)、0.75=下弦、1=新月。範囲外は周期として
    折り返す）、`PhaseRotation`（明暗境界の向き）、`Earthshine`（影の部分の見え方）を持つ。満月以外は発光球の代わりに
    明暗境界のある円盤（影は`Earthshine`だけ透けて空が見える）を`sky_flare`シェーダーで描く。月は常に太陽の反対側
    なので、満ち欠けは位置から自動では決まらず`Phase`で指定する。
  - 旧形式（BaseCube派生のSun/Moon、`Lighting.Direction`）はロード時に自動移行する。旧BaseCube系プロパティは
    無視、スカラー`Angle`はVector2へ、旧`Size`は見かけの大きさを保つ`Distance`へ換算する。`Sun`を持たず
    `Lighting.Direction`を持つ旧シーンには、その向きを引き継ぐ`Sun`を自動生成する。`Sun`の無い新形式の
    シーンでは生成しない（意図的な平行光源なし）。

## Material / MaterialService
  `MaterialService`はSystem直下に自動生成されるサービスで、配下に`Material`を置く。`BaseCube`は
  `Material`プロパティ（`MaterialService\<名前>`形式のパス文字列）でMaterialを参照する。
  - Materialは`Metallic`/`Roughness`/`Reflectance`（PBR）、`DynamicFriction`/`Restitution`/`MassDensity`
    （物理）、`Conductive`（雷の標的）を持つ。`StaticFriction`はBox3Dが使わないためエディタ/Luaから隠蔽し、
    YAMLにのみ保存する。
  - 参照中のBaseCubeは、物理値とConductiveがMaterialの値になる。参照が無い（または未解決の）BaseCubeは
    従来の`MaterialType`/摩擦/`MassDensity`を使い、見た目も従来のLambert描画のまま。
  - Materialの子の`Decal`/`Texture`は、そのMaterialを参照する全BaseCubeの対応する面（BaseCubeのローカル面。
    Front = -Z）へ投影される。BaseCube直下のDecal/Texture/SurfaceGui/Canvasが面を占有している場合は直下を優先し、
    空いた面だけをMaterialの子で補う。MeshCubeのUV空間Decalは対象外。
  - 参照はロード時に全ツリーが揃ってから解決する。解決できないパスは保持して警告し、クローンは解決済みの参照を
    引き継ぐ。Materialの改名・移動後も、保存されるパスは現在のツリー位置から再計算される。
  - PBR描画はMaterialを参照するBaseCubeだけで、Cook-Torrance GGX（metallic-roughness）に、Skyboxの6面から焼いた
    環境キューブマップの鏡面反射を加える。Skyboxが無い／テクスチャ未読込のときは手続き的な空を使う。
    パイプライン全体が非リニアのため、PBRも同じ表示空間で計算し、拡散アンビエントは従来の0.3固定とする。
  - Materialは画像マップ（`BaseColorMap`/`RoughnessMap`/`MetallicMap`/`NormalMap`）を持てる。値は`FileRef`
    インスタンスへの参照（Materialの子の名前、またはSystemからのパス）で、Packagerは`FileRef`の`ContentPath`として
    追跡する。貼り方はワールド基準のtriplanarで、`TextureScale`（1タイルあたりのstud数）ごとに繰り返す。
    BaseColorは拡散色に乗算、Roughness/Metallicは各プロパティ値に乗算、Normalはタンジェント空間（OpenGL規約）で
    `NormalStrength`で強さを変える。マップを持つMaterialの参照元はインスタンス描画ではなく個別描画になる。
  - ExplorerでMaterialServiceを右クリック →「マテリアルプリセットを追加」で、ざらついたプラスチック／木の板／
    傷のある金属（画像と値を割り当て済みのMaterial）を追加できる。画像は`assets/materials/`にあり、
    `tools/generate_material_textures.py`で再生成できる。

## システム拡張API
- **System**: シングルトン。常に1つのみ存在。Insert Objectリストには登録しない。
  `EnableIOAPI`、`EnableIPCAPI`、`EnableExternalFileAccess`の
  システム拡張フラグを保持する。これらはエディターでのみ変更でき、Luauからは読み取り専用である。
- **TextFile**: `PhysicalFileInstance`を継承する永続テキスト資産。`ContentPath`は配布時の
  初期seed、`StorageId`はユーザー領域のmutable copyを識別するUUIDである。`Content`は全文を
  読み書きでき、I/O API権限を要求しない。Luauの`Instance.new`では生成できず、エディターの
  Insert ObjectまたはSceneロードからのみ生成される。複製時は新しい`StorageId`を割り当てる。
- **Program**: `PhysicalFileInstance`を継承し、`ContentPath`のexeを所有する。子プロセスと標準入出力の
  パイプで文字列を交換する（1行=1メッセージ、改行はエスケープ）。`Start`/`Call`/`Send`/`Receive`/
  `Connect`/`Disconnect`/`Close`を持ち、`EnableIPCAPI`が必要。`Call`/`Close`はコルーチンをyieldし、
  `Close`は終了コードを返す。Luauの`Instance.new`では生成できない。Play停止・シーン切替・破棄時に
  プロセスは強制終了される。現状はWindowsのみ対応（[doc](doc/Instances/Program.md)）。
- **Workspace**: 複数インスタンスを持つ。切り替え可能。
- **StarterCharacter**: System直下に置く、キャラクターのテンプレートを保持するだけのコンテナ。
  中にHumanoid・Root(Cube)・その他のCube/Sphereを通常のInsert Object操作で組み立てる。
  Play開始時、この子要素が新規ModelにcloneされてWorkspaceに追加される。Offlineでは
  `"PlayerCharacter"`、ネットワークHost/ClientではHost割り当てPeerIdに基づく
  `"PlayerCharacter_<PeerId>"`となる。StarterCharacterが存在しない場合は、既定のリグ(旧来
  ハードコードされていたもの)を持つStarterCharacterが自動的にSystem直下に生成される。
- **System.AutoSpawnPlayerCharacter**: ゲーム開始時にローカルPlayerCharacterを自動生成するかを指定する。
- **System.NetworkInterpolationDelay**: ネットワークClientで、他プレイヤーとHostから同期される物体を
  何秒前の状態として補間表示するか（0〜1秒、既定0.1秒）。Hostの物理tick付きの姿勢を前後で補間する。
  既定値は`true`。`false`の場合はローカルPlayerCharacterを生成せず、既定カメラモードの設定値に
  かかわらずUserの起動カメラを`Free`にする。ネットワーク接続時のリモートアバター生成はこの設定の対象外とする。
- **SpawnLocation**: `Cube`派生の出現地点。既定値は`Name=SpawnLocation`、`Size=[8,1,8]`、
  白、`Anchored=true`、`CanCollide=true`、`Enabled=true`とし、通常のCubeとして描画・衝突する。
  active Workspaceの全子孫にある`Enabled=true`のSpawnLocationをfull path昇順で選ぶ。
  ローカルpeer 0とpeer 1は先頭、peer 2以降は`(PeerId-1) % 件数`を用いる。
  CharacterのRootはSpawnLocationのfull CFrameを引き継ぎ、SpawnLocation中心から
  `Spawn.Size.y/2 + Humanoid.HipHeight`だけ上げる。Model全体は元のRoot local CFrameの逆変換を
  合成して配置し、各パーツの相対姿勢を維持する。候補が無い場合はRootを`(0,100,0)`へ置き、
  authored Root rotationは維持する。
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
  LuauからWorldPositionへVector3を代入した場合もワールド座標として解釈し、座標親のローカル座標へ変換して反映する。

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

## BaseCube接触イベント
  現行物理バックエンドはBox3Dとする。`BaseCube.Touched`は物理的な反発ではなく、形状overlapの開始で発火し、`TouchEnded`はoverlap終了で発火する。
  `CanCollide=false`でも形状overlapを検出し、`CanCollide`は物理応答だけを制御する。
  ペアの両方が`CanTouch=true`の場合だけ接触イベントを発火する。`CanTouch=false`は物理応答、NoCollision、Character自己衝突の規則を変更しない。
  Box3DではCanTouch専用sensor categoryと物理contact categoryを分離する。通常shapeはsensorのvisitorとしてだけ
  Touch判定へ参加できるが、通常shape同士のcontact maskはCanCollideだけで決まる。したがってCanTouchの切替は
  bodyの速度、質量、接触反力、constraintへ影響しない。
  Touch用sensor shapeは`Touched`または`TouchEnded`に1件以上のLuau listenerがあるBaseCubeだけが保持する。
  最初のlistener接続時に生成し、両signalの最後のlistener切断時に破棄する。相手側のBaseCubeはlistenerを
  持たなくてもvisitorとして検出され、ペア両側にlistenerがある場合も通知はoverlapごとに1回へ集約する。

## Spring
  Springは2つのBaseCubeを結ぶ双方向ばねであり、`FreeLength`（自然長, stud）へ向けて引きも押しも行う。
  `FreeLength`が0なら生成時のアンカー間距離を自然長に採用し、以後のプロパティ更新でも再計算しない。
  `Stiffness`/`Damping`はRopeと同じk,c換算（hertz/dampingRatio）で、長さ制限は持たない。
  アンカーはAttachmentがあればその位置、無ければ各Cubeの中心。`Visible`がtrueの間だけ
  `Radius`/`Coils`/`Thickness`/`Color`でコイル状に描画し、`Visible`は物理に影響しない。

## PrismaticConstraint
  PrismaticConstraintは2つのBaseCubeの相対移動を`Axis`（Cube0ローカル、Attachment0があればその軸系）方向のみに
  制限する。`LimitsEnabled`時は`LowerLimit`〜`UpperLimit`（stud）をCube0アンカーからCube1アンカーまでの軸方向
  距離の範囲として使う。モーターとばねは持たない。アンカーはAttachmentがあればその位置、無ければ各Cubeの中心。
  描画は物理デバッグ表示ON時のみ（アンカー間の線、軸の矢印、可動範囲）。

## BallSocket
  BallSocketは共有アンカーを維持する球面jointであり、`AngularXMode`、`AngularYMode`、
  `AngularZMode`をそれぞれ`Free`、`Limited`、`Locked`へ設定できる。既定値は後方互換のため
  3軸とも`Free`とする。

  制限は公式Box3Dの球面jointが持つ「円錐制限1つ＋ツイスト制限1つ」へ次のように写像する
  （Box3D本体は改造せず、公式APIのみを使う。軸別の独立制限は表現できない近似である）。
  - **円錐制限（X・Y）**: X/Yのどちらかが`Free`以外なら有効。各軸の角度は`Locked`=0度、
    `Free`=180度、`Limited`=`min(|Min|, |Max|)`（度）とし、X/Yのうち小さい方を円錐の半角とする。
    Min/Maxが非対称でも絶対値の小さい方で対称に扱う。
  - **ツイスト制限（Z）**: Zが`Free`以外なら有効。`AngularZMin`〜`AngularZMax`（度）をそのまま
    下限/上限として使う。`Locked`でも固定角0度ではなくMin/Maxの範囲が使われるため、
    0度に固定したい場合はMin=Max=0とする。
  - 3軸`Free`のBallSocketは従来どおり無制限球面jointとして動作する。

  制限の基準はworld axisではなく、BallSocket生成時の`localFrameA/B`とする。AttachmentがあればAttachmentの
  local frameを使い、R6 ragdollのAttachmentが無いBallSocketは対応Motor6DのC0/C1 bind frameを使う。

  BallSocketは接続body同士のcollisionを変更しない。collision無効化はNoCollisionだけが担当し、
  RagdollのNeck/Shoulder/Hipにはそれぞれlocal joint frame基準の有限な制限を設定する。

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

  既定RootとTorsoのSizeは`(2,2,1)`、中心は従来の`basePos`を維持する。Meshを使用するカスタムTorsoの
  保存SizeはMesh形状に応じて異なり得るが、既定Cube Torsoの物理寸法へ流用しない。
  接地時はRoot中心から下向きに一度floorをsampleし、保存されたHumanoid.HipHeight（既定3 stud）のdistanceを
  GroundHeight controllerで保つ。HipHeightは常に通常の保存プロパティであり、ground sampleから自動算出・上書きしない。
  controllerは支持点における床との相対速度で減衰を計算し、各dynamic R6 bodyへ質量比例の支持力を与える。接地状態は
  HipHeightを接地状態の捕捉ゲートとして使い、捕捉後は微小なfloor distance揺れでは接地状態を反転させず、
  床が消えるかRootが上昇したときに解除する。HipHeightが高い場合も、その目標距離までraycast範囲を拡張する。
  重力相殺は現在のWorkspace.Gravityから算出する。遠距離の床へ吸着せず、jump上昇中は停止し、
  下降してlanding captureへ入った時だけ再開する。SpawnLocation上では保存されたHipHeightをRoot中心から
  地面までの距離として使う。SpawnLocationがある場合はSpawnLocation上面へHipHeightを加えた位置へ配置する。
  SpawnLocationがない場合もfallback位置へ配置し、同じHipHeightをhover目標として使う。
  GroundHeight、接地、Truss中の重力設定は操作入力とは独立した物理更新として毎フレーム評価する。
  接地中の歩行速度は足元の床に対する速度とする。支持荷重と摩擦係数で制限した接線力で歩行・停止し、
  動的な支持剛体には支持力と接線力の反力を支持点に適用する。床との相対速度には床の並進と回転を含める。
  ジャンプと床の端からの離床では水平のワールド速度を引き継ぐ。地上ジャンプのJumpPowerは支持点に対する上向きの初速とする。
  空中入力の加速には上限を設ける。Anchoredの支持剛体を連続移動させるスクリプトは`BaseCube:SetKinematicTarget`を使う。
  Truss接触時はCharacter Modelに属する全dynamic R6 bodyの重力を無効化する。W入力中は`ClimbingUp`、S入力中は
  `ClimbingDown`状態へ遷移し、各bodyの`CharacterClimbForce`へ水平方向のストレイフ速度と`ClimbSpeed`の昇降速度を
  MaintainVelocityとして設定する。垂直入力が無い場合もTruss上で速度0を維持し、Trussから離れたらForceを無効化して
  全bodyの重力を復帰する。昇降状態はRagdoll状態として扱わない。`ClimbingUp`/`ClimbingDown`中にJumpした場合は
  `Normal`へ戻し、Truss用Forceを無効化して全bodyへ通常のJumpPowerを適用する。Jump直後にTrussへ重なっていても、
  Truss制御で重力を再び無効化しない。
  `ClimbingDown`中はRoot footprintの下方向box castでCharacter自身を除外し、normal.yが0.5以上の最も高いsupportを確認する。
  Rootからsupportまでの距離が`HipHeight + 0.2 stud`以下になった場合は、地面へめり込む前に`Normal`へ自動遷移する。
  この遷移では各bodyに残った負のY速度だけを0へ戻し、上向き速度・Position・CFrameは変更しない。
  Trussが床まで続いていても、RootがTrussのAABBを離れるまでTruss制御を再開しない。
  したがってFree/Program中もCharacterHoverForce、重力、衝突、LiquidCubeの液体浮力は維持される。

  Humanoidは`Normal`/`ClimbingUp`/`ClimbingDown`/`Ragdoll`/`Recovering`の状態を持つ。Box3Dのcontact hit eventから接触点のnormal impulseを優先して
  impactを求め、利用できない場合は接触法線方向の相対接近速度を使う。Character bodyごとの同一physics tickの
  最大impactが`Humanoid.ImpactRagdollThreshold`（既定45 stud/s相当）以上になった場合、movement、jump、hover、
  full-body yaw、RootのAngularX/AngularZ lock、Motor6D姿勢制御を停止し、既存R6 Motor6DのC0/C1 bind anchorを
  使うBallSocketを有効化する。Ragdoll中だけbody collisionを有効化する。通常Characterの内部self-collisionは
  Character collision groupで抑制するが、Ragdoll中にBallSocketで管理されるbody同士は異なるBallSocket chain間でも
  collisionを許可する。明示的なNoCollisionはこの許可より優先し、外部worldとの衝突を許可する。
  空中状態から動的なhover captureへ入る時は、全character bodyの下向き成分だけを質量加重した垂直運動エネルギーから通常の
  `landingCaptureDistance`内でhoverが吸収できる制動エネルギーを差し引き、その残余を同じstud/s相当値へ換算して
  同じ閾値で判定する。したがって通常JumpPowerの着地は維持しつつ、高速Truss jumpや高所落下のエネルギーは
  hoverによる事前減速で失われる前にRagdoll判定へ渡される。
  再生中のAnimationは再生状態と時刻を保持したままRagdoll/Recovering中だけ評価を一時停止し、Normal復帰後に
  同じAnimationを再開する。死亡時も同じ遷移を使う。

  死亡していないRagdollは、Rootの線速度が`RagdollRecoverySpeed`（既定2.5 stud/s）以下、角速度が2.0 rad/s以下で、低速状態が
  `RagdollRecoveryDelay`（既定1秒）続いた場合に`Recovering`へ遷移する。通常はR6 bodyのいずれかの接地も確認し、support scanが一時的に
  失敗している場合は追加0.75秒の猶予後に接地なしでも遷移する。Recoveringでは
  BallSocketを先に無効化してMotor6Dをbind poseへ戻し、Root lockとhoverを無効にしたままRootGyroのX/Y/Zで物理的にuprightへ戻す。
  upright error、Pitch/Roll errorが15度以下、Root角速度が1.5 rad/s以下の状態を0.1秒維持した後、現在位置と有効Yawを維持してPitch/Rollだけを除去する
  CFrameを一度だけ適用する（`gyro-success`）。その直後にRoot角速度、Root lock、通常collision、Gyro/yaw、hover、movement/jumpを順に復元して`Normal`へ戻す。
  Gyroがuprightへ到達できない場合でも、Recovering開始から0.5秒以上経過し、Root線速度が4.0以下、角速度が2.5 rad/s以下なら
  supportの有無に関係なく`speed-fallback`として同じCFrame正規化を一度だけ適用する。さらに1.25秒経過後は、速度・supportの有無に関係なく
  `timeout-fallback`で最終化する。supportが得られた場合だけYを必要最小限補正し、得られない場合は現在Root Yを維持する。
  Recoveringの接地は通常時のground detectionとは別のsupport scanで判定する。Rootの現在YawでRootのX/Z footprintを薄いboxとして、
  Rootの想定足元より少し上から`HipHeight + 1 stud`以上の下方へshape castし、Character階層を除外する。接触法線のY成分が0.5未満の壁面は除外し、
  複数候補では最も高い上向き面をsupportとして採用する。scan結果は通常のY補正に使うが、supportの一時的な欠落で復帰処理を永久待機させない。
  最終CFrame正規化のYは、現在Root Yを下限として`max(currentRootY, supportY + HipHeight)`で求める。X/ZとYawは維持し、
  必要な最小上方向補正だけを加えるため、Rootを空中へ下げたり大きく持ち上げたりしない。
  Recovering中もNetworkのAvatarBatchではRagdoll bitを維持し、Host権威の復帰完了までremote poseで物理姿勢を上書きしない。

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
      Userの起動カメラモードへ適用する。System.AutoSpawnPlayerCharacterがfalseの場合はFreeを優先する。
      旧シーンや未知の値はCharacterへフォールバックする。
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
  Play Hereの初回だけは明示されたHumanoid Rootのworld positionをSpawnLocationより優先し、respawnは通常選択へ戻る。


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
- 画面GUI（ScreenGuiObject）はエディター用の `Locked` を持つ（既定false、trueのときだけYAMLへ出力）。
  Locked の要素はビューポートでクリックを素通りし、ドラッグ・ツールバーの揃え/全画面化の対象外になる。
  Explorer/プロパティからは選択・編集できる。
- ビューポートでの画面GUIの編集はツールバーのGUIタブ表示中だけ行い、その間は3Dのクリック操作と
  Alt のフリールック（マウスロック）を止める（Alt は吸着の一時オフに使う）。Shift 押下中だけ
  プライマリ選択のGUIにリサイズハンドルを出す。
- System.AllowControlModeSwitch / AllowWorkspaceSwitch（ランタイム用、既定false）: RecubinEngine で L キーの
  Free/Character 切り替え、User:RequestWorkspaceSwitch() による Workspace 切り替えを許可するか。既定ではどちらも禁止。
- System.LockAspectRatio（ランタイム用、既定false）が true なら、RecubinEngine はウィンドウの縦横比に関わらず
  BaseResolution の縦横比を保ち、余白を黒帯にする（3D・ゲームGUI・チャット・マウスのレイとも内側の矩形を使う）。
  false なら従来どおりウィンドウ全体に描き、Pixel単位の画面GUIは縦横別の倍率で拡大する。
- System.UseFixedResolution（エディター用、既定true）が false なら、エディターのビューポートは黒帯なしで
  パネルの実サイズを描画解像度にし、Pixel単位の画面GUIは縦横同率で拡大縮小する。ランタイムは変わらない。
  GUIタブ以外では画面GUIはクリックを素通りする（全画面GUIがあっても3Dを編集できるようにするため）。
- TextContent は TextXAlignment（Left/Center/Right、既定Left）・TextYAlignment（Top/Center/Bottom、
  既定Center）・TextScaled（既定false）も持つ。既定値は従来の描画（左寄せ＋縦中央）と同じで、
  既定値のときはYAMLへ出力しない。Luauでは文字列（`label.TextXAlignment = "Center"`）で読み書きする。
  TextScaled は FontSize を無視して矩形いっぱいへ拡大・縮小する（BillboardGui子の縮小専用fitとは別）。
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
- `StringValue` は `ValueBase` を継承し、空文字列を既定値とする `std::string Value` を保持する。
  YAML と Luau の `Value` で文字列を読み書きでき、変更時の `Changed` は新しい文字列を1引数で渡す。
  文字列の長さは保持され、埋め込みNULも切り捨てない。埋め込みNULを含む値はエディターで編集を無効化する。
- Explorer の各親直下の要素は、`Workspace`、`Folder`、`Model`、`Script`、`LocalScript`、`ModuleScript`、
  物理ファイル (`PhysicalFileInstance`)、`ValueBase`、`BaseCube`、その他の順で表示する。優先順位が
  同じ要素は具体的なクラスごとにまとめ、名前を ASCII 順で並べる。名前中の連続した数字は数値として
  比較するため、`Cube1`、`Cube2`、`Cube10` の順になる。優先順位未定義のクラスは最後尾に置く。
  名前変更の確定後は表示順を再計算する。並び替えは表示用配列だけを変更し、Instance の親子関係、
  選択状態、展開状態を変更しない。

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
`state`コマンドはImGuiの状態（開いているポップアップ・フォーカス・見えているtarget）を1行のJSONで
出力し、`wait`のタイムアウトと`target unavailable`にも同じ状態が付く。`--ui-automation-hidden`を
併用するとウィンドウを表示せずに起動する（OSの入力やデスクトップのスクリーンショットは使わない）。
`profile`はFrameProfilerの全区間・GPU時間・カウンターを1行のJSONで、`script_profile on|off|reset|dump`は
スクリプトAPI呼び出し(`ApiProfiler`)の回数・時間(self/total)を出し、`wait_frames <n>`でnフレーム待てる。

## Editor パネル表示とテーマ

- パネルの開閉状態は`editor_settings.yaml`の`Panels`へ終了時に保存し、起動時に復元する。
  対象はExplorer / Properties / Viewport / ContentBrowser / Console / Animation / Profiler。
  記録が無い場合の既定は、Animation・Profilerが非表示、他は表示。
- ようこそタブは保存対象外で、起動時に毎回必ず表示する。クラッシュ復旧候補があり復旧ダイアログを
  出す場合も表示する。シーンを読み込むと閉じる。
- エディターのテーマは`Preferences.Theme`(`Classic` / `Frutiger`)に保存し、設定メニューから
  即時切替する。未指定はClassic。Classicは紺色、Frutigerは淡い空色のガラス調（アクティブタブ・
  タイトルバーは青、非アクティブは灰色）。ボタンの光沢は`EditorUi::GlassPalette`で切り替える。

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

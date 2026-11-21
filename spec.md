# Recubin 仕様書

## 単位・座標・Instance

- 距離の単位は Roblox erik stud(以降、stud)。20 stud = 1 m。標準重力加速度 9.81 m/s² は 196.2 stud/s² に相当する。

- +X が北、−X が南、+Z が東、−Z が西。

- Model と BaseCube のローカル座標は、祖先で最も近い Spatial を基準とする。途中に Folder などがあっても基準は変わらない。座標親がなければワールド座標を基準とする。

- 姿勢は CFrame で表し、Position はローカル位置、WorldPosition はワールド位置を表す。Luau から WorldPosition を設定すると、指定したワールド位置に移動する。

- 親を削除すると子孫も削除される。エディターの Insert Object には生成可能なクラスを表示する。プロパティは公開範囲と読み書き権限に従ってエディターと Luau から利用できる。

- Properties は複数選択時に選択対象のプロパティの和集合を表示する。Restart などの操作は対応するプロパティ欄から実行できる。

- StringValue の Value は既定で空文字列。YAML と Luau で読み書きでき、Changed は変更後の文字列を渡す。埋め込み NUL は保持され、その値のエディター上での編集は無効になる。

## シーンとアニメーションの保存形式

- シーン YAML は recubin.type が scene、recubin.version が 0。ヘッダーのないシーンは version 0 として読み込む。異なる type または未対応の version は拒否する。
- アニメーション資産はツリー上の Animation で参照する。.rcanim は YAML で、recubin.type は animation、recubin.version は 1。rig: R6 と space: joint_delta の組み合わせでは、キーは関節の基準姿勢からのローカル差分を表す。旧シーンに埋め込まれた Animation も読み込める。
- Humanoid の WalkAnimation、JumpAnimation、EquipAnimation には、対応する Animation を指定する。有効な指定を優先し、キャラクターの複製後は複製先に対応する Animation を参照する。
- アニメーションが未指定、参照切れ、破損、形式不一致、未対応の版または不正データの場合は、その実行中だけ既定の R6 アニメーションを使う。元の資産、参照、シーンツリーを保持してシーンを未保存扱いにせず、ロード状態と代替再生は Properties と警告で確認できる。
- 旧シーンで migrations.character_animation_bindings.version: 1 が未記録の場合は、空欄の Humanoid 参照だけに既定の Animation と参照を補完し、移行済みとして記録する。以降はユーザーの指定や空欄を保持する。Restore Default Animations を実行すると既定の参照を再設定できる。

## アセット保存形式（.rcaet v1）

Explorer の「アセットを書き出し...」で選択した部分木をアセットに保存できる。Explorer の「アセットを読み込み...」ではクリックした Instance の子、Content Browser の「アセットとして読み込み」では選択中の Instance の子に追加する。Content Browser で選択が空なら Workspace に追加する。読み込みは一度の Undo で取り消せる。展開済みファイルは残る。

- 先頭行は `#rcaet 1 yaml=<N>`。続く N バイトは YAML。埋め込みファイルの生バイトは YAML の直後に並ぶ。構造のみのファイルは全体が YAML として読める。
- YAML は `recubin: {type: asset, version: 1}`、`Asset: {Name, Roots, Materials}`、`Embedded` を持つ。Roots と Materials のノードはシーンと同じ ClassName、Name、Properties、Children で表す。type または version が異なる場合は拒否する。
- Embedded の各ファイルは Source（依存パスの元の文字列）、File（バンドル内の相対パス）、Size（バイト数）、Hash（`sha256:<hex>`）、Offset（YAML 直後からのバイト位置）を持つ。Terrain.DataPath のようなディレクトリ依存は Dir: true と Files の配列で表し、.autosave は含めない。
- 書き出しは元の部分木を保持する。範囲外を参照するプロパティは空欄にして警告する。BaseCube が参照する Material は画像マップの FileRef とともに同梱する。System、Workspace、各 Service、Users、User、Lighting、UserInput、Event、実行中のキャラクターは書き出せない。
- 書き出し時に構造のみか依存ファイルの埋め込みかを選び、埋め込むファイルを個別に指定できる。実行形式は既定で除外し、警告に同意した場合に限り埋め込む。
- 読み込み時は埋め込みを `assets/imported/<アセット名>/<File>` に展開し、依存パスを更新する。既存ファイルと同内容なら再利用し、異なる場合は `_1`、`_2` のように連番を付ける。サイズ、範囲、ハッシュが不正な場合や、File に親ディレクトリ参照、絶対パス、ドライブ文字がある場合は、変更を加えず拒否する。
- 実行形式があるときは展開前に確認する。既定の「除外して読み込む」では該当する Program の ContentPath を空にする。「展開して読み込む」を選んだ場合も自動実行しない。
- 同名の Material があれば再利用し、なければ追加する。TextFile は取り込みごとに新しい StorageId を持つ。ルート名が衝突した場合は連番を付ける。

## スクリプトとシステム拡張

- Script、LocalScript、ModuleScript の ScriptExtension（Luau / Luar）が実行言語を決める。Path を .luar に設定すると Luar、.luau または .lua に設定すると Luau に切り替わる。.luauc への変更では現在値を保持する。新規作成時は .luau または .luar を選択できる。
- YAML ではパスを ContentPath、言語を ScriptExtension として保存する。明示された ScriptExtension を優先し、項目がない旧シーンではパスから言語を決める。パッケージ済みゲームでは Luar スクリプトも .luauc として実行できる。
- System は一つだけ存在し、Insert Object には表示しない。EnableIOAPI、EnableIPCAPI、EnableExternalFileAccess はエディターから設定でき、Luau では読み取り専用。
- EnableIOAPI を有効にすると、IO に ReadText、ReadBytes、WriteText、WriteBytes、AppendText、AppendBytes、Exists、IsFile、IsDirectory、List、CreateDirectory、Copy、Move、Remove、RemoveTree が公開される。変更処理の成功は true。権限不足、不正パス、I/O 失敗はエラーになる。
- IO の相対パスは起動時のポータブルルートを基準とする。絶対パスには EnableExternalFileAccess が必要。親ディレクトリ参照やシンボリックリンクによる許可範囲外へのアクセスは拒否する。RemoveTree はポータブルルート、ホーム、ドライブまたはファイルシステムのルートを保護する。TextFile.Content を含むデータの上限は 128 MiB。
- EnableIPCAPI を有効にすると IPC.Connect、Send、Receive、Close が公開され、呼び出すと未実装エラーを返す。実行時の拡張 API の同意は設定の版と IO・IPC・外部アクセスの許可内容に対して保持される。エディターと --editor-test では確認を省略する。
- TextFile と Program はエディターまたはシーン読み込みから生成できる。Luau の Instance.new からの生成は拒否する。
- TextFile の ContentPath は配布時の初期内容を示す。Content は IO 権限なしで読み書きでき、StorageId ごとにユーザー領域に保存される。複製には新しい StorageId が割り当てられる。
- Windows の Program は ContentPath の実行ファイルと標準入出力で UTF-8 の文字列を交換する。EnableIPCAPI が必要。1 行が 1 メッセージで、改行などはエスケープする。Call と Close はコルーチンを一時停止し、Close は終了コードを返す。Play 停止、シーン切り替え、終了、Program の破棄時はプロセスを終了する。[公開メソッドと通信仕様](doc/Instances/Program.md)

## ライティング・天候・マテリアル

- Workspace 直下の Weather は CloudColor とワールド座標での雲の高さ CloudHeight を持つ。カメラが上下しても雲の高さは一定。雲のアルファは CloudDensity × CloudColor.a。
- 新規 Workspace には Sun が一つ入る。最初の Sun の Angle は (方角, 高度) を度で指定する。方角は北 0°、東 90°の時計回り。高度は水平 0°、天頂 90°、負値は地平線の下。非有限値は拒否する。
- 太陽光は最初の Sun の方向から差し、Moon はその反対側に見える。Sun がない Workspace には平行光とその影がなく、環境光と PointLight・SpotLight が有効。Lighting の Brightness、Color、影の設定は太陽光に適用される。
- Sun と Moon の見かけの直径は基準直径（Sun 200、Moon 150）× 1000 / Distance に比例する。Distance の既定値は 1000。
- Sun と Moon はグロー（GlowIntensity、GlowRadius）、ベール（VeilIntensity、VeilFalloff）、光条（SpikeIntensity、SpikeCount、SpikeLength、SpikeRotation）を持つ。強度の既定値は 0。手前の物体が光条を遮り、天体が隠れるほどグローとベールが弱まる。完全に隠れた天体のグローは遮蔽物に重ならない。HorizonFade（既定 true）が有効なら地平線の下で効果が減衰する。
- Moon.Phase は満ち欠けの周期を指定する。0 と 1 は新月、0.25 は上弦、0.5 は満月（既定）、0.75 は下弦。範囲外は周期として折り返す。PhaseRotation は明暗境界の向き、Earthshine は暗い部分の見え方を指定する。
- 旧シーンの Sun・Moon は旧 BaseCube プロパティを読み飛ばし、旧 Angle と Size は新しい Angle と Distance に変換する。Sun がなく旧 Lighting.Direction がある場合は、その光の向きを引き継ぐ Sun を置く。
- MaterialService は System 直下に自動生成される。BaseCube.Material の保存形式は `MaterialService\<名前>`。未解決パスは保持して警告する。参照先の改名・移動後は現在のパスを保存し、複製では解決済みの参照を引き継ぐ。
- Material を参照する BaseCube には Material の Metallic、Roughness、Reflectance、DynamicFriction、Restitution、MassDensity、Conductive を適用する。Material を参照する場合は PBR で描画する。有効な参照がなければ BaseCube 側の材質・物理設定を使い、Lambert で描画する。StaticFriction は YAML に保持され、エディターと Luau には公開しない。
- Material の子の Decal と Texture は、参照する BaseCube のローカル面（Front は −Z）に適用する。同じ面に BaseCube 直下の Decal、Texture、SurfaceGui、Canvas があればそちらを優先する。MeshCube の UV 空間 Decal は対象外。
- マテリアルの鏡面反射には Skybox を使い、利用できなければ既定の空を使う。BaseColorMap、RoughnessMap、MetallicMap、NormalMap は FileRef を子の名前または System からのパスで指定する。画像はワールド座標基準の三平面投影で TextureScale stud ごとに繰り返す。色、粗さ、金属度のマップは対応する値に乗算し、接線空間の法線マップは OpenGL 規約で NormalStrength を適用する。参照画像はゲーム配布に含める。
- MaterialService の「マテリアルプリセットを追加」から、ざらついたプラスチック、木の板、傷のある金属を追加できる。

## 物理と拘束

- BaseCube の親子関係は Instance 階層と座標の基準を定める。物理的に追従させるには拘束を使う。
- Weld でつながる BaseCube は一つの剛体アセンブリを構成する。内部同士は衝突せず、外部とは通常どおり衝突する。一つでも Anchored が true ならアセンブリ全体が固定される。ローカル座標の基準は維持される。
- NoCollision は指定した二つの BaseCube 間の物理衝突を無効にする。剛体の結合や追従は生じない。
- Touched と TouchEnded は形状の重なりの開始と終了でそれぞれ発火する。ペアの両方で CanTouch が true の場合に、購読している側へ一度ずつ通知する。CanCollide が false でも重なりを通知する。CanTouch の変更で、物理応答、NoCollision、自己衝突や拘束の規則は変わらない。
- Spring は FreeLength に向けて両方向に力を加える。FreeLength が 0 の場合は生成時のアンカー間距離を自然長とし、その後のプロパティ変更でもその距離を維持する。Stiffness と Damping でばねと減衰を指定する。長さ制限はない。Visible が true の場合のみ、Radius、Coils、Thickness、Color に従って描画する。
- PrismaticConstraint はモーターとばねを持たず、相対移動を Cube0 のローカル Axis に制限する。Attachment0 があればその座標系を使う。LimitsEnabled が true の場合、アンカー間の軸方向距離は LowerLimit から UpperLimit（stud）まで。物理デバッグ表示ではアンカー間、軸、可動範囲を示す。
- Spring と PrismaticConstraint のアンカーは Attachment があればその位置、なければ各 Cube の中心。
- BallSocket は両者のアンカーを同じ位置に保つ。角度は関節のローカル座標を基準とし、Attachment を指定すればその座標系を使う。R6 ラグドールで Attachment がない場合は対応する Motor6D の基準姿勢を使う。BallSocket 自体は衝突設定を変更しない。
- BallSocket の AngularXMode、AngularYMode、AngularZMode は Free、Limited、Locked を取り、既定はすべて Free。X/Y のいずれかを制限すると、小さい方の角度が円錐の半角になる。各軸の角度は Free が 180°、Locked が 0°、Limited が Min と Max の絶対値の小さい方。Z が Limited または Locked の場合、AngularZMin と AngularZMax がねじりの下限・上限になる。0°に固定するには両方を 0 に設定する。
- Gyro は一つの Part のワールド角度を制御する。各軸に Enabled、TargetAngle（度）、MaxTorque、MaxAngularSpeed（度/秒）があり、有効な軸にそれぞれ適用する。

## キャラクター

- StarterCharacter は System 直下のキャラクターテンプレート。存在しなければ既定のリグ入りのテンプレートを作る。生成時は子を複製して Model として Workspace に追加する。名前はオフラインでは PlayerCharacter、ネットワークではホストが割り当てた ID を使って `PlayerCharacter_<PeerId>`。
- System.AutoSpawnPlayerCharacter はローカルキャラクターの自動生成を制御する（既定 true）。false の場合は起動時のカメラが Free になり、リモートアバターの生成は継続する。
- SpawnLocation は通常の Cube として描画・衝突する。既定は名前 SpawnLocation、サイズ (8, 1, 8)、白、Anchored・CanCollide・Enabled が true。対象はアクティブ Workspace の全子孫にある有効な SpawnLocation。フルパス昇順、0 始まりで、peer 0 と 1 は先頭、peer 2 以降は (PeerId − 1) % 件数を使う。
- 出現時の Root は出現地点の向きを引き継ぎ、その中心から出現地点のローカル上方向へ Size.y / 2 + HipHeight の位置に置く。各部位の相対姿勢は保たれる。出現地点がなければ Root は (0, 100, 0) に出現し、テンプレートの向きを保つ。
- ローカルキャラクターの Root は生成時に Anchored = false、CanCollide = true になる。User.CharacterAdded は初回と再出現のたびに発火する。引数はパーツ参照と出現位置が設定済みの Model で、この時点では Workspace への追加前。再出現時は出現地点を再選択する。Play Here の初回だけは指定のワールド位置を優先する。
- Root が移動とジャンプの基準。既定の Root と Torso のサイズは (2, 2, 1)。通常時は移動入力の水平な向きへ直立して回転し、入力がなくなると最後の方位を保つ。身体の各部は外部物体との衝突に参加する。
- HipHeight は保存される Root 中心から床までの目標距離で、既定は 3 stud。接地中はこの高さを保ち、床がなくなるか上昇すると接地を解除する。小さな距離の揺れで接地と離床を繰り返さない。ジャンプ上昇中の支持は停止し、下降して床に近づくと再開する。
- 接地中の移動は床の支持点に対する相対速度で制御する。床の並進・回転を考慮し、可動な床は支持と摩擦の反力を受ける。ジャンプや床端から離れるときは水平のワールド速度を引き継ぐ。地上ジャンプの JumpPower は床の支持点に対する上向き初速。空中での入力による加速には上限がある。Anchored の床を連続移動させるには BaseCube:SetKinematicTarget を使う。
- Truss につかまると重力が停止し、W で ClimbingUp、S で ClimbingDown になる。ClimbSpeed で昇降し、横移動もできる。上下入力がなければその高さを保つ。離脱時は重力が戻る。ジャンプで Normal に戻って離脱でき、直後に Truss に重なっていても再捕捉しない。
- ClimbingDown 中に Root が床面から HipHeight + 0.2 stud 以内に入ると Normal に戻る。床に向かう下向き速度を止め、姿勢と位置を保持する。床まで続く Truss でも Root が Truss の範囲を離れるまでは登攀を再開しない。
- 死亡時と、衝撃または高速着地が ImpactRagdollThreshold（既定 45 stud/s 相当）以上のときは Ragdoll へ移行する。通常の操作、直立、床からの支持を一時停止する。ラグドールの関節には角度制限があり、外部物体と身体同士は衝突する。明示的な NoCollision を優先する。
- 生存中は Root の線速度が RagdollRecoverySpeed（既定 2.5 stud/s）以下、角速度が 2.0 rad/s 以下の状態が RagdollRecoveryDelay（既定 1 秒）続き、接地していると Recovering に移る。接地が確認できなくても低速状態がさらに 0.75 秒続けば復帰を始める。復帰中は直立へ戻り、水平位置と方位を保つ。床を確認できた場合のみ、必要な分だけ上方へ補正する。ホストが復帰を完了するまで、リモート側ではラグドールとして扱う。
- 再生中の Animation は Ragdoll と Recovering の間に一時停止し、Normal に戻ると同じ時刻から再開する。
- Tool の Handle と装備する腕の基準回転は一致する。装備時の腕や、ジャンプから着地までの手足の姿勢は Animation で補間する。

## User・入力・Workspace

- MovementInputEnabled、CameraInputEnabled、HotkeyInputEnabled、ToolInputEnabled はそれぞれ組み込み操作を制御する。いずれも既定は true で保存される。User.Input の直接取得と Luau からの操作は引き続き利用できる。
- CursorType は Default または Type1〜Type10。各スロットは PNG、JPEG、BMP、TGA の ContentPath、0 以上の整数のホットスポット、長辺の論理サイズ Size（1〜512 px、既定 32 px）を保存する。画像は縦横比を保ち、表示倍率に合わせて拡大・縮小する。カスタムカーソルはゲーム領域内だけで適用する。
- F1〜F12 は User.Input へ固定名で通知する。この範囲の組み込み操作は F8 の MouseLock 切り替えのみ。MouseLock はプライマリビューポートの中央を基準とし、フォーカスを失うと解除する。
- CharacterSmoothing は移動方向と向きの補間率（既定 0.15）。1 は即時追従、0 は追従なし。設定値は 0〜1 に収め、非有限値は既定値に戻す。ホストはシーンのローカル User の値をリモート User に適用する。
- エディターの起動時は Free。ゲームでは System.DefaultCameraMode の Free、Character、Program を適用し、未指定・不明な値は Character。AutoSpawnPlayerCharacter が false の場合は Free を優先する。
- Character から Free または Program に切り替えると歩行入力と入力による旋回を解除する。接地中は床に対して減速し、ジャンプ・落下は継続する。全モードでキャラクターの物理は更新する。
- 死亡中もカメラ入力と許可済みの L キー切り替えを受け付ける。Character ではその場でカメラを回せる。Free ではカメラを自由に移動でき、ラグドールの姿勢は物理に従う。
- RecubinEngine の L キーによる Free / Character 切り替えは System.AllowControlModeSwitch、User:RequestWorkspaceSwitch() は System.AllowWorkspaceSwitch で許可する。どちらも既定は false。
- Play 中のアクティブ Workspace はローカルキャラクターの最も近い Workspace 祖先。Character.Parent の変更は同一フレームで反映し、カメラと入力の対象、物理、地形、天候、パーティクル、レプリケーション、Luau の workspace、プライマリビューポート、Explorer が追従する。
- キャラクターが未生成、または Workspace の外にいる間は現在の Workspace を保持する。切り替え時に Free / Program カメラの姿勢と位置は変わらず、セカンダリビューポートは指定の Workspace を表示し続ける。
- クライアントの System.NetworkInterpolationDelay は他プレイヤーとホストから同期する物体の補間表示の遅延（0〜1 秒、既定 0.1 秒）。

## ゲーム GUI とビューポート

- 画面 GUI とワールド GUI は Active、Size、Norm、Visible、BackgroundColor、ZIndex、Transparency を共有する。抽象クラス GuiObject は生成できない。
- BillboardGui の Offset は親オブジェクトのローカル座標。SizeMode が Screen の場合は Size を画面ピクセルで指定し、World の場合はワールド単位で指定する。World では子 GUI にも同じ投影倍率を適用する。
- TextLabel と TextButton は Text、TextColor、TextXAlignment、TextYAlignment、TextScaled を持つ。TextXAlignment は Left / Center / Right（既定 Left）、TextYAlignment は Top / Center / Bottom（既定 Center）。Luau では位置揃えを文字列で読み書きする。配置とスケーリングの既定値は YAML への出力を省略する。TextScaled（既定 false）が true なら FontSize にかかわらず矩形に合わせて拡大・縮小する。
- ImageLabel と ImageButton は Image を持つ。ボタンの操作は Activated で通知する。
- SurfaceGui は親 BaseCube の対象面の縦横比に合わせ、Size で指定したキャンバスの縦横比を保って面内に収める。たとえば Size が (200, 100) で対象面が正方形なら、上下に余白が入る。
- RecubinEngine で System.LockAspectRatio（既定 false）が true の場合は BaseResolution の縦横比を保ち、余白を黒帯にする。3D、ゲーム GUI、チャット、マウスのレイは内側の領域を使う。false ではウィンドウ全体を使い、Pixel 単位の画面 GUI は縦横で別の倍率を使う。
- エディターで System.UseFixedResolution（既定 true）が false の場合は、パネルの実サイズで黒帯なしに描画する。Pixel 単位の画面 GUI は縦横同率で拡大・縮小する。

## エディター

- 画面 GUI のビューポート編集は GUI タブを表示している間だけ有効。ほかのタブではクリックが 3D ビューへ通る。GUI 編集中は 3D のクリック操作と Alt のフリールックが無効で、Alt で一時的に吸着を解除する。Shift を押している間は主選択にリサイズハンドルを表示する。
- 画面 GUI の Locked（既定 false）が true の要素は、ビューポートでクリックを通し、ドラッグ・揃え・全画面化の対象から外れる。Explorer と Properties からは選択・編集できる。YAML には true の場合だけ保存する。
- Explorer の Insert Object と Group は検索付きクラスピッカーを使う。検索は大文字小文字を区別しない部分一致。分類には Container、File、Script がある。
- Explorer の「座標を再計算」は、選択した部分木のワールド姿勢を保ったままローカル座標を再計算する。
- Replace Instance は一つの Instance を選択クラスの同名 Instance に置き換える。同名同型のプロパティと既存の子を引き継ぐ。Undo / Redo で置換前後を復元できる。System、Workspace、親なしのルートは対象外。確認後に選択クラスを変えた場合は確認をやり直す。
- Explorer の各親直下では Workspace、Folder、Model、Script、LocalScript、ModuleScript、物理ファイル、ValueBase、BaseCube、その他の順に表示する。同じ優先順位ではクラスごとにまとめ、名前を ASCII 順で並べる。連続した数字は数値として比較する。改名の確定後に並び順を更新し、親子関係、選択、展開状態を保つ。
- パネルの開閉は終了時に editor_settings.yaml の Panels に保存し、次回起動時に復元する。既定では Explorer、Properties、Viewport、Content Browser、Console を表示し、Animation と Profiler を閉じる。
- ようこそタブは起動時に表示し、シーンを読み込むと閉じる。復旧ダイアログを出す場合も表示する。
- テーマは設定メニューから即時切り替えでき、Preferences.Theme に保存する。Classic（既定）は紺色、Frutiger は淡い空色のガラス調。
- 起動設定、エディター設定、地形の YAML が破損している場合は原因をログに記録し、破損データを空データで上書きしない。

### 音声デバッグ

- View の「音声デバッグ」はパネルを開いている間に計測する。SFX・BGM のピーク、RMS、隣接サンプルの最大差、NaN、クリップ、コールバック間隔を表示する。計測による再生音の変化はない。
- Sound の再生位置、再生状態、音量、距離減衰後の音量、リスナーまでの距離、パンを表示する。Play、Stop、Reset、Seek の前後の状態と再生位置を記録する。
- 波形から連続再生時の巻き戻し位置を確認できる。Reset → Play の連続実行、距離減衰・パンのバイパス、テストトーン、Markdown のコピー、logs/ への Markdown・CSV 保存を利用できる。
- Sound の距離減衰は Volume / (1 + 距離[m] × 0.1)。位置はワールド座標を基準とする。

### GUI 自動化

- --ui-automation を指定すると、標準入力から UI の意味 ID を使って操作できる。--ui-automation-hidden を併用するとウィンドウを表示しない。
- capture はメインビューポートを物理ピクセル寸法の RGBA PNG として保存する。state はポップアップ、フォーカス、表示中の操作対象を 1 行の JSON で返す。wait のタイムアウトと操作対象が見つからない場合にも同じ状態を返す。
- profile は区間、GPU 時間、カウンターを 1 行の JSON で返す。script_profile on / off / reset / dump はスクリプト API 呼び出しの回数、自己時間、合計時間の計測を操作する。wait_frames は指定フレーム数だけ待つ。
- 未保存変更の確認待ち時間は通常起動で 3 秒、GUI 自動化では 0 秒。

### 自動保存とクラッシュ復旧

- Windows 版とポータブル版 macOS エディターの自動保存ルートは、実行ファイルと同じディレクトリの .autosave/。シーンごとにサブディレクトリを使う。開発時の macOS 起動では起動時の作業ディレクトリを基準にする。旧作業ディレクトリにある自動保存データは移行しない。
- 復旧データは変更が落ち着いて 1 秒後に保存する。世代スナップショットは既定で 5 分ごとに保存し、直近 5 世代を保持する。保存中の中断によって既存の保存データが途中書き込みの内容に置き換わることはない。
- 異常終了したセッションに有効な復旧データがあれば、次回起動時に最新候補を表示する。Recover は正式なシーンを保持し、復旧内容を未保存の状態で開く。正常終了、正常なシーン切り替え、または Discard で復旧データを削除し、世代スナップショットは保持する。
- データ未作成・欠落のセッションは候補に含めない。ファイルシステムの検査に失敗した場合とデータが破損している場合は、パス付きでログに記録する。
- 設定と自動保存のファイル I/O に失敗した場合は、操作、絶対パス、理由を日英併記の OS ダイアログに表示する。同一操作・パスで失敗が続く間の通知は一度だけ。成功後に再発すると再通知する。データ欠落、YAML 破損、保存形式への変換失敗はログに記録する。

## macOS の配布形式

- Studio は平坦な ZIP で配布し、展開先の RecubinStudio/ 直下に Recubin、RecubinEngine、assets、shaders、文書、空の .autosave/ を置く。実行ファイルにはそれぞれアドホック署名を付ける。再パッケージング時はローカルの自動保存を保持し、ZIP には自動保存したシーンを含めない。
- ゲームの macOS 配布形式は .app。

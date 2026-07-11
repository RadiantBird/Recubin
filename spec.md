# 仕様書
---
## 特殊なインスタンス
- **System**: シングルトン。常に1つのみ存在。Insert Objectリストには登録しない。
- **Workspace**: 複数インスタンスを持つ。切り替え可能。
- **StarterCharacter**: System直下に置く、キャラクターのテンプレートを保持するだけのコンテナ。
  中にHumanoid・Root(Cube)・その他のCube/Sphereを通常のInsert Object操作で組み立てる。
  Play開始時、この子要素が新規Model(`"PlayerCharacter"`、スクリプト互換のため名称固定)に
  cloneされてWorkspaceに追加される。StarterCharacterが存在しない場合は、既定のリグ(旧来
  ハードコードされていたもの)を持つStarterCharacterが自動的にSystem直下に生成される。

## 単位系
- **Roblox erik_stud(0.05 meterに等しい)**
- エンジン内部・PhysXともにstud値をそのまま使用する（座標/サイズ/速度の変換はしない）
- PhysXのPxTolerancesScaleをstud基準(length=20, speed=length*9.81)に設定し、
  1m=20studの比率をPhysX側に伝えることで内部の許容誤差・閾値を適切にスケールする
- 重力など、現実のSI単位の物理定数だけは個別にstud相当へ変換する
  (例: 9.81 m/s² -> 196.2 stud/s²、`include/Math/Units.hpp`参照)

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
- ControlMode
    - エディターではデフォルトでFree
    - ゲームランタイムではデフォルトでCharacter
- `CharacterAdded`(Signal): 新しい`PlayerCharacter`がspawnされるたび発火する(初回spawn +
  死亡respawn全て)。Luau側にはspawn直後のcharacter(Model)が引数として渡される
  (この時点ではまだWorkspaceに未追加。Root等のパーツ参照はresolveParts済みで取得可能)。
  respawnを跨いで参照を使い続けたいスクリプトは、起動時の`WaitChild`/`FindChild`で一度だけ
  参照を取るのではなく、この signal で都度取り直すこと。

## キャラクター(Humanoidクラス)
- StarterCharacter内のテンプレート、またはそのclone後にRoot/Torso/Head/LeftArm/RightArm/
  LeftLeg/RightLegという名前の兄弟Cube/Sphereを探して保持し、移動・ジャンプ・接地判定・
  歩行アニメーション・一人称時の身体非表示を行う
- `WalkSpeed`/`JumpPower`/`ClimbSpeed`を持つ(旧CharacterSettingの`moveSpeed`/`jumpPower`の統合先)
- GLFWwindow/SystemStateには依存しない。Userが入力をベクトル/boolに変換して渡す
- `jump()`は接地中に加え、`LiquidCube`に水没中も許可される(水中でもジャンプ/浮上できる)
- `Truss`に接触中は、W/Sで垂直移動、A/Dで水平ストレイフする(通常の歩行の代わり)
- `Seat`に接触すると自動着席し、Rootを`Weld`でSeatに固定する。着席中はジャンプキーで離脱し、
  WASDの入力はSeat.Steer/Seat.Throttleの更新にのみ使われる(通常の移動はしない)

## レイキャスト
PhysXに実装されているもののこと。

## ファイルパスを要求するプロパティ
- エディターに参照ボタンを追加する
- 読み込みに失敗すれば警告ログを出力
- 必要に応じてフォールバック処理/強制終了

## 物理制約
- ツリー構造のどこにあっても有効
- 必要なプロパティがそろえば自動で初期化される

## スクリプト
- スクリプトは自身の最初の先祖のworkspaceをグローバル変数として参照する
- スクリプトのソースコードは**エンジンによってアプリ実行中に動的に変更されることはない**

## Luau バインディング
### プロパティ解決の優先順位
- `instance.Key` のアクセスは、まずクラスのプロパティ（DispatchTable）を解決し、
  **プロパティが見つからない場合のみ**同名の子インスタンスを返す（Roblox 互換のドットチェーン）。
  → プロパティと同名の子がある場合、常にプロパティが優先される。
- プロパティ表は最派生クラス名をキーにキャッシュされる。基底クラスと派生クラスで
  **同名プロパティを定義しない**こと（衝突時の優先順位は未規定）。

### 値型（Luau グローバル）
- `Vector3.new(x,y,z)` / `Vector2.new(x,y)` / `Color4.new(r,g,b,a)`
- `Quaternion.new(w,x,y,z)`（引数なしで単位回転）/ `Quaternion.fromEuler(Vector3)` /
  `Quaternion.fromAxisAngle(axis, angleDeg)` / `Quaternion.Slerp(a,b,t)`。
  フィールド `.w/.x/.y/.z`、`:toEuler()`。`q * q`（合成）、`q * Vector3`（回転）。
- `CFrame.new()` / `(x,y,z)` / `(Vector3 pos)` / `(Vector3 pos, Quaternion rot)` /
  `CFrame.fromAxisAngle(axis, angleDeg)`。フィールド `.Position`(Vector3)/`.Rotation`(Quaternion)、
  `:inverse()`。`cf * cf`（合成）、`cf * Vector3`（ワールド点）。

### Spatial 系トランスフォーム（BaseCube/Model/Sound 等）
- `Position`(Vector3) / `Size`(Vector3) / `Rotation`(Quaternion) / `CFrame`(CFrame) を Read/Write。
  読み取り専用の `WorldPosition`(Vector3) / `WorldCFrame`(CFrame)。
- BaseCube 系では Write 時に PhysX 姿勢へ親チェーン合成込みで同期する。

### Instance 共通
- `Parent` は Read/Write（書込で reparent。`nil` 代入で親なし化）。
- `instance:Clone()` … サブツリーを複製し（制約参照も張り替え）、**親なし**で返す。
  返り値の `.Parent` を設定するまでツリーには入らない。
- Luau 側が保持する Instance 参照は `weak_ptr` であり、対象が破棄されると以後
  `instance.AnyProperty` は常に `nil` を返す（クラッシュはしない）。**死亡→respawn で
  `PlayerCharacter`/`Root` 等は都度新規インスタンスとして作り直されるため、スクリプト起動時に
  一度だけ `WaitChild`/`FindChild` で取得した参照は respawn を跨いで無効になる。**
  respawn を跨いで参照を使い続けたいスクリプトは、Heartbeat 等の中で毎回
  `workspace:FindChild("PlayerCharacter")` のように再取得すること。

### 数値プロパティのクランプ
- 不正値が困る一部の数値（Humanoid.WalkSpeed/JumpPower/MaxHealth、各種ライトの
  Brightness/Range/Angle 等）は Luau 書込時に定義レンジ `[lo, hi]` へクランプされる。

## GUI（ScreenGui/SurfaceGui/BillboardGui）
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
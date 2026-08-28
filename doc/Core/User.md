# User

`include/Core/User.hpp`

プレイヤーのカメラ制御とキャラクター操作を担う。Free モード（エディタ視点）と Character モード（キャラクター操作）の 2 モードを持つ。

## メンバ変数

### カメラ

| 変数 | 型 | 説明 |
|---|---|---|
| `cam.Orientation` | `Quaternion` | カメラの向き |
| `cam.Position` | `Vector3` | カメラ位置 |
| `cpos` | `Vector3&` | `cam.Position` への参照エイリアス |
| `forward` / `right` / `up` | `Vector3` | カメラのローカル基底ベクトル |
| `cameraDistance` | `float` | キャラクターからのズーム距離 |
| `characterSmoothing` | `float` | 移動方向・向きの補間率。既定値は`0.15`、`1`で補間なし、`0`で目標に追従しない。YAML/Luau入力は`[0,1]`へクランプする |
| `MovementInputEnabled` / `CameraInputEnabled` / `HotkeyInputEnabled` / `ToolInputEnabled` | `bool` | 既定`true`の保存対象カテゴリ。対応する組み込み入力だけを停止し、`User.Input`の生イベントとDirect APIは停止しない |

### キャラクター

| 変数 | 型 | 説明 |
|---|---|---|
| `character` | `Model*` | キャラクタールートノード |
| `root` / `torso` / `head` | `Cube*` | 体の各パーツ |
| `leftArm` / `rightArm` / `leftLeg` / `rightLeg` | `Cube*` | 四肢 |
| `walkCycle` | `float` | 歩行アニメーション位相 |

### 入力状態

| 変数 | 型 | 説明 |
|---|---|---|
| `isRightMouseRotating` | `bool` | 右ドラッグ回転中フラグ |
| `lastMouseX` / `lastMouseY` | `double` | 前フレームのマウス座標 |
| `pendingScrollY` | `double` | スクロール量の蓄積値 |

### マウスカーソル

`CursorType` は `Default` または `Type1`〜`Type10` を選択する。各 Type は
`CursorImages` の画像パス（PNG/JPEG/BMP/TGA）、リサイズ後の論理ピクセル単位の `HotspotX/Y`、
長辺の表示サイズ `Size` を持つ。`Size` の既定値は32px、設定範囲は1〜512pxで、画像の縦横比を維持する。
OSのcontent scaleを掛けて物理ピクセルへ変換するため、DPIが異なる環境でも論理サイズを維持する。
ゲーム領域上で選択中の画像をOSカーソルへ適用し、空欄・読込失敗時は標準カーソルへ戻す。
画像のAssetGuard確認、RGBA8読込、bilinear加工、mtimeと設定値による10スロットキャッシュは
`CursorImageProcessor`が担当する。入力バックエンドへは加工済みの物理寸法・Hotspot・RGBA・revisionのみを渡す。

Luauでは `Enum.CursorType.Default` と `Type1`〜`Type10` を使用する。各EnumItemは
`Name`、`Value`、`EnumType`を読み取り専用で持ち、`Enum.CursorType:GetEnumItems()`で列挙できる。
`User.CursorType`は選択値、`User.CursorTypeNSource`は`FileRef`または`nil`、
`User.CursorTypeNContentPath`は読み取り専用の保存パス、`User.CursorTypeNHotspotX/Y`は読み書き可能な
ホットスポット、`User.CursorTypeNSize`は1〜512の整数で読み書き可能な長辺サイズである。
画像はPNG/JPEG/BMP/TGAに対応し、SVGは今回対象外とする。

## 列挙型

```cpp
enum ControlMode { Free, Character, Program }
```

Humanoid死亡中もカメラ入力と`L`キーによるモード切り替えは有効である。Characterではキャラクターを移動・カメラ追従させずその場でカメラを回転し、FreeではWASD/QEで自由移動する。この間、Free移動が死亡ラグドールの姿勢を上書きすることはない。

## メソッド

| メソッド | 説明 |
|---|---|
| `updateVectors()` | `cam.Orientation` からカメラ基底ベクトルを再計算 |
| `processInput(physics)` | キーボード・マウス入力を処理して位置・向きを更新 |
| `spawnCharacter()` | キャラクターモデル（Model + Cube 7 体）を生成してワールドへ追加 |
| `despawnCharacter()` | キャラクターを削除してワールドからクリーンアップ |
| `getCursorType()` / `setCursorType(type)` | 使用するカーソル種別を取得・設定 |
| `getCursorImageSlot(index)` | 0-basedスロットの画像パス、ホットスポット、サイズを取得 |
| `setCursorImagePath(index, path)` | スロット画像を設定（空文字でクリア） |
| `setCursorHotspotX/Y(index, value)` | ホットスポットを0以上の整数で設定 |
| `setCursorSize(index, value)` | 長辺の論理サイズを1〜512pxへクランプして設定 |
| `applyCursor(gameplayHovered, contentScale)` | ゲーム領域上なら指定DPI scaleで画像を加工してカーソルを適用し、適用成功時`true` |

## 静的メンバ

| メンバ | 説明 |
|---|---|
| `s_instance` | GLFW コールバック用の自己ポインタ |
| `scrollCallback(window, x, y)` | GLFW スクロールコールバック |

## キー操作

| キー | 動作 |
|---|---|
| W / A / S / D | 移動（Free: カメラ前後左右、Character: キャラクター移動） |
| 右マウスドラッグ | カメラ回転 |
| スクロール | ズーム |
| L | Free / Character モード切り替え |
| LeftCtrl | CtrlLock ON/OFF 切り替え（Character モード） |
| F | CtrlLock オフセット方向（Left / Right）切り替え |
| Space | ジャンプ（Character モード） |
| F8 | gameplay入力中でフォーカスを持ち、テキスト入力中でなく、CameraInputEnabledのときMouseLockを切替 |

## 依存関係

- GLFW, GLM
- `Physics`（ジャンプ判定レイキャスト）
- `Model`, `Cube`（キャラクター構築）
- `SystemState`（ビューポートフォーカス確認）

## 使われる場所

- `main.cpp` でフレームごとに `processInput()` を呼ぶ
- `Renderer::renderScene()` でカメラ行列を取得するために参照される
- `ViewportPanel` がギズモ操作時に `User` のカメラ行列を参照する

## CharacterSmoothing

`CharacterSmoothing` はキャラクターの入力方向と向きが目標へ近づく割合で、`Humanoid::move()`へ渡される。
既定値`0.15`は従来の挙動と同じである。`1`は移動方向と向きを直ちに切り替え、`0`は補間対象を更新しない。
ネットワークではクライアント予測リプレイとホスト側のリモートAvatarシミュレーションにも、対応する
`User` の値を使う。係数は入力パケットに含めず、Hostが生成するリモートUserにはシーン権威の値をコピーする。

## Direct input API

Luauの`ToggleControlMode`、CtrlLock/MouseLockのToggle/Set、`SetMoveDirection`/`ClearMoveDirection`、
`Jump`、終了・ワークスペース要求、`SelectToolSlot(1..10)`、`ActivateTool`は入力カテゴリを迂回する。
`SetMoveDirection`は正規化したワールド方向を継続適用し、zeroまたは非有限値で解除する。`Jump`は次の
`processInput`でPhysicsを使って実行する。`ExitRequested`にlistenerがあれば終了は保留され、
`ConfirmExit`または`CancelExit`で決定する。MouseLockはprimary viewportが有効かつフォーカス中でのみ有効化でき、フォーカス喪失時に解除する。

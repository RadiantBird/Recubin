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

## 列挙型

```cpp
enum ControlMode { Free, Character }
```

## メソッド

| メソッド | 説明 |
|---|---|
| `updateVectors()` | `cam.Orientation` からカメラ基底ベクトルを再計算 |
| `processInput(physics)` | キーボード・マウス入力を処理して位置・向きを更新 |
| `spawnCharacter()` | キャラクターモデル（Model + Cube 7 体）を生成してワールドへ追加 |
| `despawnCharacter()` | キャラクターを削除してワールドからクリーンアップ |

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
| F | Free / Character モード切り替え |
| Space | ジャンプ（Character モード） |

## 依存関係

- GLFW, GLM
- `Physics`（ジャンプ判定レイキャスト）
- `Model`, `Cube`（キャラクター構築）
- `SystemState`（ビューポートフォーカス確認）

## 使われる場所

- `main.cpp` でフレームごとに `processInput()` を呼ぶ
- `Renderer::renderScene()` でカメラ行列を取得するために参照される
- `ViewportPanel` がギズモ操作時に `User` のカメラ行列を参照する

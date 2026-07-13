# Animation

`include/Instances/Animation.hpp`

時間tにおける各パーツ(`Cube`)のローカル`CFrame`をキーフレームで保持するインスタンス。各トラック(`AnimTrack`)はModel相対のパーツ名と、時刻昇順の`Keyframe`列を持つ。`Humanoid`がこれを再生し、対象パーツのcframeを毎フレーム補間して更新する。

## 継承
`Instance` → `Animation`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `Length` | `float` | 全長（秒、既定1.0） |
| `Speed` | `float` | 再生速度倍率（既定1.0） |
| `Looped` | `bool` | ループ再生するか（既定false。trueで末尾到達後に先頭へ戻って再生を続ける） |
| `m_tracks` | `vector<AnimTrack>`（private） | トラック一覧 |

`Keyframe`: `time`(秒) / `cframe`(対象Cubeのローカル(親=Model基準)CFrame) / `easing`(`EasingType`、次キーへの補間方法)。
`AnimTrack`: `partName`(Model相対のCube名) / `keyframes`(time昇順)。

## メソッド

| メソッド | 説明 |
|---|---|
| `getClassName()` | `"Animation"` を返す |
| `IsA(className)` | 継承チェーンを含む型チェック |
| `setProperty(name, value)` | YAMLデシリアライズ |
| `clone()` | トラックを含めた複製 |
| `evaluateTrack(track, t)` | 指定時刻tのCFrameを補間して返す（範囲外はクランプ） |
| `addOrReplaceKey(partName, time, cframe, easing)` | 指定パーツ・時刻にキーを追加（同時刻は上書き） |
| `removeKey(partName, time)` | 指定パーツの、時刻に最も近いキーを削除 |
| `getTracks()` | トラック一覧を取得（可変/不変） |
| `exportToFile(path)`/`importFromFile(path)` | 単体`.yaml`への書き出し/読み込み（シーンとは独立、再利用用）。失敗はリターンコード(false)で表現 |
| `trackFor(partName)`（private） | 指定パーツのトラックを返す（なければ作成） |

## 依存関係

- `CFrame`（Math/CFrame.hpp）, `EasingType`（Math/Easing.hpp）
- `Humanoid`（`playAnimation`/`updateAnimation`で再生・評価する消費側）

## 継承クラス

なし

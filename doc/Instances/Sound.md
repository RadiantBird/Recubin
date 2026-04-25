# Sound

`include/Instances/Sound.hpp`

3D 空間オーディオオブジェクト。`Spatial` を継承し、ワールド位置に基づいた立体音響を持つ。

## 継承

`Instance` → `Spatial` → `Sound`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `sound` | `ma_sound` | miniaudio のサウンドハンドル |
| `loaded` | `bool` | サウンドファイルが読み込み済みか |
| `looping` | `bool` | ループ再生フラグ |
| `soundGroup` | `string` | `"SFX"` または `"BGM"` |

## メソッド

| メソッド | 説明 |
|---|---|
| `play()` | 再生開始 |
| `stop()` | 停止 |
| `setLooping(bool)` | ループ設定 |
| `update3D()` | miniaudio の 3D 位置をワールド座標に同期 |
| `setProperty(name, value)` | YAML デシリアライズ用 |

## 依存関係

- `Spatial`, `AudioService`
- miniaudio（`ma_sound`）

## 使われる場所

- `AudioService::sounds` リストで管理
- `AudioService::updateSounds()` が毎フレーム `update3D()` を呼ぶ

# AudioService

`include/Core/AudioService.hpp`

miniaudio を使った空間オーディオサービス。BGM と SFX のグループボリュームを分けて管理する。

## 継承

`Instance` → `AudioService`

## 設計

シングルトン（`static AudioService* instance`）

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `engine` | `ma_engine` | miniaudio メインエンジン |
| `groupSFX` | `ma_sound_group` | SFX グループ（効果音） |
| `groupBGM` | `ma_sound_group` | BGM グループ（背景音楽） |
| `sounds` | `vector<Sound*>` | 登録済みサウンド一覧 |

## メソッド

| メソッド | 説明 |
|---|---|
| `initialize()` | miniaudio エンジンを初期化 |
| `setBGMVolume(volume)` | BGM グループのボリュームを設定 |
| `setSFXVolume(volume)` | SFX グループのボリュームを設定 |
| `addSound(sound)` | `Sound` インスタンスを管理リストに追加 |
| `updateSounds()` | 全 Sound の `update3D()` を呼んで空間位置を同期 |
| `uninit()` | miniaudio エンジンを解放 |

## 依存関係

- miniaudio（`ma_engine`, `ma_sound_group`）
- `Sound`, `Instance`

## 使われる場所

- `main.cpp` 起動時に `initialize()` を呼ぶ
- メインループ末尾で `updateSounds()` を呼ぶ
- `Sound` オブジェクトが `play()` / `stop()` を通じて間接的に使用

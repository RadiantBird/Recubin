# PostEffect

`include/Instances/PostEffect.hpp`

画面全体に適用するポストプロセスエフェクトを表すインスタンス。`Type`(`PostEffectKind`)でエフェクト種別を切り替え、`Param1`/`Param2`はタイプごとに意味が変わる汎用パラメータとして扱う。`ZIndex`で複数PostEffectの適用順を制御し、`Intensity`で元画像とのブレンド比率(0..1)を指定する。

## 継承
`Instance` → `PostEffect`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `Enabled` | `bool` | 有効/無効（既定true） |
| `Type` | `PostEffectKind` | エフェクト種別（既定`CRT`） |
| `ZIndex` | `int` | 適用順序 |
| `Intensity` | `float` | 元画像とのブレンド比率(0..1、既定1.0) |
| `Param1` | `float` | ScanlineCount(CRT) / Levels(Posterization) / PixelSize(Pixelize) / SaturationAmount(Saturation) / NoiseAmount(VHS) / Offset(ChromaticAberration)（既定8.0） |
| `Param2` | `float` | CurveAmount(CRT)。他タイプでは未使用（既定0.15） |

`PostEffectKind`: `None`/`CRT`/`Posterization`/`Pixelize`/`Saturation`/`VHS`/`ChromaticAberration`。

## メソッド

| メソッド | 説明 |
|---|---|
| `getClassName()` | `"PostEffect"` を返す |
| `IsA(className)` | 継承チェーンを含む型チェック |
| `setProperty(name, value)` | `Enabled`/`Type`(int→enum)/`ZIndex`/`Intensity`/`Param1`/`Param2`を処理 |
| `clone()` | 全プロパティを複製した新規インスタンスを返す |

## 依存関係

- レンダラー側のポストプロセスシェーダー（Saturation: -1で反転/0で白黒/1以上でラップアラウンド、VHS: デルタタイムでノイズ生成、ChromaticAberration: 0~1でチャンネルずれ量を指定。readme.md参照）

## 継承クラス

なし

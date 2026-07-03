# AssetGuard

`include/Util/AssetGuard.hpp`

アセットのパストラバーサル防御を行う名前空間関数群。配布ゲームを再生するランタイム（`RecubinEngine.exe`）でのみ有効化し、アセットルート（ゲームフォルダ）の外を指すパスの読み込みを拒否する。エディター（`Recubin.exe`）は `enableSandbox()` を呼ばないため `allow()` は常に `true` を返す。

## グローバル状態（匿名namespace内）

| 変数 | 型 | 説明 |
|---|---|---|
| `s_enabled` | `bool` | サンドボックス有効フラグ |
| `s_root` | `filesystem::path` | アセットルート（正規化済み） |

## メソッド

| メソッド | 説明 |
|---|---|
| `enableSandbox(root)` | サンドボックスを有効化しアセットルートを確定する（ランタイム起動時に一度だけ呼ぶ） |
| `allow(path)` | `path` がアセットルート配下なら `true`。無効時は常に `true`。ルート外（絶対パス/`..`脱出/別ドライブ）は `false` を返し警告ログを出す |

## 判定ロジック

```
allow(path)
  s_enabled == false → true（エディター/テストは無制限）
  path が空 → true（各ローダ側の既存処理に委ねる）
  resolved = weakly_canonical(s_root / path)  ※失敗時は lexically_normal でフォールバック
  isWithin(s_root, resolved) が false → 警告ログ + false
  それ以外 → true
```

## 依存関係

- `Util/Logger`（`RCBN_LOG`, `RCBN_WARN`）
- `<filesystem>`

## 使われる場所

- ランタイム起動時にアセットルートを指定して `enableSandbox()` を呼ぶ
- アセット（テクスチャ・シーン・スクリプト等）を読み込む各ローダが `allow()` でパス検証

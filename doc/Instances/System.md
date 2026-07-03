# System

`include/Instances/System.hpp`

シーンツリーのルート付近に常に1つだけ存在するシングルトン・インスタンス。エディターの「Insert Object」リストには登録されない特殊クラス（spec.md参照）。`Heartbeat` シグナルや、無限Clone・無限Restart・スクリプトの無限ループを検知する安全マージン値を保持する。

## 継承
`Instance` → `System`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `Heartbeat` | `shared_ptr<RCBNScriptSignal>` | 毎フレーム発火するシグナル |
| `MaxClonesPerFrame` | `int` | 1フレームで許容するClone回数の上限（既定1000） |
| `MaxRestartsPerFrame` | `int` | 1フレームで許容するScript Restart回数の上限（既定100） |
| `ScriptLoopTimeoutSeconds` | `float` | スクリプトループのタイムアウト秒数。0以下で無効（既定2.0） |

`MaxClonesPerFrame`/`MaxRestartsPerFrame`/`ScriptLoopTimeoutSeconds` は意図的にLuauへバインドしない（DispatchTable/SetterTableに非登録）。エディターからのみ編集可能。

## メソッド

| メソッド | 説明 |
|---|---|
| `getClassName()` | `"System"` を返す |
| `IsA(className)` | 継承チェーンを含む型チェック |
| `addChild(child)` | 子が `Workspace` の場合、既存名と衝突しない一意な名前に自動リネームしてから追加 |
| `setProperty(name, value)` | 安全マージン3変数のYAMLデシリアライズ |

## フロー — 安全対策（無限Clone/Restart検知）

```
スクリプトが Clone()/Restart() を呼ぶたびカウント（1フレーム単位で集計）
  → 呼び出し回数が System.MaxClonesPerFrame / MaxRestartsPerFrame を超過
      → ランタイムを強制停止
      → 呼び出し元スクリプトを種類ごとに集計し、多い順にソートしてエラー出力
        ("Infinite cloning possible" / "Infinity recursion possible")

スクリプトのループ処理が ScriptLoopTimeoutSeconds を超えて継続
  → タイムアウトとして強制停止（0以下なら無効）
```

## 依存関係

- `RCBNScriptSignal`（Heartbeat）
- `Workspace`（子として保持し、名前重複を解決する）

## 継承クラス

なし

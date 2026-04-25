# Instance

`include/Instances/Instance.hpp`

すべてのシーンオブジェクトの基底クラス。親子関係を持つ木構造を形成する。

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `Name` | `string` | オブジェクト名 |
| `Parent` | `Instance*` | 親ノード（ルートは nullptr） |
| `children` | `unordered_map<string, Instance*>` | 子ノード一覧 |

## メソッド

| メソッド | 説明 |
|---|---|
| `GetClassName()` | クラス名文字列を返す（YAML・スクリプト用） |
| `IsA(className)` | 継承チェーンを含む型チェック |
| `setParent(newParent)` | 親を変更（循環参照を防止） |
| `findFirstAncestorWorkspace()` | 上位を遡って Workspace を探す |
| `onAncestorChanged()` | 祖先変更時の仮想コールバック |
| `addChild(child)` / `removeChild(name)` | 子の追加・削除 |
| `getChild(name)` / `getChildren()` | 子の取得 |
| `getFullPath()` | ルートからのフルパス文字列 |
| `setProperty(name, value)` | YAML デシリアライズ用プロパティセッタ |

## 依存関係

- YAML-cpp（シリアライズ）
- `Logger`

## 継承クラス

```
Instance
  ├─ Spatial
  │    ├─ BaseCube
  │    │    └─ Cube
  │    ├─ Sound
  │    └─ Model
  ├─ Script
  ├─ Decal
  └─ Workspace
```

## ポイント

`setParent()` が呼ばれると `onAncestorChanged()` が子孫に伝播する。これにより `BaseCube` や `Script` が `Workspace` に追加・削除されたときに自動で物理エンジン・スクリプトエンジンへ登録/解除される。

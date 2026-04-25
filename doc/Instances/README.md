# Instances

シーンオブジェクト階層。すべてのシーン要素は Instance を継承した木構造で管理される。

## 継承ツリー

```
Instance
  ├─ Spatial
  │    ├─ BaseCube
  │    │    └─ Cube          ← 描画可能なキューブ（最も頻繁に使用）
  │    ├─ Sound              ← 3D 空間オーディオ
  │    └─ Model              ← グループコンテナ
  ├─ Script                  ← Luau スクリプト
  ├─ Decal                   ← 面テクスチャ
  └─ Workspace               ← シーンルート
```

## クラス一覧

| クラス | ファイル | 概要 |
|---|---|---|
| [Instance](Instance.md) | `include/Instances/Instance.hpp` | 基底クラス（名前・親子関係） |
| [Spatial](Spatial.md) | `include/Instances/Spatial.hpp` | 3D トランスフォームを持つ基底 |
| [BaseCube](BaseCube.md) | `include/Instances/BaseCube.hpp` | 物理対応キューブ基底 |
| [Cube](Cube.md) | `include/Instances/Cube.hpp` | 描画可能なキューブ |
| [Workspace](Workspace.md) | `include/Instances/Workspace.hpp` | シーンルート・登録窓口 |
| [Script](Script.md) | `include/Instances/Script.hpp` | Luau スクリプトコンテナ |
| [Sound](Sound.md) | `include/Instances/Sound.hpp` | 3D 空間オーディオ |
| [Decal](Decal.md) | `include/Instances/Decal.hpp` | 面テクスチャオーバーレイ |
| [Model](Model.md) | `include/Instances/Model.hpp` | オブジェクトグループ |

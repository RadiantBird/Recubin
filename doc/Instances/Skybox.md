# Skybox

`include/Instances/Skybox.hpp`

シーンを覆う巨大キューブ（一辺5000stud）としての空表現。`Cube` を継承し、6面それぞれに専用テクスチャパスを持つ。常にカメラ位置へ追従し、無衝突・無影・無ライティング（`Unlit`）で描画される。

## 継承

`Instance` → `Spatial` → `BaseCube` → `Cube` → `Skybox`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `skyboxPaths[6]` | `std::string` | 6面のテクスチャパス（UI順: Right, Left, Top, Bottom, Front, Back） |

## メソッド

| メソッド | 説明 |
|---|---|
| `Skybox()` | サイズ5000stud, `Anchored=true`, `CanCollide=false`, `CastShadow=false`, `Unlit=true` で初期化 |
| `IsA(className)` | `"Skybox"`, `"Cube"`, `"BaseCube"`, `"Spatial"`, `"Instance"` に対して true |
| `setProperty(name, value)` | `SkyboxPaths`（6要素シーケンス）を各面へ一括反映、それ以外は `Cube::setProperty` |
| `clone()` | サイズ以外のプロパティと6面パスをコピーし子を再帰複製 |
| `setSkyboxPath(faceIndex, path)` | UI面インデックス→`Face` enum へマッピングし、該当 `Decal` 子の `texturePath`/`TextureID` を更新 |

## フロー

```
setSkyboxPath(faceIndex, path)
  → UI index (0-5: Right,Left,Top,Bottom,Front,Back)
    → Cube::Face enum (Front,Back,Top,Bottom,Right,Left) へマッピング
  → children から face が一致する Decal を検索
    → decal->texturePath = path
    → Renderer::instance->loadTexture(path) → decal->TextureID

Renderer::renderScene()
  → 毎フレーム Skybox::teleportTo(cameraPosition) でカメラに追従（フォーカス中のみ）
```

## 依存関係

- `Cube`, `Decal`, `Renderer`（`loadTexture`）

## 継承クラス

なし

# Texture

`include/Instances/Texture.hpp`

面ごとのテクスチャ設定を表す `Instance`（`Decal` に似た構造だが、タイル繰り返し設定 `StudsPerTileU/V` を追加で持つ）。単体の `Instance` 直下の子であり `Spatial` 系の継承は持たない。

## 継承

`Instance` → `Texture`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `TextureID` | `unsigned int` | OpenGL テクスチャ ID |
| `face` | `Face` | 対象面（Front/Back/Top/Bottom/Right/Left） |
| `texturePath` | `std::string` | テクスチャファイルパス |
| `Color` | `Color4` | テクスチャに乗算する色 |
| `StudsPerTileU` | `float` | U方向のタイル繰り返し間隔（studs） |
| `StudsPerTileV` | `float` | V方向のタイル繰り返し間隔（studs） |

## メソッド

| メソッド | 説明 |
|---|---|
| `Texture(textureID, targetFace)` | 名前を `"Texture_<Face名>"` として初期化 |
| `getClassName()` | `"Texture"` を返す |
| `IsA(className)` | `"Texture"`, `"Instance"` に対して true |
| `setProperty(name, value)` | `Texture`（パス）, `Face`, `Color`, `StudsPerTileU`, `StudsPerTileV` を設定 |
| `clone()` | 全メンバをコピー |
| `setFace(f)` | `face` を変更し `Name` を `"Texture_<Face名>"` に更新 |
| `setTexturePath(path)` | パスを保存し `Renderer::instance->loadTexture()` でテクスチャをロード |

## フロー

```
setProperty("Texture", path)
  → setTexturePath(path)
    → texturePath = path
    → TextureID = Renderer::instance->loadTexture(path)
```

## 依存関係

- `Decal.hpp`（`Face` enum を共有）, `Color4`, `Renderer`

## 継承クラス

なし

# SurfaceGui

`include/Instances/SurfaceGui.hpp`

`Cube` の指定フェイスに直接描画される GUI。専用 FBO にベイクしたテクスチャを `Cube::draw()` 内でフェイステクスチャとして合成する（`BillboardGui` のように ImGui オーバーレイとしては描画されない）。

## 継承
`Instance` → `WorldGuiObject` → `SurfaceGui`

## メンバ変数
| 変数 | 型 | 説明 |
|---|---|---|
| `face` | `Face` | 描画対象のフェイス（Front/Back/Top/Bottom/Right/Left） |
| `m_fboID` | `unsigned int` | ベイク用フレームバッファ（Renderer が管理） |
| `m_texID` | `unsigned int` | ベイク先テクスチャ |
| `m_texW`/`m_texH` | `int` | ベイクテクスチャの解像度 |
| `m_bakedContentSignature` | `uint64_t` | 最後に正常にベイクした描画内容のランタイム署名 |
| `m_hasBakedContentSignature` | `bool` | 有効なベイク署名を保持しているか |

## メソッド
| メソッド | 説明 |
|---|---|
| `~SurfaceGui()` | FBO/テクスチャの GL リソースを解放 |
| `hasRenderableDirectChild()` | 可視ピクセルを持つ直接子があるかを動的確保なしで判定 |
| `contributesBakedVisualOverride()` | Visible、有効なベイクテクスチャ、自身または直接子の可視内容が揃い、Cube面を実際に上書きするかを判定 |
| `computeRenderContentSignature(defaultFontSize)` | 実際にベイクされる背景・子要素から描画内容署名を計算 |
| `IsA(name)` | 継承チェーンを含む型チェック |
| `setProperty(name, val)` | YAML デシリアライズ用 |
| `clone()` | 自身と子を複製 |

## フロー

```
Renderer::renderViewport 内 Main Pass
  → Workspace 走査中、Cube の子に SurfaceGui を発見
    → renderWorldGui からは除外（"3D フェイス描画に移行"）
    → bakeSurfaceGui(): m_fboID へ子 GUI 要素を描画
      → 前回成功時とサイズ・描画内容署名が同じなら m_texID をそのまま再利用
      → Cube::draw() がフェイス描画時に m_texID を合成
        → フラグメントシェーダーの isSurfaceGui 分岐で
          mix(ourColor, texColor, texColor.a) ブレンド
```

FBO ID、テクスチャ ID、描画内容署名はランタイムキャッシュであり、cloneやYAMLへは保存されない。
透明・非表示・未ベイクで`contributesBakedVisualOverride()`がfalseのSurfaceGuiは、子として存在していても
親Cubeのメインパスインスタンシングを阻害しない。

## 依存関係
- `WorldGuiObject`, `Named`, `Decal.hpp`（`Face` 列挙）
- OpenGL（`GL/glew.h`、FBO/テクスチャ管理）
- `Cube::draw()`（`src/Instances/Cube.cpp`）、`fragment.glsl`（`isSurfaceGui` 分岐）

## 継承クラス
なし

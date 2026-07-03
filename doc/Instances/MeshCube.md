# MeshCube

`include/Instances/MeshCube.hpp`

GLB（glTF Binary）ファイルから任意メッシュを読み込み描画する `BaseCube`。頂点は最大軸が1.0になるよう正規化・中心化され、`Size` プロパティで他のプリミティブと同じ意味の実寸を持つ。物理形状は凸メッシュ（ConvexMesh）。

## 継承

`Instance` → `Spatial` → `BaseCube` → `MeshCube`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `MeshFile` | `std::string` | ロード済み GLB ファイルパス（空なら未ロード） |
| `m_VAO` / `m_VBO` / `m_EBO` | `unsigned int` | GPU ジオメトリバッファ（インスタンスごと） |
| `m_indexCount` | `unsigned int` | インデックス数 |
| `m_textureID` | `unsigned int` | GLB 埋め込みテクスチャ（最初に見つかったもののみ使用） |
| `m_cpuVertices` | `std::vector<MeshVertex>` | CPU側頂点（Position/Normal/U,V/MatAlpha） |
| `m_cpuIndices` | `std::vector<unsigned int>` | CPU側インデックス |

`MeshVertex` は `Position`, `Normal`, `U`, `V`, `MatAlpha`（マテリアルの `baseColorFactor.a`、透過対応）を保持する。

## メソッド

| メソッド | 説明 |
|---|---|
| `MeshCube(Pos, Sz)` | ジオメトリ未ロード状態で生成 |
| `~MeshCube()` | `releaseGPU()` で GPU リソース解放 |
| `loadFromGLB(path)` | GLB を解析しメッシュ・テクスチャ・GPUバッファを再構築。失敗時は既存状態を変更せず false |
| `draw(modelLoc, shaderProgram)` | VAO が未生成なら何もしない。テクスチャ未ロード時は `Renderer::whiteTexture` を使用 |
| `IsA(className)` | `"MeshCube"`, `"BaseCube"`, `"Spatial"`, `"Instance"` に対して true |
| `setProperty(name, value)` | `MeshFile` 設定時に `loadFromGLB` を実行、成功したら物理アクターを `recreateActor` で再構築 |
| `clone()` | 位置・サイズ・色等をコピーし、`MeshFile` があれば再ロードして複製 |
| `getPhysicsShape()` | `PhysicsShape::ConvexMesh` を返す |
| `getConvexVertices()` | `m_cpuVertices` の位置をそのまま PhysX 用に変換 |
| `hasGeometry()` / `getVAO()` / `getIndexCount()` | 描画・シャドウパス向けのアクセサ |

## フロー

```
loadFromGLB(path)
  → 拡張子チェック(.glb) + AssetGuard::allow(path)
  → cgltf_parse_file / cgltf_load_buffers
  → 各 node の mesh.primitives を走査
    → node のワールド行列で Position/Normal を変換
    → UV, マテリアル baseColorFactor.a を頂点へ格納
    → 最初のプリミティブの埋め込みテクスチャを Renderer::loadTextureFromMemory でロード
  → 全頂点のバウンディングボックスを計算 → 中心化 + 最大軸1.0に正規化
  → releaseGPU() → CPU頂点/インデックスを保持 → uploadToGPU()
```

## 依存関係

- `BaseCube`, `Renderer`, `Physics`, `AssetGuard`, `Logger`
- cgltf（GLBパーサ）, OpenGL（GLEW）, PhysX

## 継承クラス

なし

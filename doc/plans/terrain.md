# Terrain 設計ドキュメント
> Recubin エンジン向け / Claude Code への引き渡し用

---

## 1. 世界スケール

| 項目 | 値 |
|------|-----|
| ワールドサイズ | 65536 × 512 × 65536 ブロック |
| ブロックサイズ | 4 × 4 × 4 studs |
| チャンクサイズ | 16 × 16 × 16 ブロック |
| チャンク総数 | 4096 × 32 × 4096（約5億、同時展開はしない） |

チャンク座標は `int32_t` で保持。プレイヤー周辺のチャンクのみメモリに展開し、遠いチャンクはファイルに退避するストリーミング方式。

---

## 2. データ層

### BlockShape

```cpp
enum class BlockShape : uint8_t {
    Empty,
    Cube,
    // Wedge（角を一辺削る）8種
    Wedge_TopNE, Wedge_TopNW, Wedge_TopSE, Wedge_TopSW,
    Wedge_BotNE, Wedge_BotNW, Wedge_BotSE, Wedge_BotSW,
    // Tetra（角を一点削る）8種
    Tetra_TopNE, Tetra_TopNW, Tetra_TopSE, Tetra_TopSW,
    Tetra_BotNE, Tetra_BotNW, Tetra_BotSE, Tetra_BotSW,
};
// 計20種、uint8_t で余裕
```

- Wedge・Tetraの向きは「どの角を削ったか」で識別
- 回転は保持しない（形状enumで方向を表現）

### BlockMaterial

```cpp
enum class BlockMaterial : uint8_t {
    Grass,
    Dirt,
    Stone,
    // 以降追加予定
};
```

### Block

```cpp
struct Block {
    BlockShape   shape;    // 1 byte
    BlockMaterial material;// 1 byte
    uint8_t r, g, b;       // 3 byte
};
// 計 5 byte/ブロック
```

### Chunk

```cpp
struct Chunk {
    Block    blocks[16][16][16]; // 約 20 KB
    int32_t  cx, cy, cz;        // チャンク座標
    TerrainMesh mesh;            // 描画用メッシュ
};
```

---

## 3. 描画層

### TerrainVertex

```cpp
struct TerrainVertex {
    float x, y, z;    // 位置
    float nx, ny, nz; // 法線
    float u, v;       // UV
    float r, g, b;    // 色（ブロックごとに異なるため頂点に持たせる）
};
// 計 36 byte/頂点
```

### TerrainMesh

```cpp
struct TerrainMesh {
    GLuint   VAO = 0;
    GLuint   VBO = 0;
    GLuint   EBO = 0;
    uint32_t indexCount = 0;
    bool     dirty = true; // true のとき次フレームで再生成
};
```

### メッシュ生成の方針

- チャンク内の全ブロックをループ
- 隣接ブロックが `Empty` の面のみ頂点を追加（隠れ面除去）
- ブロック形状（Cube / Wedge / Tetra）ごとに頂点テーブルを用意
- `dirty = true` のチャンクは毎フレーム冒頭で再生成してVBOを更新

### Renderer への統合

- 既存の `renderViewport` にTerrain描画パスを追加
- 既存のシェーダーをほぼ流用予定（頂点構造が近いため）

---

## 4. 物理層

### 方針

- **TriangleMesh**を使用（洞窟・複雑地形に対応するため）
- Terrainは動かないので **`PxRigidStatic`** で作成（kinematic不要）
- 描画用に生成した頂点データをそのままPhysXに流用
- cookingは既存の`Physics.cpp`インフラ（`PxCookingParams`等）をそのまま使う

### Chunkへの追加

```cpp
struct Chunk {
    Block blocks[16][16][16];
    int32_t cx, cy, cz;
    TerrainMesh mesh;
    physx::PxRigidStatic* physicsActor = nullptr;
};
```

### チャンク物理生成

```cpp
void buildChunkPhysics(Chunk& chunk, Physics& physics) {
    PxCookingParams cookParams(Physics::s_pxPhysics->getTolerancesScale());

    PxTriangleMeshDesc desc;
    desc.points.data     = physVerts.data();
    desc.points.count    = (PxU32)physVerts.size();
    desc.triangles.data  = physIndices.data();
    desc.triangles.count = (PxU32)(physIndices.size() / 3);

    PxDefaultMemoryOutputStream buf;
    PxCookTriangleMesh(cookParams, desc, buf);

    PxDefaultMemoryInputData input(buf.getData(), buf.getSize());
    PxTriangleMesh* triMesh = Physics::s_pxPhysics->createTriangleMesh(input);

    PxRigidStatic* actor = Physics::s_pxPhysics->createRigidStatic(PxTransform(PxIdentity));
    PxRigidActorExt::createExclusiveShape(*actor, PxTriangleMeshGeometry(triMesh), *mat);
    physics.scene->addActor(*actor);

    chunk.physicsActor = actor;
    triMesh->release(); // Geometryに渡したら解放でOK
}
```

### dirty フラグとの連携

- `dirty = true` のとき、描画メッシュと物理メッシュを**同時に再生成**
- 再生成前に古い`physicsActor`を`scene->removeActor`して`release`する

---

## 5. 生成アルゴリズム（概要）

```
for x in chunk:
    for z in chunk:
        height = noise(worldX, worldZ) * MAX_HEIGHT
        for y in chunk:
            if worldY < height:
                blocks[x][y][z] = { Cube, Stone/Dirt/Grass, ... }
            else:
                blocks[x][y][z] = { Empty, ... }
```

- ノイズ関数は未選定（Perlin / Simplex どちらでも可）
- 地表付近をGrass、その下をDirt、深部をStoneにする予定

---

## 6. チャンクストリーミング

### ファイル構造

```
terrain/
  r_0_0.yaml    # チャンク(cx:0-31, cz:0-31) の全y層
  r_0_1.yaml
  r_1_0.yaml
  ...
```

- 1リージョン = 32×32チャンク（全y層含む）
- リージョンファイル名: `r_{rx}_{rz}.yaml`
- リージョン座標: `rx = cx / 32`, `rz = cz / 32`

### YAMLフォーマット（RLE圧縮）

```yaml
chunks:
  - cx: 0
    cy: 0
    cz: 0
    blocks:
      # [count, shape, material, r, g, b]
      - [256, 0, 2, 128, 128, 128]  # Stone x256
      - [128, 0, 1, 139, 90, 43]    # Dirt x128
      - [16, 1, 0, 0, 0, 0]         # Empty x16
```

- 走査順: `x → z → y`（水平方向に同ブロックが続くため圧縮率が高い）
- shapeとmaterialはenum値をuint8_tとして保存

### ストリーミングフロー（同期、毎フレーム）

```
毎フレーム:
  1. プレイヤーのチャンク座標を計算
  2. 半径4チャンク以内 → メモリになければロード
  3. 半径4チャンク超  → メモリにあればアンロード

ロード:
  1. リージョンファイルを開く（なければ新規生成）
  2. 対象チャンクのYAMLを読んでRLEを展開
  3. Chunkデータを構築、dirty=true でメッシュ・物理を生成

アンロード:
  1. 変更があればリージョンファイルに書き戻す
  2. physicsActor を removeActor/release
  3. VAO/VBO/EBO を解放
  4. メモリから解放
```

- 常駐チャンク数: 半径4 = 9×9×32 = 約2592チャンク（最大）
- 将来的に非同期ロードへ移行予定

## 7. 頂点テーブル

### 頂点番号定義

```
    6---7
   /|  /|
  4---5 |
  | 2-|-3
  |/  |/
  0---1

0: (-1,-1,-1)  1: ( 1,-1,-1)
2: (-1,-1, 1)  3: ( 1,-1, 1)
4: (-1, 1,-1)  5: ( 1, 1,-1)
6: (-1, 1, 1)  7: ( 1, 1, 1)
```

### 形状ごとの使用頂点

| 形状 | 使用頂点 | 省く頂点 |
|------|----------|----------|
| Cube | 0-7（全部） | なし |
| Wedge_TopNE | 0,1,2,3,4,5,6 | 7 |
| Wedge_TopNW | 0,1,2,3,5,6,7 | 4 |
| Wedge_TopSE | 0,1,2,3,4,6,7 | 5 |
| Wedge_TopSW | 0,1,2,3,4,5,7 | 6 |
| Wedge_BotNE | 0,2,4,5,6,7 | 1,3（下辺） |
| Wedge_BotNW | 1,3,4,5,6,7 | 0,2 |
| Wedge_BotSE | 0,1,3,5,6,7 | 2,4（一部） |
| Wedge_BotSW | 0,1,2,4,6,7 | 3,5 |
| Tetra_TopNE | 0,1,2,3,7 | 4,5,6 |
| Tetra_TopNW | 0,1,2,3,4 | 5,6,7 |
| Tetra_TopSE | 0,1,2,3,5 | 4,6,7 |
| Tetra_TopSW | 0,1,2,3,6 | 4,5,7 |
| Tetra_BotNE | 1,4,5,6,7 | 0,2,3 |
| Tetra_BotNW | 0,4,5,6,7 | 1,2,3 |
| Tetra_BotSE | 3,4,5,6,7 | 0,1,2 |
| Tetra_BotSW | 2,4,5,6,7 | 0,1,3 |

### 面生成の方針

- 隣接ブロックが `Empty` → 面を出す
- 隣接ブロックが `Cube` → 面をスキップ（カリング）
- 隣接ブロックが `Wedge`/`Tetra` → 面をそのまま出す（ブレンドは将来対応）

---

## 8. 未決定事項

- [x] ノイズ関数の選定 → Perlin noise（fBm、オクターブ重ね合わせ）
- [x] Wedge/Tetraの頂点テーブル定義
- [ ] ブラシ機能（保留中）
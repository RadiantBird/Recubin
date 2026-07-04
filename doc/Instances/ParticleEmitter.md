# ParticleEmitter

`include/Instances/ParticleEmitter.hpp`

汎用パーティクル発生源。火・煙・水しぶき・汎用スクエアなど、タイプ別クラスを増やさず
プロパティの組み合わせだけで表現する。BaseCube等の子として配置し、発生位置は毎フレーム
親のワールドCFrameから継承する（`LightSource`と同じ設計。自身はPosition/Sizeを持たない）。
描画はカメラ常時正面のビルボードで、テクスチャなしの単色頂点シェーダーを用いる。

## 継承

`Instance` → `ParticleEmitter`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `StartColor`/`EndColor` | `Color4` | 発生時/消滅時の色。`.a`がそのままフェードアウトを兼ねる |
| `StartSize`/`EndSize` | `float` | 発生時/消滅時の板の半径基準(stud) |
| `EmitRate` | `float` | 連続発生数(個/秒) |
| `MaxParticles` | `int` | 同時上限 |
| `Lifetime`/`LifetimeVariance` | `float` | 寿命(秒)とその±ランダム幅 |
| `Speed`/`SpeedVariance` | `float` | 初速(stud/s)とその±ランダム幅 |
| `Direction` | `Vector3` | 発生方向（親のローカル空間。発生時に親のワールド回転で変換） |
| `SpreadAngle` | `float` | `Direction`を軸にしたランダム拡散円錐の半頂角(度) |
| `GravityScale` | `float` | `Workspace::Gravity`への乗数。負値で火・煙のように上昇 |
| `SpinSpeed`/`SpinSpeedVariance` | `float` | ビルボード面内での自転速度(度/秒)とその±ランダム幅 |
| `Rotation`/`RotationVariance` | `float` | 発生時の初期回転角(度)とその±ランダム幅 |
| `Enabled` | `bool` | 連続発生(`EmitRate`)のみを止める。既存粒子の寿命消化と`Emit()`は止めない |

## メソッド

| メソッド | 説明 |
|---|---|
| `update(dt)` | 粒子の積分・寿命判定・連続発生を1フレーム分進める |
| `emit(count)` | `Enabled`を無視して即座に`count`個発生させる |
| `getParticles()` | 現在の粒子バッファを読み取り専用で返す（Rendererが描画に使用） |
| `updateAll(root, dt)` *(static)* | ツリーを再帰的に辿り、見つけた全`ParticleEmitter`の`update(dt)`を呼ぶ |
| `setProperty(name, value)` | YAML デシリアライズ用 |

## シミュレーションと描画の分離

`update()`はメインループから**毎フレーム1回だけ**（`ParticleEmitter::updateAll`経由で）呼ばれる。
`Renderer::renderParticles()`はエディターの複数ビューポート分だけ1フレームに複数回呼ばれうるため、
粒子の状態を変更せず`getParticles()`を読むだけに徹する。

```
main.cpp / game_main.cpp（毎フレーム1回）
  ParticleEmitter::updateAll(workspace, dt)
    → 各 ParticleEmitter::update(dt)
        → 死亡粒子除去 → 重力積分 → EmitRateに応じて spawnOne()

Renderer::renderViewport()（ビューポートの数だけ）
  → renderParticles(workspace, view, projection, cameraRight, cameraUp)
      → collectParticleEmitters() で全発生源を収集
      → 各粒子をカメラ基底(cameraRight/cameraUp)でビルボード展開し、1つの頂点バッファへ
      → 1回の glDrawArrays(GL_TRIANGLES) で描画（発生源横断でバッチ）
```

## 依存関係

- `Instance`, `Spatial`（親のワールドCFrame取得）, `Workspace`（`Gravity`取得）
- `Vector3`, `Quaternion`, `CFrame`, `Color4`
- `Renderer`（`m_particleVAO`/`m_particleVBO`/`m_particleShader`、`collectParticleEmitters`）

## 使われる場所

- `SceneHierarchyPanel::renderInsertMenu()`「効果」サブメニューから配置
- `PropertiesPanel`のスキーマ駆動インスペクタでプロパティ編集
- `LuauEngine`のバインディングで全プロパティの読み書きと`:Emit(count)`が呼び出し可能

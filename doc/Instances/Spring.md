# Spring

`include/Instances/Spring.hpp`

2つの`BaseCube`を双方向ばねで接続する物理制約インスタンス。Box3Dの`b3DistanceJoint`（`enableSpring`）を使い、`FreeLength`（自然長）へ向けて引きも押しも行う。Ropeのような力の片側制限・長さ制限（`enableLimit`）は持たない。`Attachment0/1`が無ければ各Cubeの中心がアンカー。`Visible`がtrueの間、Rendererがコイル状に描画する。

## 継承
`Instance` → `PhysicsConstraint` → `Spring`

## プロパティ

| プロパティ | 型 | 説明 |
|---|---|---|
| `Cube0`/`Cube1` | `BaseCube`参照 | 接続対象（Workspace相対パス） |
| `Attachment0`/`Attachment1` | `Attachment`参照（任意） | アンカー位置。未設定ならCube中心 |
| `FreeLength` | `float` | 自然長(stud)。0=生成時のアンカー間距離を採用（update時も現在の自然長を保持） |
| `Stiffness` | `float` | ばね定数k（既定100.0）。Ropeと同じk,c→hertz/dampingRatio変換 |
| `Damping` | `float` | 減衰c（既定10.0） |
| `Visible` | `bool` | コイル描画の表示（既定true）。物理には影響しない |
| `Color` | `Color4` | コイル色 |
| `Radius` | `float` | コイル半径(stud, 既定0.5) |
| `Coils` | `int` | 巻き数（既定8） |
| `Thickness` | `float` | 線の太さ(stud, 既定0.1) |
| `Enabled` | `bool` | 制約の有効/無効 |

## 描画
`Renderer::renderConstraints`が`SpringHelix::build`（`include/Util/SpringHelix.hpp`）で両アンカー間のコイル折れ線を作り、Ropeと同じリボン経路で描く。両端は軸上の直線リード、2点がほぼ重なる場合は直線へ退避する。

## 依存関係
`BaseCube`, `Attachment`, `Workspace`, `Physics`（`createSpring`）, `Box3DPhysicsBackend`, `Renderer`（friend）

# Recubin -Powering imagination again-

## 目標
- Robloxの感覚でソフトウェア（ゲーム）を開発し、ローカルで公開できるようにしたい。
- 過剰な本人確認はしない。まあローカルだし。
- Robloxでできなかったことを実現する(複数Workspaceとか特殊効果とか)。
- わかりやすいエディターを作る。直感的であれ。
- 面白い作品を規制なしで作れるコミュニティを作る。
- 物理で遊べるようにする

## 現時点の懸念
- 縄とかモーターとかどうやって実装するんですか？
- 溶接をRobloxみたいにアセンブリにするけど、抽象ヒットボックスはどうやって作るか？
それに、どうやって力をルートに伝達して計算するか？
回答
```
1. 物理・ジョイント（縄、モーター）

PhysXを使っているなら、これらは「自分で計算する」のではなく「PhysXの機能をどうLuauに露出させるか」の問題になります。

    縄 (Rope): PxDistanceJoint で最大距離を制限するか、よりリアルにするなら PxArticulationReducedCoordinate を使って複数の短い棒を繋ぎます。

    モーター: PxD6Joint が最強です。これ一つで自由度を固定したり、ドライブ（動力）を与えたり、Robloxの HingeConstraint や Servo と同等のことができます。

2. 溶接とアセンブリ、力の伝達

Robloxの「Assembly」の概念は、PhysXの PxRigidDynamic そのものです。

    抽象ヒットボックス: 透明な PxShape を PxRigidActor に追加し、シミュレーション（衝突）はするが描画はしない設定にするだけです。

    力の伝達: PhysXでは、溶接（Weld）されたパーツ群は一つの「Actor」として扱われます。どこに力を加えても、重心（Center of Mass）に基づいて全体の挙動が計算されるので、自分でルートへの伝達を計算する必要はありません。
```

## Todoリスト
(小さい数字から順番にやる)

- 物理制約のテストを実行し、実行ログを保存しておく

- 物理制約の作成 (5)
  - Rope(最大距離だけを制限)
  - Rod(距離を固定し、回転を制限しない)
- アセンブリ剛体(PxRigidDynamic)を利用した溶接の実装 (6)
  - これを"Weld"Instanceとして定義。
    - cube0: BaseCube
    - cube1: BaseCube
  - アセンブリ化されたすべてのBaseCubeを収集できるようにする(正しく溶接できるように)
- モーター(ヒンジ)
  - cube0: BaseCube
  - cube1: BaseCube
  接着面をXYZで決定する
  接着面の中心を作用点とする

## 中断された作業
- 特にない

## 現在の問題
- なし
## 使用中の技術
- C++
- OpenGL
- Luau
- stb_image
- miniaudio
- PhysX
- Windows bat
- Python
---
Rust?今はいらない。俺はRustで製品をブランディングしない。
...けど、そろそろ必要かもな。
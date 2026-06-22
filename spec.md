# 仕様書
---
## 特殊なインスタンス
- **System**: シングルトン。常に1つのみ存在。Insert Objectリストには登録しない。
- **Workspace**: 複数インスタンスを持つ。切り替え可能。
- **StarterCharacter**: System直下に置く、キャラクターのテンプレートを保持するだけのコンテナ。
  中にHumanoid・Root(Cube)・その他のCube/Sphereを通常のInsert Object操作で組み立てる。
  Play開始時、この子要素が新規Model(`"PlayerCharacter"`、スクリプト互換のため名称固定)に
  cloneされてWorkspaceに追加される。StarterCharacterが存在しない場合は、既定のリグ(旧来
  ハードコードされていたもの)を持つStarterCharacterが自動的にSystem直下に生成される。

## 単位系
- **Roblox erik_stud(0.05 meterに等しい)**
- エンジン内部・PhysXともにstud値をそのまま使用する（座標/サイズ/速度の変換はしない）
- PhysXのPxTolerancesScaleをstud基準(length=20, speed=length*9.81)に設定し、
  1m=20studの比率をPhysX側に伝えることで内部の許容誤差・閾値を適切にスケールする
- 重力など、現実のSI単位の物理定数だけは個別にstud相当へ変換する
  (例: 9.81 m/s² -> 196.2 stud/s²、`include/Math/Units.hpp`参照)

## インスタンス(Instanceクラスを継承したクラス)
- 基本的に親(Parent)を持つ
- 親が所有権を持つ
- 親が削除された場合、子も再帰的に削除

- 新規クラスは自動的にエディターに公開される
- エディターに基本的なプロパティを公開する(エンジン内部の状態、セキュリティ上公開してはいけないものなどを除く)
- Luau側に基本的なRead/Writeプロパティをバインディングする
- エディターの「Insert Object」リストに登録される(一部の抽象クラスは除外)

## ユーザー(Userクラス)
- clone後のキャラクター本体(`character`, Model)を持つ。個別の身体パーツへの参照は持たず、
  移動・ジャンプ・接地判定・歩行アニメーションは`character`内のHumanoidに委譲する
- Cameraを持つ
- 入力を管理する
- ControlMode
    - エディターではデフォルトでFree
    - ゲームランタイムではデフォルトでCharacter

## キャラクター(Humanoidクラス)
- StarterCharacter内のテンプレート、またはそのclone後にRoot/Torso/Head/LeftArm/RightArm/
  LeftLeg/RightLegという名前の兄弟Cube/Sphereを探して保持し、移動・ジャンプ・接地判定・
  歩行アニメーション・一人称時の身体非表示を行う
- `WalkSpeed`/`JumpPower`を持つ(旧CharacterSettingの`moveSpeed`/`jumpPower`の統合先)
- GLFWwindow/SystemStateには依存しない。Userが入力をベクトル/boolに変換して渡す

## レイキャスト
PhysXに実装されているもののこと。

## ファイルパスを要求するプロパティ
- エディターに参照ボタンを追加する
- 読み込みに失敗すれば警告ログを出力
- 必要に応じてフォールバック処理/強制終了

## 物理制約
- ツリー構造のどこにあっても有効
- 必要なプロパティがそろえば自動で初期化される

## スクリプト
- スクリプトは自身の最初の先祖のworkspaceをグローバル変数として参照する
- スクリプトのソースコードは**エンジンによってアプリ実行中に動的に変更されることはない**
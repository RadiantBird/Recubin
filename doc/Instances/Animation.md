# Animation

`include/Instances/Animation.hpp`

Scene Tree上でアニメーション資産を表すインスタンス。唯一のトラックデータとして
`AnimationClip`を所有し、`Humanoid`から明示的に参照される。ファイル資産、内蔵Clip、
旧Scene埋め込みAnimationは、いずれも同じClip評価器へ渡される。

## 継承
`Instance` → `Animation`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `Length` | `float` | 全長（秒、既定1.0） |
| `Speed` | `float` | 再生速度倍率（既定1.0） |
| `Looped` | `bool` | ループ再生するか（既定false。trueで末尾到達後に先頭へ戻って再生を続ける） |
| `ContentPath` | `string` | `.rcanim`への通常のプロジェクト相対パス。例: `assets/anims/r6_walk.rcanim` |
| `m_clip` | `unique_ptr<AnimationClip>`（private） | 唯一のトラックデータ |
| `m_source` / `m_loadStatus` | enum（private） | `LegacyEmbedded`/`File`/`BuiltIn`と読込結果 |

`Keyframe`: `time`(秒) / `cframe`(対象Cubeのローカル(親=Model基準)CFrame) / `easing`(`EasingType`、次キーへの補間方法)。
`AnimTrack`: `partName`(Model相対のCube名) / `keyframes`(time昇順)。

## メソッド

| メソッド | 説明 |
|---|---|
| `getClassName()` | `"Animation"` を返す |
| `IsA(className)` | 継承チェーンを含む型チェック |
| `setProperty(name, value)` | YAMLデシリアライズ |
| `clone()` | トラックを含めた複製 |
| `evaluateTrack(track, t)` | 指定時刻tのCFrameを補間して返す（範囲外はクランプ） |
| `addOrReplaceKey(partName, time, cframe, easing)` | 指定パーツ・時刻にキーを追加（同時刻は上書き） |
| `removeKey(partName, time)` | 指定パーツの、時刻に最も近いキーを削除 |
| `getTracks()` | トラック一覧を取得（可変/不変） |
| `loadContent()` | `ContentPath`からClipを読み込む。失敗してもContentPathとInstanceを変更しない |
| `resolveR6WalkClip()` | 有効なR6 joint-delta Clipを返す。読込失敗時だけ実行中の内蔵Walkを返す |
| `exportToFile(path)`/`importFromFile(path)` | `.rcanim`のexportと、`.rcanim`または旧単体`.yaml`のimport。失敗はfalse |
| `trackFor(partName)`（private） | 指定パーツのトラックを返す（なければ作成） |

## 依存関係

- `CFrame`（Math/CFrame.hpp）, `EasingType`（Math/Easing.hpp）
- `Humanoid`（`playAnimation`/`updateAnimation`で再生・評価する消費側）

## 継承クラス

なし

## AnimationClipとの関係

`AnimationClip`は`name`/`rig`/`length`/`speed`/`looped`と時刻順トラックを持つ純粋な
ランタイムデータで、Scene Treeへ直接公開しない。`.rcanim`のキーはRigのバインド姿勢からの
Jointローカル差分（`space: joint_delta`）。旧AnimationのModel相対トラックはロード時に
`space: model_relative`のClipへ変換し、別コンテナを併存させない。

`.rcanim`はYAMLの`recubin.type: animation`、`version: 1`ヘッダーを必須とする。
未知のversion、型違い、破損・不正な値は採用しない。Walkとして参照されている場合は
参照やファイルを書き換えず、実行中だけ内蔵R6 Walkへフォールバックする。状態はPropertiesの
`Source`、`LoadStatus`、`UsingBuiltInFallback`で確認できる。

# AnimationEditorPanel

`include/Editor/AnimationEditorPanel.hpp`

選択中 Model の子 Animation をキーフレーム編集するパネル。再生バーで時間 `t` を指定し、Model 直下の Cube を選択してギズモで動かした後「Add Key」で現在の CFrame をキーとして記録する。フォーカスが入っている間だけ Model 配下の Cube を一時的に改変するプレビュー専用ツールで、フォーカスが外れると元の CFrame（バインドポーズ）へ復元する。

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `selectedInstance` | `Instance**` | 選択中インスタンスへのポインタ（`SceneHierarchyPanel` と共有） |
| `m_history` | `CommandHistory*` | Animation 生成コマンドを Undo 可能にする（借用） |
| `m_time` | `float` | 現在の再生時間 |
| `m_playing` | `bool` | 再生中フラグ |
| `m_easingChoice` | `int` | 次に追加するキーの `EasingType`（コンボボックス選択値） |
| `m_wasFocused` | `bool` | 直前フレームのフォーカス状態（フォーカス遷移検出用） |
| `m_poseSaved` | `bool` | バインドポーズを退避済みか |
| `m_savedModel` | `Instance*` | バインドポーズ退避元の Model |
| `m_bindPose` | `unordered_map<string, CFrame>` | 退避した partName→CFrame |

## メソッド

| メソッド | 説明 |
|---|---|
| `onRender()` | ウィンドウ全体の描画（フォーカス管理・再生コントロール・キーフレーム編集・トラック一覧） |
| `resolveModel()` | 選択インスタンスから編集対象の Model を求める（Animation/Model/Spatial いずれの選択にも対応） |
| `findAnimation(model)` | Model 直下の最初の Animation インスタンスを返す |
| `saveBindPose(model)` / `restoreBindPose()` | フォーカスイン/アウト時に Model 配下 Spatial の CFrame を退避・復元 |
| `applyPreview(anim, model, t)` | Root 相対のキーフレームを現在の Root CFrame に合成して各 Cube に適用 |

## フロー

```
onRender()
  フォーカス遷移: saveBindPose() / restoreBindPose()
  Animation 無ければ [Create Animation] → AddInstanceCommand (Undo対応)
  Play/Pause/Stop, Speed, Export/Import(.yaml)
  Time スライダー操作 or Play中
    └─ applyPreview(anim, model, m_time)
  選択Cubeがあれば [Add Key]
    └─ Root相対CFrame算出 → anim->addOrReplaceKey(partName, t, cframe, easing)
  トラック一覧: キーごとに easing変更 / Go(該当tへジャンプ) / X(削除)
```

## 依存関係

- `EditorPanel`, `CommandHistory`
- `Instance`, `Spatial`, `Animation`, `AnimTrack`, `Keyframe`
- `Math/CFrame`
- ImGui, Windows COM（`IFileDialog`, Export/Import 用ファイル選択）

## 使われる場所

- `EditorManager` がパネルの一つとして所有

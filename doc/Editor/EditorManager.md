# EditorManager

`include/Editor/EditorManager.hpp`

全エディタパネルを所有・管理し、ImGui ドックスペースの描画を統括するクラス。

## 列挙型

```cpp
enum class EditorMode { Edit, Play, Pause }
```

## メンバ変数（public）

| 変数 | 型 | 説明 |
|---|---|---|
| `mode` | `EditorMode` | 現在の動作モード |
| `consolePanel` | `unique_ptr<ConsolePanel>` | ログコンソール |
| `hierarchyPanel` | `unique_ptr<SceneHierarchyPanel>` | シーン階層ビュー |
| `propertiesPanel` | `unique_ptr<PropertiesPanel>` | プロパティインスペクタ |
| `contentBrowserPanel` | `unique_ptr<ContentBrowserPanel>` | アセットブラウザ |
| `viewportPanel` | `unique_ptr<ViewportPanel>` | 3D ビューポート |
| `profilerPanel` | `unique_ptr<ProfilerPanel>` | 描画・物理・スクリプトのフレーム時間グラフ |
| `scenePath` | `string` | 保存/読み込み対象のシーンファイルパス（デフォルト: `assets/scenes/test_scene.yaml`） |
| `m_history` | `CommandHistory` | Undo/Redo スタック（各パネルに生ポインタで共有） |

## メソッド（public）

| メソッド | 説明 |
|---|---|
| `render(GLFWwindow*)` | ドックスペース・全パネル `onRender()`・ショートカット処理を行う（メインループから呼ぶ） |
| `render()` | `render(nullptr)` の互換オーバーロード |
| `setWorkspace(ws)` | Stop 後のリロード時に全パネルの workspace ポインタを一括更新し、history をクリアする |
| `beginViewportRender()` | ViewportPanel の FBO にバインド（3D 描画前に呼ぶ） |
| `endViewportRender()` | FBO をアンバインドしてテクスチャを ImGui に転送 |
| `isEditMode()` / `isPlayMode()` / `isPauseMode()` | モード確認 |
| `isDirty()` | 未保存変更があるか |
| `markDirty()` | 未保存フラグを立てる |
| `requestSaveDialog(window)` | 未保存確認ダイアログを次フレームで表示するようリクエスト |

## プライベートメソッド

| メソッド | 説明 |
|---|---|
| `renderToolbar()` | Play/Pause/Stop・ギズモモード・オブジェクト追加・Save/Load ボタンを描画 |
| `handleEditorShortcuts()` | Edit モード中のキーボードショートカット処理 |
| `renderSaveDialog()` | 未保存確認モーダルダイアログの ImGui 描画 |
| `saveCurrentScene()` | `SceneLoader::saveScene()` でシーンを保存し `m_isDirty` をクリア |
| `applyTheme()` | 起動時に呼ぶ ImGui カラーテーマ適用 |

## キーボードショートカット（Edit モードのみ）

| ショートカット | 動作 |
|---|---|
| `Ctrl+S` | シーン保存（テキスト入力中でも有効） |
| `Ctrl+Z` | Undo |
| `Ctrl+Shift+Z` | Redo |
| `Backspace` | 選択インスタンスを削除（Viewport 非フォーカス時） |
| `Ctrl+C` | 選択インスタンスをクリップボードへコピー |
| `Ctrl+V` | クリップボードから兄弟として貼り付け |
| `Ctrl+Shift+V` | クリップボードから選択インスタンスの子として貼り付け |
| `1`〜`4` | Basicツールバー表示中、Select／Move／Resize／Rotateを選択（テキスト入力中は無効） |

## ツールバーボタン

Play/Pause/Stop、Select/Move/Resize/Rotate（ギズモモード）、New Cube/New Cylinder/New Prism/New Sphere、Save/Load。  
オブジェクト追加は `CommandHistory` 経由で Undo 対応。名前衝突は連番サフィックスで自動回避。

ツールバーは青系の控えめなガラス調ボタン、2本線の区切り、選択中ツールの強調を使う。ガラスは
暗いbase、上下の明暗差、上部30%のsheen、上端のinner highlight、下辺・右辺の暗いborderで構成し、
文字はこれらの背景層より前面に描画する。Snap は Move／Rotate／Resize の単位付き設定としてまとまり、
Collision Fit は独立した `Fit` 操作として表示する。
既存のドッキング、各ツールのトグル挙動、キーボードショートカットは変更しない。

大きなツールバー群の間には`drawToolbarMajorSeparator()`を置く。このhelperは左右10pxの余白と、
58pxボタン行を基準に中央配置する40pxの暗線＋明線を一体として描画する。Snap内部は線を増やさず、
Move／Rotate／Resize／Fitの間を12pxの余白で分ける。

Dock tabは同じ配色の軽い表現として、暗いinactive tab、明るいactive tab、active時の青白い上端lineを
使う。panel titleはより弱い濃紺のbaseとborderだけに留め、本文背景をガラス化しない。

## パネル間のデータ共有

```
hierarchyPanel->selectedInstance
        ↓（ポインタを共有）
propertiesPanel->selectedInstance  ← 同じ Instance* を参照
viewportPanel->selectedInstance    ← 同じ Instance* を参照してギズモを描画

m_history（CommandHistory）
        ↓（生ポインタで共有）
hierarchyPanel->m_history
propertiesPanel->m_history
viewportPanel->m_history

m_clipboard（shared_ptr<Instance>）
        ↓（生ポインタで共有）
hierarchyPanel->m_clipboard
```

## 依存関係

- ImGui, ImGuizmo
- `ConsolePanel`, `SceneHierarchyPanel`, `PropertiesPanel`, `ContentBrowserPanel`, `ViewportPanel`
- `ProfilerPanel`, `FrameProfiler`
- `CommandHistory`, `ViewportFocusManager`
- `Workspace`, `User`, `SceneLoader`

## Script / TextFile 補助エディタ

Explorer の `Script` / `TextFile` 行をダブルクリックすると、`EditorManager` が同じ
`MainDockSpace` に `CodeEditorPanel` を追加する。同一 Instance は既存タブを再利用し、
パネルは `weak_ptr` で対象を参照するため、シーン切替・Instance削除後に dangling pointer を残さない。
JetBrains Mono は `Renderer::init()` で一度だけ読み込み、コード本文と行番号だけへ渡す。
エディタへフォーカスがある間の Ctrl+S は Script の `Source` / 元の `Path`、または TextFile の
RuntimeFileSystem overlay を保存し、通常の Ctrl+S は従来どおり Scene 保存として扱う。コードファイルの
保存では Scene dirty を変更せず、終了時にコード用の未保存確認を別途表示する。

## 使われる場所

- `Renderer` が `unique_ptr<EditorManager>` として所有
- `Renderer::renderImGui()` から `render()` を呼ぶ

## Character Animation参照migration

- 保存済みSceneのロード後、R6 StarterCharacterかつ
  `character_animation_bindings.version: 1`未記録の場合だけ判定する。
- WalkAnimationが空欄なら、StarterCharacterへ`R6Walk` Animationを追加して
  `assets/anims/r6_walk.rcanim`を参照する。旧headerにWalkパスがあればScene相対から新規則へ変換する。
- 非空のユーザー参照は、未解決・欠損・破損でも変更しない。migration versionだけを記録する。
- migration後にユーザーが参照を差し替えたり削除しても自動挿入しない。
- Treeまたはmetadataを変更した場合はDirtyにするが自動保存しない。無題Scene、非R6、Play snapshot復元は変更しない。
- Fileメニューの`Restore Default Animations`をユーザーが明示実行した場合だけ標準Walk参照を再設定する。
- `migrateCharacterAnimationBindings`はGUIを構築しないTree＋metadataの静的処理で、Editor表示側も同じ結果を利用する。

## Scene Autosave / Crash Recovery

EditorManagerはコンストラクタで明示的なAutosave保存rootを受け取り、AutosaveManagerへ渡す。
Windowsでは`main.cpp`が`GetModuleFileNameW`で取得した実行中の`Recubin.exe`の親ディレクトリを使う。
macOSの平坦portable配布は`.autosave` markerと必須resourceを確認して実行ファイル隣へCWD、設定、
Autosave rootを揃え、開発実行は従来の起動CWDを使う。旧CWD配下のAutosaveは自動移行・候補走査しない。
Edit中の変更は1秒debounceの`recovery.rcbn`と5分周期の世代snapshotへ同期保存し、
正常なScene切替・終了時はlockとrecoveryだけを削除する。起動時に有効な残存lockを検出した場合は
Welcomeより先にRecoveryモーダルを表示し、Recover、Autosaveフォルダー表示、Discardを提供する。
Recoverはrecoveryをsource、lock記録のScenePathをlogical pathとしてtransactional loadし、
正式Sceneファイルを上書きしない。
設定ファイルとAutosaveの実I/O失敗は日英のOS標準ダイアログへoperation、絶対path、OS理由を表示する。
SettingsメニューのVSync診断トグルは既定で有効とし、変更時にメインOpenGL contextへ即時適用する。
値は`editor_settings.yaml`の`Preferences.VSync`として保存・復元する。
同一障害の連続表示は抑制し、成功後の再発時は再通知する。

# Play 中の Workspace 追従

Play 中はローカル Character の所属 Workspace が Primary Viewport と Explorer の表示対象になる。
Character が Workspace 外にある間は現在の対象を維持する。Secondary Viewport は開いた Workspace に固定され、
Free/Program カメラの変換は Workspace 切替時にテレポートしない。

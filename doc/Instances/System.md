# System

`include/Instances/System.hpp`

シーンツリーのルート付近に常に1つだけ存在するシングルトン・インスタンス。エディターの「Insert Object」リストには登録されない特殊クラス（spec.md参照）。`Heartbeat` シグナルや、無限Clone・無限Restart・スクリプトの無限ループを検知する安全マージン値を保持する。

## 継承
`Instance` → `System`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `Heartbeat` | `shared_ptr<RCBNScriptSignal>` | 毎フレーム発火するシグナル |
| `MaxClonesPerFrame` | `int` | 1フレームで許容するClone回数の上限（既定1000） |
| `MaxRestartsPerFrame` | `int` | 1フレームで許容するScript Restart回数の上限（既定100） |
| `ScriptLoopTimeoutSeconds` | `float` | スクリプトループのタイムアウト秒数。0以下で無効（既定2.0） |
| `BaseResolution` | `Vector2` | `ScreenGuiObject`(`Norm::Pixel`)の自動スケーリング基準解像度（既定 1920x1080）。Luauからは読み取り専用 |
| `LockAspectRatio` | `bool` | ランタイム用（既定false）。trueなら RecubinEngine はウィンドウの縦横比に関わらず `BaseResolution` の縦横比を保ち、余白を黒帯にする。3Dは黒帯の内側のサイズのオフスクリーンFBOへ描いてウィンドウ中央へ転送し（`Renderer::render`）、ゲームGUI・チャット・マウスのレイも同じ内側の矩形を使う（`NullEditorManager::renderUI`、矩形は `ScreenGuiLayout::lockedAspectArea`）。エディターのビューポートには影響しない。Luauからは読み取り専用、trueのときだけYAMLへ出力 |
| `AllowControlModeSwitch` | `bool` | ランタイム用（既定false）。trueなら RecubinEngine で L キーによる Free/Character の切り替えを許可する（`User::allowControlModeSwitch` へ毎フレーム反映）。エディターのPlayは従来どおり EditorManager が決める。Luauからは読み取り専用、trueのときだけYAMLへ出力 |
| `AllowWorkspaceSwitch` | `bool` | ランタイム用（既定false）。trueなら RecubinEngine で `User:RequestWorkspaceSwitch()` による Workspace 切り替えを行う。falseなら要求を捨てる。Luauからは読み取り専用、trueのときだけYAMLへ出力 |
| `UseFixedResolution` | `bool` | エディター用（既定true）。trueならエディターのビューポートを `BaseResolution` の縦横比で黒帯付き表示する。falseなら黒帯なしでパネルの実サイズをそのまま描画解像度にし、`Norm::Pixel` の画面GUIは縦横同率（比率の小さい方）で拡大縮小する。ランタイム(game_main)には影響しない。Luauからは不可視、falseのときだけYAMLへ出力 |
| `EnableIOAPI` | `bool` | Luau I/O APIの許可（既定false） |
| `EnableIPCAPI` | `bool` | IPC APIスタブの許可（既定false） |
| `EnableExternalFileAccess` | `bool` | ゲーム領域外ファイルアクセスの許可（既定false） |
| `DefaultCameraMode` | `CameraMode` | パッケージ版ゲーム起動時のカメラモード（`Character`/`Free`/`Program`、既定`Character`） |
| `NetworkInterpolationDelay` | `float` | Clientで他プレイヤー・同期物体を何秒前の状態として補間表示するか（0〜1、既定0.1）。長いほど通信の揺れに強く滑らか、短いほど現在に近い。Luauからは読み取り専用 |

`MaxClonesPerFrame`/`MaxRestartsPerFrame`/`ScriptLoopTimeoutSeconds` は意図的にLuauへバインドしない（DispatchTable/SetterTableに非登録）。エディターからのみ編集可能。

`BaseResolution` は `PropertyRegistry` 経由で登録され、YAML保存/エディター編集/Luau読み取りに対応する（`.luaReadOnly()` によりLuauからの書込のみ不可）。`Renderer::renderScreenGui` が現在のビューポート解像度との比率を計算し、`Norm::Pixel` の `Position`/`Size` にX,Y別々に乗算する（`doc/Core/Renderer.md` 参照）。ただしエディターのビューポートで `UseFixedResolution` が false のときは、縦横の比率の小さい方を両軸に使う（`GameGuiRenderContext::uniformPixelScale`）。

拡張フラグもYAMLとエディターの通常System inspectorで扱うが、Luauからは読み取り専用である。通常ランタイムでは有効な拡張の同意receiptが必要になり、Editor/`--editor-test`では警告receiptを省略する。

`DefaultCameraMode` はPropertyRegistryへ登録された文字列enumで、YAML保存とエディター編集に対応し、Luauからは読み取り専用である。パッケージ版ゲームはこの値を起動時のUserカメラモードへ適用する。旧シーンや未知のYAML値は`Character`へフォールバックする。

## メソッド

| メソッド | 説明 |
|---|---|
| `getClassName()` | `"System"` を返す |
| `IsA(className)` | 継承チェーンを含む型チェック |
| `addChild(child)` | 子が `Workspace` の場合、既存名と衝突しない一意な名前に自動リネームしてから追加 |
| `setProperty(name, value)` | 安全マージン3変数のYAMLデシリアライズ |

## フロー — 安全対策（無限Clone/Restart検知）

```
スクリプトが Clone()/Restart() を呼ぶたびカウント（1フレーム単位で集計）
  → 呼び出し回数が System.MaxClonesPerFrame / MaxRestartsPerFrame を超過
      → ランタイムを強制停止
      → 呼び出し元スクリプトを種類ごとに集計し、多い順にソートしてエラー出力
        ("Infinite cloning possible" / "Infinity recursion possible")

スクリプトのループ処理が ScriptLoopTimeoutSeconds を超えて継続
  → タイムアウトとして強制停止（0以下なら無効）
```

## 依存関係

- `RCBNScriptSignal`（Heartbeat）
- `Workspace`（子として保持し、名前重複を解決する）

## システム拡張と同意

receiptはポータブルroot直下の`system_extensions.receipt`に、構成versionと権限集合（IO/IPC/External）を保存する。
有効権限集合または構成versionが一致しない場合だけ、通常ランタイムのスクリプト開始前に警告を表示する。
Continue時だけreceiptを書き込み、失敗時は警告して次回も再表示する。Quitまたはウィンドウ閉鎖は起動中止となる。
全権限無効時も空集合のreceiptを更新する。Editorと`--editor-test`は警告・receiptを完全にバイパスする。

## 継承クラス

なし

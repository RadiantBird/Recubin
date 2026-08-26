# GUI Automation

GUI自動化は通常起動へ影響しない opt-in 機能です。`--ui-automation` を付けた
Editorだけが標準入力を行単位で読み取り、結果を `[UIAUTO] OK ...` または
`[UIAUTO] ERROR ...` として出力します。引数は引用符で囲めます。

対応コマンドは次の形式です。

| コマンド | 引数 |
|---|---|
| `help` / `targets` / `quit` | なし |
| `wait` | `<target> [timeoutFrames]` |
| `move` / `click` / `right_click` | `<target>` |
| `type` | `<UTF-8 remainder>` |
| `key` | `<Ctrl+F等のキー表記>` |
| `mouse` | `<x> <y>` |
| `mouse_down` / `mouse_up` | `<left\|right\|middle>` |
| `wheel` | `<x> <y>` |
| `focus_window` | `<ImGui window name>` |
| `capture` | `<output path>` |

Explorerの代表的なtarget IDは次の通りです。

- `Explorer/Context/InsertObject`
- `Explorer/Context/Group`
- `Explorer/Context/ReplaceInstance`
- `Explorer/Context/SelectChildren`
- `Explorer/ClassPicker/Search`
- `Explorer/ClassPicker/Category/Cubes`
- `Explorer/ClassPicker/Class/Cube`
- `Explorer/Node/<full path>`

例:

```text
focus_window "###Explorer"
wait "Explorer/Node/System\\"
right_click "Explorer/Node/System\\"
wait "Explorer/Context/InsertObject"
click "Explorer/Context/InsertObject"
wait "Explorer/ClassPicker/Search"
click "Explorer/ClassPicker/Search"
type script
wait "Explorer/ClassPicker/Category/Script"
capture "artifacts/script-picker.png"
quit
```

`capture` はmain viewportのdefault framebufferをphysical framebuffer sizeで読み取り、
RGBA PNGとして保存します。OpenGLのback framebufferを対象とし、secondary viewportは
対象外です。通常起動時はreader、入力注入、target登録、captureのいずれも有効化されません。

## 実行契約

入力は`IPlatform`の非ブロッキング標準入力取得をメインスレッドから行う。
Windows、macOS、Mockで同じインターフェイスを実装し、reader threadやdetached
threadは使用しない。コマンドの構文検証は共有pure validatorを本番queue処理と
回帰テストで共用し、余剰引数や終端のない`key Ctrl+`は拒否する。

fixtureを使用するスモークでは、`--ui-automation-scene <scene>`と
`--ui-automation-settings <settings>`を`--ui-automation`と同時に指定する。
captureはRGBA PNGとして保存される。

## Popupと未保存変更の自動化契約

`--ui-automation` 専用モードでは未保存変更確認のdanger cooldownを0秒とする。通常のEditor操作では
3秒を維持する。Class Pickerの置換確認popupは選択クラスに紐づき、選択クラスが変わった場合は
再確認する。選択変更では旧クラスの承認だけを破棄してpickerを維持し、確定・取消では保留中の操作を閉じて次の挿入・置換へ状態を漏らさない。

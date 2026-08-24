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

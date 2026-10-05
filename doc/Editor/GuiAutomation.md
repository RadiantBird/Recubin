# GUI Automation

GUI自動化は通常起動へ影響しない opt-in 機能です。`--ui-automation` を付けた
Editorだけが標準入力を行単位で読み取り、結果を `[UIAUTO] OK ...` または
`[UIAUTO] ERROR ...` として出力します。引数は引用符で囲めます。

対応コマンドは次の形式です。

| コマンド | 引数 |
|---|---|
| `help` / `targets` / `state` / `profile` / `quit` | なし |
| `script_profile` | `<on\|off\|reset\|dump>` |
| `wait_frames` | `<フレーム数>` |
| `wait` | `<target> [timeoutFrames]` |
| `move` / `click` / `right_click` | `<target>` |
| `type` | `<UTF-8 remainder>` |
| `key` | `<Ctrl+F等のキー表記>` |
| `mouse` | `<x> <y>` |
| `mouse_down` / `mouse_up` | `<left\|right\|middle>` |
| `wheel` | `<x> <y>` |
| `focus_window` | `<ImGui window name>` |
| `capture` | `<output path>` |

## 状態の取得（`state`）

`state`は、直前のフレームが終わった時点のImGuiの状態を、1行のJSONで`[UIAUTO] OK state {...}`として
標準出力へ出す。画像を見なくても、ダイアログが開いているか、どこにフォーカスがあるかを確認できる。
コマンドはキューに並ぶので、`wait`の後に置けばその時点の状態になる。

| フィールド | 内容 |
|---|---|
| `frame` | 自動化のフレーム番号 |
| `hiddenWindow` | `--ui-automation-hidden`で起動しているか |
| `popups` | 開いているポップアップ（外側から順） |
| `modal` | 最前面のモーダル（なければ`null`） |
| `focus` / `hovered` | フォーカス中・マウス下のウィンドウ（なければ`null`） |
| `windows` | 表示中の最上位ウィンドウ（最大60件） |
| `targetCount` / `targets` | 見えているtargetの数と名前（名前は先頭60件、昇順） |

ウィンドウ名は翻訳されるため、`###`を含む場合は`###`以降の安定したIDだけを出す。

`wait`のタイムアウトと`target unavailable`のエラー行には、同じJSONが`state=`として自動で付く
（ハーネスは`[UIAUTO] ERROR`をそのまま出力するので、失敗時の原因がログだけで分かる）。

## プロファイラー（`profile` / `script_profile`）

画面を見なくても、プロファイラーの値とスクリプトAPIのホットパスをテキストで取得できる。

- **`profile`**: FrameProfilerの全区間・GPU時間・カウンター（直近240フレームの平均と最大）、FPS、
  フレーム時間を1行のJSONで`[UIAUTO] OK profile {...}`として出す。
  `{"fps":{..},"frameMs":{..},"sectionsMs":{"<名前>":{"avg":..,"max":..}},"gpuMs":{..},"counters":{"<名前>":{"avg":..,"max":..}}}`
- **`script_profile on|off|reset|dump`**: スクリプトAPI(Luauバインディングと`Instance.setParent`)の
  名前付き区間の累積プロファイラー(`ApiProfiler`)を操作する。`dump`は呼び出し回数・合計時間(`totalMs`、子の区間を含む)・
  自分だけの時間(`selfMs`)・1回あたりの平均(`avgUs`)・最大(`maxMs`)を、`selfMs`の大きい順に上位40件、
  1行のJSONで出す。無効時のコストは分岐1つで、通常起動には影響しない。
  区間名は `get:<プロパティ名>` / `set:<プロパティ名>` / `Instance.new:<クラス名>` / `Vector3.new` /
  `Instance.setParent` とその内訳(`setParent/...`、`BaseCube.onAncestorChanged/...`、`Workspace.registerCube`)など。
  新しい計測点は `ApiProfiler::Scope scope("名前")`（詳細名つきは `Scope("get", key)`）を置くだけでよい。
- **`wait_frames <n>`**: n フレーム待つ(スクリプトの実行や物理の安定を待つため)。
- Playの操作は `click "Editor/Toolbar/Play"` / `click "Editor/Toolbar/Stop"` で行える。

例（非表示モードでシーンをPlayしてスクリプトのホットパスを測る）:

```text
script_profile reset
script_profile on
click "Editor/Toolbar/Play"
wait_frames 60
profile
script_profile dump
click "Editor/Toolbar/Stop"
quit
```

## 非表示モード（`--ui-automation-hidden`）

`--ui-automation`と併用すると、ウィンドウを表示せずに起動する（`GLFW_VISIBLE`を偽にする）。
他の作業や画面に影響せず、フォーカスの奪い合いも起きない。`capture`は非表示でも
フレームバッファの内容を読み取れる。`--ui-automation`なしでは無視される。

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

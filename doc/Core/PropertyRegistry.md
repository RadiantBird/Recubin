# PropertyRegistry

`include/Core/PropertyRegistry.hpp`

クラスごとの「プロパティ定義の単一の表」を保持し、そこから Luau の get/set・YAML 読込/保存・clone・エディター表示・汎用 Undo をすべて駆動する仕組み。1 プロパティ = 1 行の宣言（`field` / `fieldVia` / `method_prop` / `enumProp` / `sig` などのビルダー）にまとめることで、従来クラスごとに手書きしていた各所への同期処理を排除する。

型知識は `PropType` によるディスパッチとして `.cpp` の中央 switch 関数（`valueToLua` / `valueFromLua` / `valueToYaml` / `valueFromYaml`）に集約されており、宣言側（ヘッダのテンプレート）は値を `PropValue`（`variant`）に正規化するだけで良い。

## 移行状況（注意）

`PropertyRegistry::registerClass()` を呼んでいるのは GUI 系（`ScreenGuiObject`, `TextLabel`, `GuiButton` 等）、光源系（`Lighting`, `SpotLight`, `PointLight`, `Sun`, `Moon` 等）、`Humanoid`, `PathfindingService`, `FileRef`, `LiquidCube`, `AppImage`, `ProximityPrompt` など一部のクラスに限られる。**`BaseCube` / `Cube` は移行対象外**で、意図的に `setProperty` 手書き実装を維持している（特殊なプロパティ挙動が多いため）。新規クラス追加時にどちらの方式を採るかは既存の類似クラスに合わせて判断すること。

## 型・列挙

| 型 | 説明 |
|---|---|
| `PropType` | `Float / Int / Bool / String / Vec3 / Vec2 / Color4 / Enum` |
| `PropKind` | `Field`（通常プロパティ） / `Signal`（Luau 読み取り専用のイベント） |
| `PropValue` | `variant<float, int, bool, string, Vector3, Vector2, Color4>` |

## PropertyDesc（1プロパティ = 1行の宣言）

| フィールド | 型 | 説明 |
|---|---|---|
| `name` | `string_view` | プロパティ名（Luau/YAML/エディター共通、`yamlKey` で個別上書き可） |
| `type` / `kind` | `PropType` / `PropKind` | 値の型・種別 |
| `get` / `set` | `function` | 既定の取得/設定（YAML・clone・エディター・Lua 共通） |
| `luaSet` | `function` | 非null なら Lua 書込のみこちらを使う（副作用セッター用、`fieldVia` が設定） |
| `signalGet` | `function<int(lua_State*, Instance*)>` | `kind==Signal` のときの取得（`LuauEngine::pushSignal` を呼ぶ） |
| `enumNames` | `vector<pair<string_view,int>>` | `Enum` 型の文字列⇔値表 |
| `serialize` / `cloneable` / `editable` | `bool` | 各機能への参加可否（既定 true） |
| `noLuaWrite` / `clampOnLuaWrite` | `bool` | Lua からの書込制御 |
| `lo` / `hi` / `step` | `float` | エディタースライダー用レンジ |
| `instanceRefClass` | `string_view` | 非空なら、その `IsA` 型だけを選択可能なInstance参照文字列としてエディターに通知する |

`readOnly()` / `noYaml()` / `noClone()` / `noEditor()` / `omitEmpty()` / `yaml(key)` / `clampLua()` / `luaReadOnly()` で宣言を後置き修飾する。

## 宣言ビルダー

| 関数 | 説明 |
|---|---|
| `field<M>(name, lo, hi, step)` | メンバポインタ `M` から直接読み書きする通常フィールド |
| `instanceRefField<M>(name, targetClass)` | `string`メンバをWorkspace相対パスで保持する型制約付きInstance参照として宣言し、`instanceRefClass`へ対象型を設定する |
| `fieldVia<Field, SetMethod>(name, ...)` | 読みはフィールド直、Lua 書込のみセッターメソッド経由 |
| `method_prop<Getter, Setter>(name, ...)` | get/set 双方メソッド経由（例: `Transparency`, `Sound.Volume`） |
| `enumProp<M>(name, names, yamlAsString)` | enum メンバ。Luau 側は文字列で読み書きする |
| `custom(name, type, get, set)` | 明示的な get/set 関数（物理同期など特殊アクセス用） |
| `sig<M>(name)` | シグナルメンバ（`shared_ptr<RCBNScriptSignal>`）を Luau に公開（読み取り専用） |

## 登録・利用 API

| 関数 | 説明 |
|---|---|
| `registerClass(className, props)` / `registerClass(className, baseClassName, props)` | クラスのスキーマを登録（基底クラス指定可） |
| `registeredClassNames()` | 登録済み全クラス名（dispatch 配線漏れ検出用） |
| `schemaFor(className)` | 自クラス分のみのスキーマ（Lua dispatch 登録用） |
| `collectSchema(className)` | 基底→派生の順に集約したスキーマ（YAML/clone/editor 用） |
| `loadProperty(obj, className, name, yamlNode)` | YAML の1キーを対応プロパティへ書き込む |
| `saveProperties(emitter, obj, className)` | 自クラス分のプロパティを YAML へ出力 |
| `cloneFields(src, dst, className)` | `collectSchema` に従い全プロパティを複製 |
| `applyToDispatch(className, getters, setters)` | `LuauEngine::DispatchTable` / `SetterTable` へゲッター/セッターを登録 |
| `readValue` / `writeValue` | `PropertyDesc` 経由の直接読み書き |

## 依存関係

- `LuauEngine`（`GetterFunc`/`SetterFunc`, `pushSignal`, メタテーブル定数）
- `Instance`, `Vector3`, `Vector2`, `Color4`
- yaml-cpp

## 使われる場所

- 各 Instance 派生クラスのコンストラクタ／静的初期化で `registerClass()` を呼びスキーマを登録する
- `LuauEngine::InitDispatchTable_*` 系が `applyToDispatch()` を呼んで dispatch テーブルへ配線する
- `SceneLoader` が `loadProperty()` を、保存処理が `saveProperties()` を、`Instance::clone()` 系が `cloneFields()` を利用する
- Propertiesの共通Instance pickerは`instanceRefClass`を参照し、対象型の`IsA`検証、Workspace相対パス設定、Pick/Clear、Undo/Redoを共通処理する。`ScreenGuiObject.FontFile`のように条件付き表示する欄も、個別UIから同じ共通pickerを呼び出す

## 置換時の互換性

インスタンス置換のプロパティ移送は、置換元と置換先の継承関係に依存せず、両方の
集約schemaから同名・同型のフィールドだけを対象にする。型が異なるフィールドは
移送せず、子インスタンスのidentity、typed参照、Undo/Redoの復元契約を維持する。
Highlightも`applyToDispatch()`へ接続し、Luauのread/writeとclampを通常のregistry
経路で扱う。

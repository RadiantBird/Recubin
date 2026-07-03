# LuauEngine

`include/Core/LuauEngine.hpp`

Luau スクリプティングランタイム。C++ の Instance 階層を Luau 側に公開し、コルーチンベースの `wait()` に対応する。

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `L` | `lua_State*` | Luau VM |
| `workspace` | `Workspace*` | スクリプトが参照するワークスペース |
| `currentScript` | `static Script*` | 現在実行中のスクリプト |
| `DispatchTable` | `static unordered_map<string_view, unordered_map<string_view, GetterFunc>>` | クラス名 → プロパティ名 → ゲッター関数 |
| `SetterTable` | `static unordered_map<string_view, unordered_map<string_view, SetterFunc>>` | クラス名 → プロパティ名 → セッター関数 |

## 型エイリアス

```cpp
using GetterFunc = function<int(lua_State*, Instance*)>;
using SetterFunc = function<int(lua_State*, Instance*)>;
```

## メソッド

| メソッド | 説明 |
|---|---|
| `setBindings(instance)` | Instance を Luau のユーザーデータとしてスタックに積む |
| `setGlobalInstance(name, instance)` | Luau グローバル変数として Instance を公開 |
| `execute(script)` | Script を 1 回実行（コルーチンとして起動） |
| `executeWorkspaceScripts()` | Workspace 内の有効な Script をすべて実行 |
| `setWorkspace(ws)` | Workspace を設定 |
| `update(deltaTime)` | 待機中コルーチンを再開 |

## DispatchTable の構造

```
DispatchTable["BaseCube"]["Position"] = getter lambda
DispatchTable["BaseCube"]["Color"]    = getter lambda
DispatchTable["Decal"]["TextureID"]   = getter lambda
...
```

`__index` / `__newindex` メタメソッドがクラス名で二段階ルックアップして適切な getter/setter を呼ぶ。

## Luau バインディング一覧

| Luau 側 | C++ 側 |
|---|---|
| `instance.Name` | `Instance::Name` |
| `instance:FindChild(name)` | `Instance::getChild()` |
| `cube.Position` | `BaseCube::cframe.Position` |
| `cube.Color` | `BaseCube::Color` |
| `decal.TextureID` | `Decal::TextureID` |
| `game:add(className)` | `LuauEngine::global_add` |
| `print(msg)` | `Logger` 経由でコンソールへ |
| `wait(seconds)` | コルーチンを yield |

## wait() の仕組み

```
wait(2.0) 呼び出し
  → Script::Sleeping = true, SleepRemaining = 2.0
  → lua_yield()

毎フレーム update(dt):
  → 全 Sleeping スクリプトの SleepRemaining -= dt
  → 0 以下なら lua_resume() でコルーチン再開
```

## 実装ファイルの分割

`LuauEngine` クラス自体は `src/Core/LuauEngine.cpp` に加え、以下の翻訳単位に実装が分割されている（別クラスではない）。

| ファイル | 内容 |
|---|---|
| `src/Core/LuauEngine_Dispatch.cpp` | `InitDispatchTable_Base/World/Physics/Misc/GUI` / `InitSetterTable_*` の実装。`DispatchTable`/`SetterTable` へクラス別プロパティ getter/setter を登録する |
| `src/Core/LuauEngine_Math.cpp` | `Vector3` / `Vector2` / `Color4` / `Quaternion` / `CFrame` の Luau メタメソッド（`__index`, `__add`, `__mul` 等）とコンストラクタ実装 |

## 依存関係

- Luau SDK
- `Instance`, `BaseCube`, `Script`, `Workspace`, `Vector3`, `Vector2`, `Quaternion`, `CFrame`, `Color4`, `Logger`
- `PropertyRegistry`（`applyToDispatch()` 経由で PropertyRegistry 管理クラスの getter/setter が `DispatchTable`/`SetterTable` に配線される）
- `RCBNScriptSignal`（Signal/Connection バインディング）

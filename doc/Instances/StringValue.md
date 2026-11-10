# StringValue

`include/Instances/StringValue.hpp`

`ValueBase` を継承し、任意の `std::string` を `Value` に保持する値インスタンス。既定値は空文字列で、YAML と Luau の `Value` にそのまま対応する。`Value` が変更されると `Changed` が新しい文字列を1引数で発火する。

## 継承

`Instance` → `ValueBase` → `StringValue`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `Value` | `std::string` | 保持する文字列 |

## Luau

```lua
local value = Instance.new("StringValue")
value.Value = "hello"
value.Changed:Connect(function(newValue)
    print(newValue)
end)
```

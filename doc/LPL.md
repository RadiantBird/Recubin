# Luar Programming Language

## 概要
Luau言語から派生し、ついにオブジェクト指向・テーブルのディープコピーを実現。

## const
Luau 0.731と同じconst宣言を使用できる。

```luau
const VERSION: string = "1.0"
const WIDTH, HEIGHT = 1280, 720

const function area(): number
    return WIDTH * HEIGHT
end
```

- const宣言には初期値が必要であり、宣言後の再代入は禁止される。
- constが参照するテーブルなどの値自体は不変化されない。
- 内側のスコープで同じ名前を宣言するシャドーイングは可能。
- `const`は文脈依存キーワードであり、フィールド名などでは通常の識別子として使用できる。

## OOP
```luau
class Lua is
    -- public, privateブロックの外にあるメソッド、変数はデフォルトでprivate
    private is
        Year = 0
        function explode()
            print("Boom!!")
        end
    end

    public is
        static function new(year: number) -- コンストラクタ(予約語)
            self.Year = year
        end

        static function panic() -- static関数(インスタンスからは呼べない)
            print("I'm not a crab!!!")
        end

        function greet()
            print(`Hello from {self.Year}!`)
        end

        function operator==(AnotherRock: Lua)
            return false -- Luaは純血です
        end

        function free() -- デストラクタ(予約語)
            print("Goodbye world")
            -- self = nil
        end
    end
end

local L = Lua.new(1993)
local L2 = Lua.new(2006)
L.greet()
print(L == L2) -- false
Lua.panic()
-- L.explode() -- コンパイルエラー(privateへのアクセス)
-- L.Year = 2026 -- コンパイルエラー(privateへのアクセス)
L.free() --[[
このとき、参照の破棄、イベントの切断、ループ停止（並行処理など）などが行われる。
メモリを解放はしない。
]]
```

### コンパイル後(例)
```luau
-- クラステーブル
local Lua = {}
Lua.__index = Lua

-- メタメソッド（operator==）
Lua.__eq = function(self, AnotherRock)
    return false
end

-- ========================
-- private領域（外から触れないようローカル化）
-- ========================

local function explode(self)
    print("Boom!!")
end

-- ========================
-- public static関数
-- ========================

function Lua.new(year)
    local self = setmetatable({}, Lua)
    self.Year = 0 -- 初期値

    -- コンストラクタ処理
    self.Year = year

    return self
end

function Lua.panic()
    print("I'm not a crab!!!")
end

-- ========================
-- public インスタンスメソッド
-- ========================

function Lua.greet(self)
    print(`Hello from {self.Year}!`)
end

function Lua.free(self)
    print("Goodbye world")
    -- クリーンアップ処理（ユーザー定義）
end

-- ========================
-- 使用コード
-- ========================

local L = Lua.new(1993)
local L2 = Lua.new(2006)

L:greet()

print(L == L2) -- false

Lua.panic()

-- L:explode() -- アクセス不可（ローカル関数なので）

-- L.Year = 2026 -- ⚠ Luau的には防げない（仕様的にはエラーだが実行時は通る）

L:free()
```

### 継承
```Luau
class Language is abstract
    year = 1901
    public is
        function explain() abstract
    end
end

class Binary is Language
    year = 1930
    public is
        function explain() override
            print("made by 0 and 1")
        end
    end
end

class ASM is Binary
    year = 1940
    public is
        -- 親クラスと同じ名前のメソッドをoverrideなしで書くことは禁止されている
        -- function explain()
        --     print("made with ASCII")
        -- end

        function explain() override
            print("made with ASCII")
        end

        function assembly() final
            print("B0 0A >>> MOV AL, 10")
        end
    end
end

class C is ASM
    year = 1972
    public is
        function explain() override
            super.explain() -- ASM.explain()
            print("made with English")
        end

        -- finalによって禁止されている
        -- function assembly() final
        --     print("B0 0A >>> char A = 10;")
        -- end
    end
end
```
#### 継承の仕様
- overrideキーワードを使用する場合、
  親クラスに同名のメソッドが存在しなければならない。

- 親クラスに同名のメソッドが存在する場合、
  **overrideキーワードなしでの再定義は禁止される。**

- overrideはメソッドにのみ適用される。
  フィールドに対して使用することはできない。

- フィールドは親クラスのものを自由に上書きできる。
  overrideキーワードは不要である。


- overrideするメソッドは、
  親クラスのメソッドとシグネチャ（名前・引数・戻り値）が
  **完全一致していなければならない。**

- abstractクラスはインスタンス化できない。

- abstractメソッドを持つクラスは、abstractでなければならない。

- abstractメソッドを実装する場合もoverrideキーワードを必須とする。

- finalキーワードはメソッドにのみ適用可能であり、
  子クラスでオーバーライドすることを禁止する。

- superは直前の親クラスに定義されたメソッドを呼び出す。
  多段継承の場合、親クラス側でsuperが呼ばれない限り、それ以上は遡らない。

### 備考

* コロンとドットは使い分けません。なぜか？コンパイラが自動でselfを使うように変換します。
  その方が理不尽でわかりづらいエラー（引数ずれ）とかが起きませんよね？

* static以外のメソッドは常にselfを受け取る。
  しかし、**演算子オーバーロード関数はselfが必要なため、staticとして宣言はできない。**

* newは特別なstatic関数であり、以下のように変換される：

  1. 新しいテーブルを生成する
  2. クラスのメタテーブルを設定する
  3. そのインスタンスをselfとして関数を実行する
  4. selfを返す

* クラス直下およびフィールド定義は、インスタンス生成時に各インスタンスへコピーされる。

* 子クラスは自動で親のnewを継承する。

* operator関数はインスタンスメソッドとしてのみ定義可能である。
  staticとして宣言することはできない。(前述の通り)

* 演算子オーバーロード関数で想定していた型と異なる引数が渡された場合、
  false, nil, 0のいずれかを返すのが望ましい。

* public / privateによるアクセス制御はコンパイル時にのみ適用される。
  実行時には追加のオーバーヘッドは発生しない（ゼロオーバーヘッド抽象化）。

* privateメンバは実行時には通常のフィールドとしてインスタンスに格納されるが、
  コンパイラによってクラス外からのアクセスは禁止される。

* これらの制約はLuarコンパイラによって保証されるものであり、
  **生成されたLuaコードや手動で記述されたLuaコードには適用されない。**

## テーブルコピー
```luau
local t = {
    1,
    2,
    3,
    {
        "a",
        "b",
        "c"
    }
}

local t2 = table.deepcopy(t)
print(t2) --[[
{
    1,
    2,
    3,
    {
        "a",
        "b",
        "c"
    }
}
]]
```

### 備考
- ポインタはコピーせずに参照のまま(外部オブジェクトとか)
    **metatableはコピーされない**
    userdata / function / thread は参照
- 循環は同じ参照にし、一度追加したものはパスする。
- この関数はLuar標準に組み込まれる。(Luauのtableライブラリをmodする)
- もしくは、展開して貼り付け(実用性というか)

## モジュール(import)
`import`はコンパイル時の名前解決だけを行う。実行時にmoduleを読み込む機能ではない。

### 定義ファイル
`import qaz`と書いた`.luar`ファイルと同じディレクトリに、`qaz.luard`を配置する。

```luau
-- main.luar
import qaz

print(wsx)       -- qaz.wsxへ変換
print(workspace) -- global宣言なので変換しない
```

```luau
-- qaz.luard
declare wsx: string
declare global workspace: workspace
```

`.luard`に記述できるものは、コメント、空行、`declare name: Type`、
`declare global name: Type`だけである。`.luar`本体に`declare`を書くことはできない。

### 名前の衝突
```luau
-- main.luar
import hoge
import foo

print(bar)      -- コンパイルエラー: hoge.barとfoo.barのどちらか曖昧
print(hoge.bar) -- OK
```

```luau
-- hoge.luard
declare bar: string
```

```luau
-- foo.luard
declare bar: number
```

### 仕様
- `.luard`はimport元と同じディレクトリだけから探索する。
- importしたmoduleの非global宣言に一致する、ソース内で束縛されていない未修飾名は
  `[module名].[フィールド名]`へ変換する。
- local、const、関数引数、ループ変数、関数名、クラス名はmodule宣言より優先される。
- `declare global`で宣言された名前と、どのmoduleにも宣言されていない名前は未修飾のままにする。
- 同じ名前が複数moduleに存在しても、その未修飾名が実際に使われなければエラーにしない。
  実際に使われた場合だけ曖昧な参照としてコンパイルエラーにする。
- `hoge.bar`のような修飾済み参照は曖昧性の影響を受けない。
- moduleは実行時にテーブルとして提供されている必要がある。
- 実際の読み込みはC/C++バインドや`require`など、利用するLuau環境が行う。
  Luarコンパイラはmoduleのロード処理を生成しない。
- 定義ファイルの欠落、不正な記述、同一ファイル内の重複宣言、同じmoduleの重複importは
  ファイル名と行番号を含むコンパイルエラーになる。

## 処理フロー
`ソース-[Luarコンパイラ]->Luauソース-[Luauコンパイラ]->Luauバイトコード->LuauVM実行`
もしくは、Lua変換(試験的):
`ソース-[Luarコンパイラ]->Luaソース-[Luaコンパイラ(インタプリタ?)]->LuaVM実行`
### あとがき
mixinも追加予定(今回は実装しない)
mixinってなんですか？多分不要では

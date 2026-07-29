-- table.deepcopy のテスト

local function check(label, cond)
    if cond then
        print("PASS: " .. label)
    else
        print("FAIL: " .. label)
    end
end

-- ── 基本: 数値・文字列のコピー ───────────────────────────────
local t1 = { 1, 2, 3, name = "Luar" }
local c1 = table.deepcopy(t1)

check("basic values copied",    c1[1] == 1 and c1[2] == 2 and c1[3] == 3)
check("basic string copied",    c1.name == "Luar")
check("basic is new table",     c1 ~= t1)

c1[1] = 99
check("mutation isolated",      t1[1] == 1)

-- ── ネスト: 深いテーブルのコピー ─────────────────────────────
local t2 = { 1, 2, 3, { "a", "b", "c" } }
local c2 = table.deepcopy(t2)

check("nested values copied",   c2[4][1] == "a" and c2[4][2] == "b" and c2[4][3] == "c")
check("nested is new table",    c2[4] ~= t2[4])

c2[4][1] = "X"
check("nested mutation isolated", t2[4][1] == "a")

-- ── 循環参照 ─────────────────────────────────────────────────
local t3 = { value = 42 }
t3.self = t3
local c3 = table.deepcopy(t3)

check("circular value copied",  c3.value == 42)
check("circular is new table",  c3 ~= t3)
check("circular self-ref",      c3.self == c3)

-- ── function / userdata / thread は参照のまま ────────────────
local fn = function() return 1 end
local t4 = { fn = fn }
local c4 = table.deepcopy(t4)

check("function stays as reference", c4.fn == fn)

-- ── メタテーブルはコピーされない ─────────────────────────────
local mt = { __index = function(_, k) return k end }
local t5 = setmetatable({ x = 10 }, mt)
local c5 = table.deepcopy(t5)

check("field copied",           c5.x == 10)
check("metatable not copied",   getmetatable(c5) == nil)

print("done")

-- test_script_features: require / ModuleScript / task モジュールの回帰テスト
-- RecubinTest.exe が print の [PASS]/[FAIL] を数えて終了コードを決める

-- ===== require / ModuleScript =====
local M1 = require(script.MathModule)
local M2 = require(script.MathModule)
if M1 ~= nil and M1 == M2 then
    print("[PASS] require returns the same cached value")
else
    print("[FAIL] require cache broken")
end

if M1.add(2, 3) == 5 then
    print("[PASS] module function works")
else
    print("[FAIL] module function broken")
end

local okCyclic = pcall(function()
    return require(script.CyclicModule)
end)
if not okCyclic then
    print("[PASS] cyclic require detected")
else
    print("[FAIL] cyclic require was not detected")
end

local okNoReturn = pcall(function()
    return require(script.NoReturnModule)
end)
if not okNoReturn then
    print("[PASS] module without return value errors")
else
    print("[FAIL] module without return value did not error")
end

-- ===== task モジュール =====
local flags = {}

task.spawn(function(x)
    flags.spawnRan = x
end, 42)
if flags.spawnRan == 42 then
    print("[PASS] task.spawn runs immediately with arguments")
else
    print("[FAIL] task.spawn did not run immediately")
end

task.spawn(function()
    task.wait(0.1)
    flags.spawnWaitDone = true
end)
if flags.spawnWaitDone == nil then
    print("[PASS] task.spawn yields at task.wait")
else
    print("[FAIL] task.spawn did not yield at task.wait")
end

flags.delayRan = false
task.delay(0.1, function()
    flags.delayRan = true
end)
if flags.delayRan == false then
    print("[PASS] task.delay does not run immediately")
else
    print("[FAIL] task.delay ran immediately")
end

-- wait() 引数なし(次フレーム再開)
wait()
print("[PASS] wait() without arguments resumed")

-- タスクの完了を待って結果を確認する
wait(0.5)
if flags.spawnWaitDone == true then
    print("[PASS] task.spawn resumed after task.wait")
else
    print("[FAIL] task.spawn was not resumed after task.wait")
end
if flags.delayRan == true then
    print("[PASS] task.delay fired after the delay")
else
    print("[FAIL] task.delay did not fire")
end

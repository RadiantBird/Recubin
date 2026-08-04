-- ============================================================
--  Greeter.luau  ―  看板に取り付けられたスクリプト
--
--  検証する機能:
--    * script.Parent（自分が取り付けられたインスタンスを参照）
--    * System.Heartbeat:Once（起動後に一度だけ実行）
-- ============================================================

local sign = script.Parent
print("[Greeter] attached to: " .. (sign and sign.Name or "?"))

System.Heartbeat:Once(function()
    print("[Greeter] Welcome to TestGame! Collect all the coins, then open the door.")
end)

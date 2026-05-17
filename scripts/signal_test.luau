-- ============================================================
--  Signal / Connection テストスクリプト
--  期待出力:
--    [Connect]  tick #1 ~ #3 → Disconnected
--    [Once]     1回だけ表示
--    [Until]    tick #1 ~ #2 → Event fired
--    [Touched]  Dropper が着地したとき表示
-- ============================================================

print("=== Signal Test Start ===")

-- ---- Test 1: Heartbeat:Connect + Disconnect ----
local tickCount = 0
local conn
conn = System.Heartbeat:Connect(function(dt)
    tickCount += 1
    print("[Connect] tick #" .. tickCount .. "  dt=" .. string.format("%.4f", dt))
    if tickCount >= 3 then
        conn:Disconnect()
        print("[Connect] Disconnected after 3 ticks ✓")
    end
end)

-- ---- Test 2: Heartbeat:Once ----
System.Heartbeat:Once(function(dt)
    print("[Once]    fired (dt=" .. string.format("%.4f", dt) .. ") ✓")
end)

-- ---- Test 3: Heartbeat:Until ----
local stopEvent = Instance.new("Event")
local untilCount = 0
System.Heartbeat:Until(stopEvent, function(dt)
    untilCount += 1
    print("[Until]   tick #" .. untilCount)
    if untilCount >= 2 then
        stopEvent:Fire()
        print("[Until]   Event:Fire() called – no more ticks ✓")
    end
end)

-- ---- Test 4: Touched:Connect ----
local dropper = workspace:FindChild("Dropper")
if dropper then
    dropper.Touched:Once(function()
        print("[Touched:Once]   first touch ✓")
    end)
    local hitCount = 0
    dropper.Touched:Connect(function()
        hitCount += 1
        print("[Touched:Connect] hit #" .. hitCount)
    end)
else
    print("[WARN] 'Dropper' not found – Touched test skipped")
end

print("=== Listeners registered, waiting for events... ===")

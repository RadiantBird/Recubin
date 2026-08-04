-- ============================================================
--  DayNight.luau  ―  ライティングをゆっくり回転させる昼夜サイクル
--
--  検証する機能:
--    * Workspace > Lighting を FindChild で取得
--    * Lighting.Direction の書き込み（Vector3）
--    * wait() によるループ（コルーチン yield）
-- ============================================================

local lighting = workspace:FindChild("Lighting")
if not lighting then
    print("[DayNight] Lighting が見つかりません")
    return
end

local t = 0.0
while true do
    t += 0.02
    -- 太陽を水平に1周。y成分は下向き固定＝斜め上から照らす
    lighting.Direction = Vector3.new(math.cos(t), -0.7, math.sin(t))
    wait(0.05)
end

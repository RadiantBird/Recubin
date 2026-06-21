-- ライティングを360°回転させ、地形（特に三角柱ランプの斜面）の
-- 法線・陰影が全方向から正常に描画されることを確認するテスト。
-- 使い方: Workspace に Script を1つ作り、この Path を指定して Play。

local lighting = workspace:FindChild("Lighting")
if not lighting then
    print("Lighting が見つかりません")
    return
end

local t = 0.0
while true do
    t = t + 0.04
    -- 太陽を水平方向に1周させる（yは下向き成分を固定＝斜め上から照らす）
    lighting.Direction = Vector3.new(math.cos(t), -0.7, math.sin(t))
    wait(0.03)
end

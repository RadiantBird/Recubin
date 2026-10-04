-- Motor再現シーン用の診断。毎秒、車の位置・車輪軸のずれ・アタッチメント分離を出力する。
-- 正常なら attachment separation はほぼ0、chassis は Y≈3.25 付近で走り続ける。
local chassis = workspace.CarChassis
local startPos = chassis.WorldPosition
local rotor = workspace.HingeRotor
local hingeBase = workspace.HingeBase
print("[MotorRepro] car start", startPos)

while true do
    wait(1)
    local maxSep = 0
    for i = 0, 3 do
        local a = chassis["CarChassisAttachment" .. i].WorldPosition
        local b = workspace["CarWheel" .. i]["CarWheelAttachment" .. i].WorldPosition
        local d = a - b
        local sep = math.sqrt(d.x * d.x + d.y * d.y + d.z * d.z)
        if sep > maxSep then maxSep = sep end
    end
    local p = chassis.WorldPosition
    -- ヒンジ: Axis=(0,1,0) なら Rotor は水平を保つ(長軸ベクトルのyが0、中心yが6のまま)。
    local rp = rotor.WorldPosition
    local look = rotor.WorldCFrame.LookVector
    local rel = rp - hingeBase.WorldPosition
    print("[MotorRepro] hinge rotor pos", rp, "rel", rel, "LookVector", look)
    print("[MotorRepro] chassis", p, "max attachment separation", maxSep)
end

-- CameraProgramTest
-- ControlMode::Program のテスト:
-- User.ControlMode を "Program" に切り替え、毎フレーム User.CameraCFrame を
-- 書き換えてカメラを原点付近の周りに周回させる。マウス/キーボードのカメラ操作が
-- 効かなくなり、代わりにLuauから書いたCFrameが反映されることを目視確認する。

local target = Vector3.new(0, -20, 0)
local radius = 15
local height = 8
local angularSpeed = 0.5 -- rad/s

User.ControlMode = "Program"

local t = 0

System.Heartbeat:Connect(function(dt)
    t = t + dt
    local theta = t * angularSpeed -- 周回角(ラジアン)

    local pos = Vector3.new(
        target.x + math.cos(theta) * radius,
        target.y + height,
        target.z + math.sin(theta) * radius
    )

    User.CameraCFrame = CFrame.lookAt(pos, target)
end)

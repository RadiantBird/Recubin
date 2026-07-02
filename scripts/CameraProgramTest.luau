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

-- radius/height は周回中一定なので、見下ろす角度(pitch)も一定。
-- radius は常に正なので atan(-height/radius) だけで象限を気にせず求まる。
local pitchDeg = math.deg(math.atan(-height / radius))
local t = 0

System.Heartbeat:Connect(function(dt)
    t = t + dt
    local theta = t * angularSpeed -- 周回角(ラジアン)

    local pos = Vector3.new(
        target.x + math.cos(theta) * radius,
        target.y + height,
        target.z + math.sin(theta) * radius
    )

    -- 周回位置から原点方向を向くyaw。 forward=(-sin(yaw),0,-cos(yaw)) と
    -- pos=(cos(theta),*,sin(theta))*radius の関係から yaw = pi/2 - theta。
    local yawDeg = math.deg(math.pi / 2 - theta)
    local rot = Quaternion.fromAxisAngle(Vector3.new(0, 1, 0), yawDeg)
              * Quaternion.fromAxisAngle(Vector3.new(1, 0, 0), pitchDeg)

    User.CameraCFrame = CFrame.new(pos, rot)
end)

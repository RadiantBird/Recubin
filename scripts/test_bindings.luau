-- Luau Binding テストスクリプト
-- 各バインディングを順番に確認し print で結果を出力する

local PASS = "[PASS]"
local FAIL = "[FAIL]"

local function check(label, cond)
    print((cond and PASS or FAIL) .. " " .. label)
end

-- ===== グローバル変数 =====
print("--- Globals ---")
check("workspace != nil", workspace ~= nil)
check("script != nil",    script ~= nil)
check("workspace.Name == 'Workspace'", workspace.Name == "Workspace")
check("script.Name == 'TestBindings'", script.Name == "TestBindings")

-- ===== Instance メソッド =====
print("--- Instance ---")
local cube = workspace:FindChild("TestCube")
check("FindChild TestCube", cube ~= nil)

check("Parent: cube.Parent == workspace", cube.Parent ~= nil)
check("IsA: cube:IsA('BaseCube')", cube:IsA("BaseCube"))
check("IsA: cube:IsA('Cube')",     cube:IsA("Cube"))
check("IsA: cube:IsA('Sound') == false", not cube:IsA("Sound"))

local children = workspace:GetChildren()
check("GetChildren returns table", type(children) == "table")
check("GetChildren has items", #children > 0)
print("  GetChildren count: " .. #children)

-- ===== BaseCube プロパティ =====
print("--- BaseCube ---")
local pos = cube.Position
check("Position getter is not nil", pos ~= nil)
check("Position.x is number", type(pos.x) == "number")
print("  Position: " .. tostring(pos))

local col = cube.Color
check("Color getter is not nil", col ~= nil)
check("Color.r is number", type(col.r) == "number")
print("  Color: " .. tostring(col))

local origSize = cube.Size
check("Size getter is not nil", origSize ~= nil)
cube.Size = Vector3.new(3, 3, 3)
check("Size setter (x)", cube.Size.x == 3)
cube.Size = origSize

check("Anchored getter == false", cube.Anchored == false)
cube.Anchored = true
check("Anchored setter = true", cube.Anchored == true)
cube.Anchored = false

check("CanCollide getter == true", cube.CanCollide == true)
cube.CanCollide = false
check("CanCollide setter = false", cube.CanCollide == false)
cube.CanCollide = true

local vel = cube.Velocity
check("Velocity getter is not nil", vel ~= nil)
check("Velocity.y is number", type(vel.y) == "number")
print("  Velocity: " .. tostring(vel))

-- ===== Workspace.Gravity =====
print("--- Workspace.Gravity ---")
local grav = workspace.Gravity
check("Gravity getter is not nil", grav ~= nil)
check("Gravity.y < 0 (default)", grav.y < 0)
print("  Gravity: " .. tostring(grav))
workspace.Gravity = Vector3.new(0, -20, 0)
check("Gravity setter (y == -20)", workspace.Gravity.y == -20)
workspace.Gravity = Vector3.new(0, -9.81, 0)

-- ===== Motor (値の読み書きのみ、物理パラメータは変えない) =====
print("--- Motor ---")
local motor = workspace:FindChild("TestMotor")
check("FindChild TestMotor", motor ~= nil)
if motor ~= nil then
    check("DriveVelocity getter is number", type(motor.DriveVelocity) == "number")
    check("MaxForce getter is number", type(motor.MaxForce) == "number")
    print("  DriveVelocity: " .. motor.DriveVelocity)
    print("  MaxForce: " .. motor.MaxForce)
    local origDV = motor.DriveVelocity
    local origMF = motor.MaxForce
    motor.DriveVelocity = 3.0
    check("DriveVelocity setter = 3", motor.DriveVelocity == 3)
    motor.MaxForce = 500
    check("MaxForce setter = 500", motor.MaxForce == 500)
    motor.DriveVelocity = origDV
    motor.MaxForce = origMF
end

-- ===== Sound =====
print("--- Sound ---")
local radio = workspace:FindChild("Radio")
local snd = nil
if radio ~= nil then
    snd = radio:FindChild("BGM")
end
check("FindChild Sound", snd ~= nil)
if snd ~= nil then
    check("IsPlaying getter (initially false)", snd.IsPlaying == false)
    check("Looped getter is bool", type(snd.Looped) == "boolean")
    check("Volume getter is number", type(snd.Volume) == "number")
    print("  Volume: " .. snd.Volume)

    snd.Looped = true
    check("Looped setter = true", snd.Looped == true)

    -- フルボリュームで再生 (3D 減衰に埋もれないよう Volume は変えない)
    print("  [Sound] Playing for 3 seconds... (サウンドが聞こえますか？)")
    snd:Play()
    wait(1.0)
    check("IsPlaying at 1s", snd.IsPlaying == true)
    wait(2.0)
    check("IsPlaying at 3s", snd.IsPlaying == true)
    snd:Stop()
    check("IsPlaying after Stop()", snd.IsPlaying == false)
end

-- ===== Lighting =====
print("--- Lighting ---")
local lighting = nil
local root = workspace.Parent
if root ~= nil then
    lighting = root:FindChild("Lighting")
end
check("FindChild Lighting", lighting ~= nil)
if lighting ~= nil then
    check("Brightness getter is number", type(lighting.Brightness) == "number")
    print("  Brightness: " .. lighting.Brightness)
    lighting.Brightness = 0.5
    check("Brightness setter = 0.5", lighting.Brightness == 0.5)
    lighting.Brightness = 1.0
end

-- ===== Destroy =====
print("--- Destroy ---")
local dummy = workspace:FindChild("Dummy")
check("FindChild Dummy", dummy ~= nil)
if dummy ~= nil then
    dummy:Destroy()
    local gone = workspace:FindChild("Dummy")
    check("Destroy: Dummy removed", gone == nil)
end

print("--- テスト完了 ---")

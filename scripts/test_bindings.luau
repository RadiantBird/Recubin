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

-- ===== Vector3 / Vector2 / Color4 演算子 =====
print("--- Vector3 Arithmetic ---")
local v3a = Vector3.new(0, 1, 2)
local v3b = Vector3.new(1, 2, 3)
check("Vector3 __add", tostring(v3a + v3b) == tostring(Vector3.new(1, 3, 5)))
check("Vector3 __sub", tostring(v3a - v3b) == tostring(Vector3.new(-1, -1, -1)))
local negV3a = -v3a
check("Vector3 __unm", negV3a.x == 0 and negV3a.y == -1 and negV3a.z == -2)
check("Vector3 __mul (scalar, vec*n)", tostring(v3a * 2) == tostring(Vector3.new(0, 2, 4)))
check("Vector3 __mul (scalar, n*vec)", tostring(2 * v3a) == tostring(Vector3.new(0, 2, 4)))
check("Vector3 __mul (componentwise)", tostring(v3a * v3b) == tostring(Vector3.new(0, 2, 6)))
check("Vector3 __div (scalar)", tostring(v3b / 2) == tostring(Vector3.new(0.5, 1, 1.5)))
check("Vector3 __eq", Vector3.new(0, 1, 2) == v3a)

print("--- Vector2 Arithmetic ---")
local v2a = Vector2.new(1, 2)
local v2b = Vector2.new(3, 4)
check("Vector2 __add", tostring(v2a + v2b) == tostring(Vector2.new(4, 6)))
check("Vector2 __sub", tostring(v2a - v2b) == tostring(Vector2.new(-2, -2)))
check("Vector2 __unm", tostring(-v2a) == tostring(Vector2.new(-1, -2)))
check("Vector2 __mul (scalar)", tostring(v2a * 2) == tostring(Vector2.new(2, 4)))
check("Vector2 __mul (componentwise)", tostring(v2a * v2b) == tostring(Vector2.new(3, 8)))
check("Vector2 __div (scalar)", tostring(v2b / 2) == tostring(Vector2.new(1.5, 2)))
check("Vector2 __eq", Vector2.new(1, 2) == v2a)

print("--- Color4 Arithmetic ---")
local c4a = Color4.new(0.2, 0.2, 0.2, 1)
local c4b = Color4.new(0.1, 0.1, 0.1, 0)
check("Color4 __add", tostring(c4a + c4b) == tostring(Color4.new(0.3, 0.3, 0.3, 1)))
check("Color4 __sub", tostring(c4a - c4b) == tostring(Color4.new(0.1, 0.1, 0.1, 1)))
check("Color4 __mul (scalar)", tostring(c4a * 2) == tostring(Color4.new(0.4, 0.4, 0.4, 2)))
check("Color4 __div (scalar)", tostring(c4a / 2) == tostring(Color4.new(0.1, 0.1, 0.1, 0.5)))
check("Color4 __eq", Color4.new(0.2, 0.2, 0.2, 1) == c4a)
local c4c = Color4.fromRGBA(10, 20, 30, 40)
check("Color4.fromRGBA", tostring(c4c) == tostring(Color4.new(0.0392156878, 0.0784313757, 0.117647059, 0.156862745)))

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
workspace.Gravity = Vector3.new(0, -196.2, 0)

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
local lighting = workspace:FindChild("Lighting")
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

-- ===== Quaternion / CFrame 値型 =====
print("--- Quaternion / CFrame ---")
local qid = Quaternion.new()
check("Quaternion.new() is identity (w==1)", qid.w == 1 and qid.x == 0)
local qe = Quaternion.fromEuler(Vector3.new(0, 90, 0))
check("Quaternion.fromEuler is not nil", qe ~= nil)
check("Quaternion fields are numbers", type(qe.w) == "number" and type(qe.y) == "number")
local euler = qe:toEuler()
check("Quaternion:toEuler returns Vector3", type(euler.y) == "number")
local qax = Quaternion.fromAxisAngle(Vector3.new(0, 1, 0), 90)
check("Quaternion.fromAxisAngle is not nil", qax ~= nil)

local cf = CFrame.new(Vector3.new(1, 2, 3), qe)
check("CFrame.Position.x == 1", cf.Position.x == 1)
check("CFrame.Rotation is not nil", cf.Rotation ~= nil)
local cfWorld = cf * Vector3.new(0, 0, 0)
check("CFrame * Vector3 returns Vector3", type(cfWorld.x) == "number")

local lrId = Quaternion.LookRotation(Vector3.new(0, 0, -1))
check("LookRotation(-Z) is identity", math.abs(lrId.w - 1) < 0.001)
local lcf = CFrame.lookAt(Vector3.new(0, 0, 0), Vector3.new(5, 0, 0))
check("lookAt Position is eye", lcf.Position.x == 0)
local fwd = lcf.Rotation * Vector3.new(0, 0, -1)
check("lookAt faces target (+X)", math.abs(fwd.x - 1) < 0.001 and math.abs(fwd.y) < 0.001 and math.abs(fwd.z) < 0.001)
local ok = pcall(function() return CFrame.new(Vector3.new(0, 0, 0), Vector3.new(1, 2, 3)) end)
check("CFrame.new(pos, Vector3) errors", ok == false)

-- ===== Spatial 透過トランスフォーム（Rotation / CFrame on BaseCube） =====
print("--- Spatial transform ---")
local origRot = cube.Rotation
check("BaseCube.Rotation getter is not nil", origRot ~= nil)
cube.Rotation = Quaternion.fromAxisAngle(Vector3.new(0, 1, 0), 45)
check("BaseCube.Rotation setter applied", math.abs(cube.Rotation.y) > 0.0001)
cube.Rotation = origRot

local origCF = cube.CFrame
check("BaseCube.CFrame getter is not nil", origCF ~= nil)
check("WorldPosition getter is not nil", cube.WorldPosition ~= nil)

-- ===== BaseCube 追加プロパティ（純レンダリングフラグ） =====
print("--- BaseCube render flags ---")
check("CastShadow getter is bool", type(cube.CastShadow) == "boolean")
cube.CastShadow = false
check("CastShadow setter = false", cube.CastShadow == false)
cube.CastShadow = true
check("Unlit getter is bool", type(cube.Unlit) == "boolean")
check("TextureScale getter is number", type(cube.TextureScale) == "number")

-- ===== Motor.Axis =====
if motor ~= nil then
    print("--- Motor.Axis ---")
    check("Axis getter is not nil", motor.Axis ~= nil)
    local origAxis = motor.Axis
    motor.Axis = Vector3.new(0, 1, 0)
    check("Axis setter (y == 1)", motor.Axis.y == 1)
    motor.Axis = origAxis
end

-- ===== Instance:Clone() / Parent setter =====
print("--- Clone / reparent ---")
local clone = cube:Clone()
check("Clone returns instance", clone ~= nil)
check("Clone has no parent yet", clone.Parent == nil)
clone.Name = "TestCubeClone"
clone.Parent = workspace
check("Clone parented to workspace", workspace:FindChild("TestCubeClone") ~= nil)
clone:Destroy()

-- ===== M-3 クランプ（不正値が丸められる） =====
if lighting ~= nil then
    print("--- clamp ---")
    local origB = lighting.Brightness
    lighting.Brightness = -5
    check("Brightness clamped to >= 0", lighting.Brightness >= 0)
    lighting.Brightness = origB
end

-- ===== FileRef（アセット参照） =====
print("--- FileRef ---")
local audioRef = workspace:FindChild("BGMFileRef")   -- エディタで音声を指定した FileRef
local imageRef = workspace:FindChild("ImageFileRef")  -- エディタで画像を指定した FileRef
if audioRef ~= nil then
    check("FileRef.Path is string", type(audioRef.Path) == "string")
    print("  audioRef.Path: " .. audioRef.Path)
    -- Path は読取専用（書込は無視される）
    local before = audioRef.Path
    audioRef.Path = "hack.mp3"
    check("FileRef.Path is read-only", audioRef.Path == before)
    -- 消費: Sound に FileRef を代入して再生
    if snd ~= nil then
        snd.Source = audioRef
        snd:Play()
        wait(0.5)
        check("Sound.Source = FileRef → playing", snd.IsPlaying == true)
        snd:Stop()
    end
end
if imageRef ~= nil and cube ~= nil then
    local decal = cube:FindChild("Decal_Front")
    if decal ~= nil then
        decal.Source = imageRef
        check("Decal.Source = FileRef applied", true)  -- ロード失敗時はログに出る
    end
end

print("--- テスト完了 ---")

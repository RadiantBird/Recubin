-- Value系インスタンス(IntValue/BoolValue/NumberValue/Vector3Value/Color4Value/
-- CFrameValue/QuaternionValue/ObjectValue)の自動PASS/FAILテスト

local PASS = "[PASS]"
local FAIL = "[FAIL]"

local function check(label, cond)
    print((cond and PASS or FAIL) .. " " .. label)
end

-- ===== IntValue =====
print("--- IntValue ---")
local sceneInt = workspace:FindChild("TestIntValue")
check("FindChild TestIntValue", sceneInt ~= nil)
if sceneInt ~= nil then
    check("scene IntValue.Value == 7", sceneInt.Value == 7)
end

local newInt = Instance.new("IntValue")
newInt.Value = 42
check("new IntValue.Value == 42", newInt.Value == 42)

local intChangedVal = nil
newInt.Changed:Connect(function(v) intChangedVal = v end)
newInt.Value = 99
check("IntValue.Changed fired with new value", intChangedVal == 99)

-- ===== BoolValue =====
print("--- BoolValue ---")
local sceneBool = workspace:FindChild("TestBoolValue")
check("FindChild TestBoolValue", sceneBool ~= nil)
if sceneBool ~= nil then
    check("scene BoolValue.Value == true", sceneBool.Value == true)
end

local newBool = Instance.new("BoolValue")
newBool.Value = true
check("new BoolValue.Value == true", newBool.Value == true)

local boolChangedVal = nil
newBool.Changed:Connect(function(v) boolChangedVal = v end)
newBool.Value = false
check("BoolValue.Changed fired with new value", boolChangedVal == false)

-- ===== NumberValue =====
print("--- NumberValue ---")
local sceneNumber = workspace:FindChild("TestNumberValue")
check("FindChild TestNumberValue", sceneNumber ~= nil)
if sceneNumber ~= nil then
    check("scene NumberValue.Value == 3.14159", math.abs(sceneNumber.Value - 3.14159) < 0.0001)
end

local newNumber = Instance.new("NumberValue")
newNumber.Value = 2.5
check("new NumberValue.Value == 2.5", newNumber.Value == 2.5)

local numberChangedVal = nil
newNumber.Changed:Connect(function(v) numberChangedVal = v end)
newNumber.Value = 9.75
check("NumberValue.Changed fired with new value", numberChangedVal == 9.75)

-- ===== Vector3Value =====
print("--- Vector3Value ---")
local sceneVec3 = workspace:FindChild("TestVector3Value")
check("FindChild TestVector3Value", sceneVec3 ~= nil)
if sceneVec3 ~= nil then
    check("scene Vector3Value.Value == Vector3.new(1,2,3)",
        tostring(sceneVec3.Value) == tostring(Vector3.new(1, 2, 3)))
end

local newVec3 = Instance.new("Vector3Value")
newVec3.Value = Vector3.new(4, 5, 6)
check("new Vector3Value.Value == Vector3.new(4,5,6)",
    tostring(newVec3.Value) == tostring(Vector3.new(4, 5, 6)))

local vec3ChangedVal = nil
newVec3.Changed:Connect(function(v) vec3ChangedVal = v end)
newVec3.Value = Vector3.new(7, 8, 9)
check("Vector3Value.Changed fired with new value",
    vec3ChangedVal ~= nil and tostring(vec3ChangedVal) == tostring(Vector3.new(7, 8, 9)))

-- ===== Color4Value =====
print("--- Color4Value ---")
local sceneColor4 = workspace:FindChild("TestColor4Value")
check("FindChild TestColor4Value", sceneColor4 ~= nil)
if sceneColor4 ~= nil then
    check("scene Color4Value.Value == Color4.new(1,0,0,1)",
        tostring(sceneColor4.Value) == tostring(Color4.new(1, 0, 0, 1)))
end

local newColor4 = Instance.new("Color4Value")
newColor4.Value = Color4.new(0, 1, 0, 1)
check("new Color4Value.Value == Color4.new(0,1,0,1)",
    tostring(newColor4.Value) == tostring(Color4.new(0, 1, 0, 1)))

local color4ChangedVal = nil
newColor4.Changed:Connect(function(v) color4ChangedVal = v end)
newColor4.Value = Color4.new(0, 0, 1, 1)
check("Color4Value.Changed fired with new value",
    color4ChangedVal ~= nil and tostring(color4ChangedVal) == tostring(Color4.new(0, 0, 1, 1)))

-- ===== CFrameValue =====
print("--- CFrameValue ---")
local sceneCFrame = workspace:FindChild("TestCFrameValue")
check("FindChild TestCFrameValue", sceneCFrame ~= nil)
if sceneCFrame ~= nil then
    check("scene CFrameValue.Value.Position == Vector3.new(1,2,3)",
        tostring(sceneCFrame.Value.Position) == tostring(Vector3.new(1, 2, 3)))
    check("scene CFrameValue.Value.Rotation == Quaternion.new()",
        tostring(sceneCFrame.Value.Rotation) == tostring(Quaternion.new()))
end

local newCFrame = Instance.new("CFrameValue")
local qRot = Quaternion.fromAxisAngle(Vector3.new(0, 1, 0), 90)
newCFrame.Value = CFrame.new(Vector3.new(4, 5, 6), qRot)
check("new CFrameValue.Value.Position == Vector3.new(4,5,6)",
    tostring(newCFrame.Value.Position) == tostring(Vector3.new(4, 5, 6)))
check("new CFrameValue.Value.Rotation == qRot",
    tostring(newCFrame.Value.Rotation) == tostring(qRot))

local cframeChangedVal = nil
newCFrame.Changed:Connect(function(v) cframeChangedVal = v end)
newCFrame.Value = CFrame.new(Vector3.new(7, 8, 9), Quaternion.new())
check("CFrameValue.Changed fired with new value",
    cframeChangedVal ~= nil and tostring(cframeChangedVal.Position) == tostring(Vector3.new(7, 8, 9)))

-- ===== QuaternionValue =====
print("--- QuaternionValue ---")
local sceneQuat = workspace:FindChild("TestQuaternionValue")
check("FindChild TestQuaternionValue", sceneQuat ~= nil)
if sceneQuat ~= nil then
    check("scene QuaternionValue.Value == Quaternion.new()",
        tostring(sceneQuat.Value) == tostring(Quaternion.new()))
end

local newQuat = Instance.new("QuaternionValue")
local qNew = Quaternion.fromAxisAngle(Vector3.new(1, 0, 0), 45)
newQuat.Value = qNew
check("new QuaternionValue.Value == qNew", tostring(newQuat.Value) == tostring(qNew))

local quatChangedVal = nil
newQuat.Changed:Connect(function(v) quatChangedVal = v end)
local qNew2 = Quaternion.fromAxisAngle(Vector3.new(0, 0, 1), 30)
newQuat.Value = qNew2
check("QuaternionValue.Changed fired with new value",
    quatChangedVal ~= nil and tostring(quatChangedVal) == tostring(qNew2))

-- ===== ObjectValue =====
-- 注意: Instance userdata の "==" 比較はLuauEngine.cpp側でRCBN_INST_METATABLEに
-- __eqメタメソッドが未実装のため、同一インスタンスを指していても別々に取得した
-- userdataは常にfalseになる（既知の制約、Value系クラスの実装とは無関係）。
-- そのため参照の同一性は .Name の一致 + 片方を改名してもう片方に反映されるかで検証する。
print("--- ObjectValue ---")
local target = workspace:FindChild("Target")
check("FindChild Target", target ~= nil)

local sceneObjectValue = workspace:FindChild("TestObjectValue")
check("FindChild TestObjectValue", sceneObjectValue ~= nil)
if sceneObjectValue ~= nil and target ~= nil then
    local resolved = sceneObjectValue.Value
    check("scene ObjectValue.Value resolved to Target (path resolved)",
        resolved ~= nil and resolved.Name == "Target")
    -- 参照の同一性: targetを改名すると resolved 経由でも反映されるはず
    target.Name = "TargetRenamed"
    check("scene ObjectValue.Value shares identity with Target (rename reflected)",
        resolved.Name == "TargetRenamed")
    target.Name = "Target"
end

local newObjectValue = Instance.new("ObjectValue")
newObjectValue.Value = target
local newResolved = newObjectValue.Value
check("new ObjectValue.Value resolved to target", newResolved ~= nil and newResolved.Name == "Target")
target.Name = "TargetRenamed2"
check("new ObjectValue.Value shares identity with target (rename reflected)",
    newResolved.Name == "TargetRenamed2")
target.Name = "Target"

local objectChangedVal = "unset"
newObjectValue.Changed:Connect(function(v) objectChangedVal = v end)
newObjectValue.Value = nil
check("ObjectValue.Value == nil after unset", newObjectValue.Value == nil)
check("ObjectValue.Changed fired with nil", objectChangedVal == nil)

newObjectValue.Value = target
check("ObjectValue.Changed fired with Instance again",
    objectChangedVal ~= nil and objectChangedVal.Name == "Target")

print("--- テスト完了 ---")

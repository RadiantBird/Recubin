-- Recubin.exe --gen-test-scene で生成された全インスタンス網羅シーンの健全性チェック。
-- GLコンテキスト無しでも生成・保存・再ロードがクラッシュしないことの回帰確認が主目的。
local requiredNames = {
    "Cube", "Cylinder", "TriangularPrism", "MeshCube", "Sphere", "LiquidCube",
    "Sound", "PointLight", "SurfaceGui", "ParticleEmitter", "DecalHost",
}

local children = workspace:GetChildren()
local found = {}
for _, child in ipairs(children) do
    found[child.Name] = true
end

local missing = {}
for _, name in ipairs(requiredNames) do
    if not found[name] then
        table.insert(missing, name)
    end
end

local decalHost = nil
for _, child in ipairs(children) do
    if child.Name == "DecalHost" then
        decalHost = child
    end
end

local decalOk = false
local textureOk = false
if decalHost then
    for _, child in ipairs(decalHost:GetChildren()) do
        if child.Name == "Decal" then decalOk = true end
        if child.Name == "Texture" then textureOk = true end
    end
end
if not decalOk then table.insert(missing, "DecalHost/Decal") end
if not textureOk then table.insert(missing, "DecalHost/Texture") end

if #missing == 0 then
    print("[PASS] gen-test-scene: all required instances present (" .. #children .. " workspace children)")
else
    print("[FAIL] gen-test-scene: missing " .. table.concat(missing, ", "))
end

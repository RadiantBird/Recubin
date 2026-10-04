-- StressTest
local center = Vector3.new(25, 100, 25)

local ts = workspace:FindChild("TestSetting")

local amount = ts.amount.Value
local range = ts.range.Value
local hr = range/2
local anchored = ts.anchored.Value
local castShadow = ts.castShadow.Value
local randomRotation = ts.randomRotation.Value
local useMaterial = ts.useMaterial.Value

local rn = math.random
local minY, maxY = 10, 200
do
    local function l()
        return rn(-hr, hr)
    end
    function genRainPos()
        return Vector3.new(l(), rn(minY, maxY), l()) 
    end
end

local materials = {
    "MaterialService\\RoughPlastic",
    "System\\MaterialService\\ScratchedMetal",
    "System\\MaterialService\\WoodPlanks"
}

-------

function runFPSchecker()
    local fpsl: TextLabel = workspace:WaitChild("FPSlabel") -- do not care if it not found, it's your fault XDD
    local stackTime = 0
    local frames = 0
    print("Running FPS checker...")
    system.Heartbeat:Connect(function(dt)
        stackTime += dt
        frames += 1
        if stackTime >= 1 then
            fpsl.Text = `FPS: {frames}`
            stackTime = 0
            frames = 0
        end
    end)
end

function main()
    runFPSchecker()
    for i = 1, amount do
        local cube: BaseCube = Instance.new("Cube")
        cube.Color = Color4.new(rn(), rn(), rn(), 1);
        cube.Anchored = anchored
        cube.CastShadow = castShadow
        cube.Position = center + genRainPos()
        if randomRotation then
            cube.CFrame *= CFrame.new(
                math.random(0, 359),
                math.random(0, 359),
                math.random(0, 359)
            )
        end
        if useMaterial then
            cube.Material = materials[math.random(1, #materials)] or ""
        end
        cube.Parent = workspace
        cube.Name = `TCube{i}`

        -- if i % 100000 == 0 then -- do the loop ASAP
        --     wait()
        -- end
    end
    print("Generated cubes.")
    return 0
end

main()
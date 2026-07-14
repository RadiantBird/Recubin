-- StressTest
local center = Vector3.new(25, 100, 25)

local amount = 10000
local range = 50
local hr = range/2
local anchored = true

local rn = math.random

do
    local function l()
        return rn(-hr, hr)
    end
    function genRainPos()
        return Vector3.new(l(),l(),l()) 
    end
end

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
        local cube = Instance.new("TriangularPrism")
        cube.Color = Color4.new(rn(), rn(), rn(), 1);
        cube.Anchored = anchored
        cube.Position = center + genRainPos()
        cube.Parent = workspace
        cube.Name = `TCube{i}`
        if i % 100 == 0 then
            wait()
        end
    end
    print("Generated cubes.")
    return 0
end

main()
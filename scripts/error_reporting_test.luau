local signal = Instance.new("SignalEvent")
local continued = 0
local onceCalls = 0

local function explodeLookAt()
    CFrame.lookAt(CFrame.new(), Vector3.zero)
end

signal.Fired:Connect(function()
    explodeLookAt()
end)

signal.Fired:Connect(function()
    continued += 1
end)

signal.Fired:Once(function()
    onceCalls += 1
    error("once failure marker")
end)

signal:Fire()
signal:Fire()

if continued == 2 then
    print("[PASS] Signal errors do not stop other listeners or later fires")
else
    print("[FAIL] Signal listener continuation count: " .. continued)
end

if onceCalls == 1 then
    print("[PASS] Once listener disconnects after an error")
else
    print("[FAIL] Once listener call count: " .. onceCalls)
end

task.spawn(function()
    error("task failure marker")
end)

local brokenModule = script:FindChild("BrokenModule")
task.spawn(function()
    require(brokenModule)
end)

local timeoutSignal = Instance.new("SignalEvent")
local reachedAfterTimeout = false
timeoutSignal.Fired:Connect(function()
    while true do
    end
end)
timeoutSignal.Fired:Connect(function()
    reachedAfterTimeout = true
end)
timeoutSignal:Fire()

if reachedAfterTimeout then
    print("[PASS] Signal timeout is isolated and later listeners run")
else
    print("[FAIL] Signal timeout prevented later listener")
end

-- SignalEventComms
local signalEvent: SignalEvent = workspace:FindChild("SignalEvent")
local mode = script.Mode

local mv = mode.Value
if mv == 0 then
    task.wait(0.2)
    signalEvent:Fire("Hello", 123, {"world"})
elseif mv == 1 then
    signalEvent.Fired:Connect(function(s, n, t)
        print(s)
        print(n)
        print(t)
    end)
else
    print("What?")
end
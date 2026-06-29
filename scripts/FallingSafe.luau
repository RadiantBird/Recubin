-- FallingSafe
local user = system.User
local char = workspace:WaitChild("PlayerCharacter")
local root = char:FindChild("Root")

system.Heartbeat:Connect(function(...)
    if root.Position.y <= -50 then
        root.Position = Vector3.new(0, 100, 0)
    end
end)
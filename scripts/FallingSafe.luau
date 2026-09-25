-- FallingSafe
-- local user = system.User
local char: Model = workspace:WaitChild("PlayerCharacter")
local root = char:FindChild("Root")
local spawnPoint: BaseCube = workspace:FindChild("SpawnLocation")
local spawnPos = Vector3.new(0, 100, 0)
if spawnPoint then
    spawnPos = spawnPoint.WorldPosition + Vector3.new(0, 10, 0)
end

while wait() do
    if root.WorldPosition.y <= -50 then
        char:PivotTo(CFrame.new(spawnPos))
        wait(1)
    end
end
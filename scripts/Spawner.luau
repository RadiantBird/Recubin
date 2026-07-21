-- Spawner
local Spawn: Cube = script.Parent

function teleportChar(character)
    if not character then
        return
    end

    local root = character:FindChild("Root")
    if not root then 
        return
    end

    root.Position = Spawn.Position + Vector3.new(0, 6.5, 0)
end

-- teleportChar(workspace:WaitChild("PlayerCharacter"))

User.CharacterAdded:Connect(function(character)
    teleportChar(character)
end)
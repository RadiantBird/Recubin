-- DamageScript
local p:Cube = script.Parent
p.Touched:Connect(function(other:BaseCube)
    print(other)
    local op:Instance? = other.Parent
    if not op then
        return
    end

    if op.Name == "PlayerCharacter" then
        local humanoid:Humanoid? = op:FindChild("Humanoid")
        if humanoid then
            humanoid.Health = 0
            print("Oof!")
        end
    end
end)
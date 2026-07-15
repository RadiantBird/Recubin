-- KeyframeCheck
local character = script.Parent
local humanoid   = character and character:FindChild("Humanoid")
local anim       = character and character:FindChild("Animation")

if not humanoid or not anim then
    print("[WARN] StarterCharacter/Humanoid/Animation が見つかりません。パスを調整してください")
    return
end

humanoid.KeyframeReached:Connect(function(partName, time)
    print("[KeyframeReached] part=" .. partName .. "  time=" .. string.format("%.2f", time))
end)

humanoid:PlayAnimation(anim)
print("=== PlayAnimation started ===")

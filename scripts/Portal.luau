-- Portal
local frontrooms: Workspace = system.Frontrooms
local backrooms:  Workspace = system.Backrooms

local fButton, bButton = frontrooms.Button, backrooms.Button
local p1:ProximityPrompt, p2:ProximityPrompt = fButton.Prompt, bButton.Prompt

local char = nil
User.CharacterAdded:Connect(function(c)
    char = c
    print(char)
end)

p1.Triggered:Connect(function()
    char.Parent = backrooms
end)

p2.Triggered:Connect(function()
    char.Parent = frontrooms
end)
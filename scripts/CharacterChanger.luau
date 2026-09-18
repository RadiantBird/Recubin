-- CharacterChanger
--
-- The old script waited for workspace.Agent, but triangle.rcbn uses the
-- StarterCharacter template (Storage/Model).  The runtime clones that
-- template before firing CharacterAdded, so replacing it with the template
-- or with a stale Agent would discard the cloned references and accessories.
local function validateCharacter(character)
    if not character then
        return
    end

    local humanoid = character:FindChild("Humanoid")
    local root = character:FindChild("Root")
    local torso = character:FindChild("Torso")

    if not humanoid or not root or not torso then
        print("[CharacterChanger] Character is not an R6 rig: " .. character.Name)
        return
    end

    print("[CharacterChanger] Using spawned R6 character: " .. character.Name)
end

User.CharacterAdded:Connect(validateCharacter)
validateCharacter(User.Character)

-- InputTest
local User = System:FindChild("User")

User.Input.Pressed:Connect(function(key)
    if key == "Space" then print("jump!") end
    if key == "MouseButton1" then print("clicked") end
    print(key)
end)
User.Input.Released:Connect(function(key) print("released " .. key) end)
if User.Input:IsPressed("W") then print("moving forward") end

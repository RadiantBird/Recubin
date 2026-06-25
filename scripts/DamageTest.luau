-- DamageTest
print("wait for 3 sec")
wait(3)
local hum = workspace.PlayerCharacter.Humanoid
hum.Died:Connect(function() print("died!") end)
hum:TakeDamage(60)   -- Health 40
hum.Health = 0       -- Died発火 → ラグドール → RespawnTime後に再生成

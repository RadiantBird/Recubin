-- Mutable Vector Test
print("Mutable Vector Test starting")
local v = Vector3.zero

while v.x < 10000 do
    v:set(v.x + 1, v.y, v.z)
end
print(`Test Succeed: {v.x}`)
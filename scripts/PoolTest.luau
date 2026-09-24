-- PoolTest
print("Starting pool test")
while true do
    local c:Cube = Instance.pick("Cube") -- Poolから取得する
    c.WorldPosition = Vector3.new(1, math.random(2, 15), 3)
    c.Anchored = true
    c.Parent = workspace
    task.wait()
    c = Instance.throw(c) -- Poolに返す
    assert(c == nil, "C++ lied!!!")
end

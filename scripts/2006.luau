local origin = Vector3.new(0, 100, 0)
local direction = Vector3.new(0, -1, 0)
local result = workspace:Raycast(origin, direction, {})

if result then
    print("Hit!")
    print(result.Distance)
    if result.Instance then
        print(result.Instance.Name)
    end
else
    print("No hit")
end

print(Vector3.zero)

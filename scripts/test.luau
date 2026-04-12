print("Hello from Luau!");
local Floppa = workspace:FindChild("Floppa");
print(Floppa.Name);
print(Floppa.Position);
Floppa.Name = "Big Floppa";
print(Floppa.Name);
Floppa.Position = Vector3(1, 2, 3);
print(Floppa.Position);
Floppa.Color = Color4(1, 0, 0, 1);
print(Floppa.Color);

print("Waiting 2 seconds...");
wait(2.0);
print("Resumed after wait!");

print("Waiting 1 second...");
wait(1.0);
print("Resumed after second wait!");
for i = 1, 5 do
    print("Count: " .. i);
    wait(0.5);
end
print("Disco mode engaged!");
while true do
    Floppa.Color = Color4(math.random(), math.random(), math.random(), 1);
    wait(0.1);
end
-- PathfindingTest
-- ナビメッシュパスファインディングのテスト:
-- PathfindingService:FindPath() で経路を求め、PlayerCharacterのHumanoidを
-- ウェイポイントに沿って歩かせる。Action=="Jump"の地点ではジャンプする。
--
-- goalOffset はテストシーンの地形に合わせて調整してください
-- （ジャンプが必要な隙間・段差を跨ぐ位置にすると挙動を確認しやすい）。

local character = script.Parent--workspace:WaitChild("PlayerCharacter")
local humanoid   = character:FindChild("Humanoid")
local root       = character:FindChild("Root")
assert(not root.Anchored, "Root is anchored!!!")
if not humanoid or not root then
    print("[FAIL] HumanoidまたはRootが見つかりません")
    print(humanoid, root)
    return
end

local pathfindingService = System:FindChild("PathfindingService")
if not pathfindingService then
    print("[FAIL] PathfindingServiceが見つかりません")
    return
end

local startPos = root.WorldPosition
local goalPos  = workspace.Goal.WorldPosition
print(goalPos)

local waypoints = pathfindingService:FindPath(workspace, startPos, goalPos)
print("[PathfindingTest] waypoints: " .. #waypoints)

if #waypoints == 0 then
    print("[FAIL] 経路が見つかりませんでした（Terrain/静的Cubeの配置とgoalOffsetを確認してください）")
    return
end

for i, wp in ipairs(waypoints) do
    print(string.format("  #%d %s (%.1f, %.1f, %.1f)", i, wp.Action, wp.Position.x, wp.Position.y, wp.Position.z))
end

local index = 1
local jumpedThisWaypoint = false

local conn
conn = System.Heartbeat:Connect(function(dt)
    if index > #waypoints then
        print("[PASS] 目標地点に到達しました")
        conn:Disconnect()
        return
    end

    local wp = waypoints[index]

    if wp.Action == "Jump" and not jumpedThisWaypoint then
        humanoid:Jump()
        jumpedThisWaypoint = true
    end

    local arrived = humanoid:MoveToward(wp.Position)
    if arrived then
        index = index + 1
        jumpedThisWaypoint = false
    end
end)

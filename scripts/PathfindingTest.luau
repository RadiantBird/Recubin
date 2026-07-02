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
pathfindingService:Configure({
    AgentRadius     = 2.0,  -- エージェント半径(stud)。範囲 0.1〜10.0
    AgentHeight     = 2.0,  -- エージェント高さ(stud)。範囲 0.5〜20.0
    AgentMaxClimb   = 0.6,  -- 登れる段差の高さ(stud)。範囲 0〜10.0
    AgentMaxSlope   = 50.0, -- 歩行可能な最大斜度(度)。範囲 0〜89.0
    MaxJumpDistance = 6.0,  -- ジャンプで飛び越えられる最大水平距離(stud)。範囲 0〜50.0
    MaxJumpHeight   = 4.0,  -- ジャンプで飛び越えられる最大高低差(stud)。範囲 0〜50.0
})

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

local conn
conn = System.Heartbeat:Connect(function(dt)
    if index > #waypoints then
        print("[PASS] 目標地点に到達しました")
        conn:Disconnect()
        return
    end

    local wp = waypoints[index]
    local arrived = humanoid:MoveToward(wp.Position)

    if arrived then
        -- wp.Action=="Jump" は「このウェイポイントに到達した時点で跳ぶ」の意味
        -- （次のウェイポイントが跳んだ先の着地点）。到達前にJump()を呼ぶと
        -- 踏切位置よりずっと手前で跳んでしまい、着地位置に届かなくなる。
        if wp.Action == "Jump" then
            humanoid:Jump()
        end
        index = index + 1
    end
end)

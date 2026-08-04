-- ============================================================
--  GameController.luau  ―  コイン収集ミニゲームの中枢ロジック
--
--  検証する機能:
--    * Instance:FindChild / GetChildren で階層探索（直下のみ）
--    * BaseCube.Touched シグナル（コイン収集・ハザード接触）
--    * BaseCube.Position の読み書き（コインの退避/復元、ドア開閉）
--    * GuiButton.Activated シグナル（リセットボタン）
--    * ProximityPrompt.Triggered シグナル（ドアを開く）
--    * Humanoid:TakeDamage / Humanoid.Health（ブレードでダメージ）
--    * System.Heartbeat（ダメージのクールダウン用クロック）
--    * Vector3.new
-- ============================================================

print("[GameController] start")

local score = 0
local total = 0

-- Heartbeat で進む簡易クロック（ブレードのダメージ間隔に使う）
local clock = 0
System.Heartbeat:Connect(function(dt)
    clock += dt
end)

-- ---- HUD 取得（Workspace > HUD > ScoreLabel / ResetButton） ----
local hud        = workspace:FindChild("HUD")
local scoreLabel = hud and hud:FindChild("ScoreLabel")
local resetBtn   = hud and hud:FindChild("ResetButton")

-- ---- コイン取得（Workspace > Coins の子すべて） ----
local coins = {} -- { inst, home(Vector3), collected(bool) }
local coinsFolder = workspace:FindChild("Coins")
if coinsFolder then
    for _, coin in ipairs(coinsFolder:GetChildren()) do
        total += 1
        coins[#coins + 1] = { inst = coin, home = coin.Position, collected = false }
    end
end

local function updateHud()
    if not scoreLabel then return end
    if total > 0 and score >= total then
        scoreLabel.Text = "You Win!  " .. score .. " / " .. total
    else
        scoreLabel.Text = "Score: " .. score .. " / " .. total
    end
end
updateHud()

-- ---- コイン収集: 触れたら地下へ退避してスコア加算 ----
for _, c in ipairs(coins) do
    c.inst.Touched:Connect(function()
        if c.collected then return end
        c.collected = true
        score += 1
        c.inst.Position = Vector3.new(c.home.x, -500, c.home.z)
        print("[GameController] collected " .. c.inst.Name .. "  (" .. score .. "/" .. total .. ")")
        updateHud()
    end)
end

-- ---- リセットボタン: コインを元に戻してスコアを 0 に ----
if resetBtn then
    resetBtn.Activated:Connect(function()
        for _, c in ipairs(coins) do
            c.inst.Position = c.home
            c.collected = false
        end
        score = 0
        updateHud()
        print("[GameController] reset")
    end)
end

-- ---- ハザード: Blade に触れると 1 秒間隔でダメージ ----
local blade = workspace:FindChild("Blade")
if blade then
    local lastHit = -10
    blade.Touched:Connect(function()
        if clock - lastHit < 1.0 then return end
        lastHit = clock
        local player = workspace:FindChild("PlayerCharacter")
        local hum = player and player:FindChild("Humanoid")
        if hum then
            hum:TakeDamage(15)
            print("[GameController] blade hit! Health = " .. hum.Health)
        end
    end)
end

-- ---- ドア: ProximityPrompt を起動すると上にスライドして開く ----
local door = workspace:FindChild("Door")
local prompt = door and door:FindChild("Prompt")
if prompt then
    local opened = false
    prompt.Triggered:Connect(function()
        if opened then return end
        opened = true
        local p = door.Position
        door.Position = Vector3.new(p.x, p.y + 6, p.z)
        print("[GameController] door opened")
    end)
end

print("[GameController] ready: " .. total .. " coins to collect")

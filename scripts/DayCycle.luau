-- DayCycle
local skyboxInstance = workspace:FindChild('Skybox')
assert(skyboxInstance, 'Workspace.Skybox is required by DayCycle')
local Skybox = skyboxInstance :: Skybox

local sunInstance = workspace:FindChild('Sun')
assert(sunInstance, 'Workspace.Sun is required by DayCycle')
local Sun = sunInstance :: Sun

local lightingInstance = workspace:FindChild('Lighting')
assert(lightingInstance, 'Workspace.Lighting is required by DayCycle')
local Lighting = lightingInstance :: Lighting

local cycleSeconds = 180 -- 1周にかかる時間（秒）

local maxBrightness = 1.0
local minBrightness = 0.1

local dayColor = Color4.new(0, 130, 255, 255)
local nightColor = Color4.new(20, 31, 82, 255)

local currentTime = 0

-- 線形補完（Lerp）関数
-- a: 開始値, b: 終了値, t: 割合(0.0 ~ 1.0)
function lerp(a:number, b:number, t:number): number
    return a + (b - a) * t
end

local function toByte(val)
    if val < 0 then return 0 end
    if val > 255 then return 255 end
    return math.floor(val + 0.5)
end

function lerpColor(c1, c2, t)
    return Color4.fromRGBA(
        toByte(lerp(c1.r, c2.r, t)),
        toByte(lerp(c1.g, c2.g, t)),
        toByte(lerp(c1.b, c2.b, t)),
        toByte(lerp(c1.a, c2.a, t))
    )
end

function init()
    Skybox.Color = dayColor
end

function update(deltaTime)
    currentTime = (currentTime + deltaTime) % cycleSeconds

    local progress = currentTime / cycleSeconds
    local rad = progress * math.pi * 2

    -- 東(方角90°)の地平線から天頂を通って西(方角270°)の地平線へ昇り、地平線の下を戻る。
    -- 太陽の向きがそのまま光の向きになる（Sun.Angle = Vector2(方角, 高度)）。
    local elevation = math.deg(math.asin(math.sin(rad)))
    local azimuth = if math.cos(rad) >= 0 then 90 else 270
    Sun.Angle = Vector2.new(azimuth, elevation)

    local sunHeight = (math.sin(rad) + 1) / 2

    Lighting.Brightness = lerp(minBrightness, maxBrightness, sunHeight)
    local finalColor = lerpColor(nightColor, dayColor, sunHeight)
    -- print(finalColor)
    Skybox.Color = finalColor
end

function main()
    init()

    system.Heartbeat:Connect(function(dt)
        update(dt)
    end)
end

main()

-- DayCycle
local Skybox = workspace:FindChild('Skybox')
local Sun = workspace:FindChild('Sun')
local Lighting = workspace:FindChild('Lighting')

local cycleSeconds = 30 -- 1周にかかる時間（秒）

local maxBrightness = 1.0
local minBrightness = 0.1

local dayColor = Color4.new(0, 130, 255, 255)
local nightColor = Color4.new(20, 31, 82, 255)

local currentTime = 0

-- 線形補完（Lerp）関数
-- a: 開始値, b: 終了値, t: 割合(0.0 ~ 1.0)
function lerp(a, b, t)
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
    local angle = progress * 360
    Sun.Angle = angle

    local sunHeight = (math.sin(progress * math.pi * 2) + 1) / 2
    local rad = math.rad(angle)
    local dirX = 0.0
    local dirY = math.sin(rad)  -- 高さ（昼に+1.0、夜に-1.0）
    local dirZ = math.cos(rad)

    Lighting.Brightness = lerp(minBrightness, maxBrightness, sunHeight)
    local finalColor = lerpColor(nightColor, dayColor, sunHeight)
    -- print(finalColor)
    Lighting.Direction = Vector3.new(dirX, -dirY, -dirZ)
    Skybox.Color = finalColor
end

function main()
    init()

    system.Heartbeat:Connect(function(dt)
        update(dt)
    end)
end

main()

-- TerrainDoodle
-- 地形APIテスト: 空中にスマイルを描く
local terrain = workspace:FindChild("Terrain")
if not terrain then
    print("Terrain が見つかりません")
    return
end

-- 11x11 のスマイル（'#' がブロックを置く位置）
local face = {
    "...#####...",
    "..#.....#..",
    ".#.......#.",
    "#...#.#...#",   -- 目
    "#...#.#...#",
    "#.........#",
    "#.#.....#.#",   -- 口（笑み）
    "#..#####..#",
    ".#.......#.",
    "..#.....#..",
    "...#####...",
}

local BS      = 4                          -- 1ブロック = 4 studs
local originX = -20                        -- 左端の X (studs)
local topY    = 150                        -- 上端の Y (studs, 空中)
local planeZ  = 0                          -- 面の Z (studs)
local yellow  = Color4.new(1.0, 0.85, 0.1, 1.0)

local function draw()
    local placed = 0
    for r = 1, #face do
        local row = face[r]
        for c = 1, #row do
            if string.sub(row, c, c) == "#" then
                local x = originX + (c - 1) * BS
                local y = topY    - (r - 1) * BS
                if terrain:SetBlock(Vector3.new(x, y, planeZ), yellow) then
                    placed = placed + 1
                end
            end
        end
    end
    return placed
end

-- 地形チャンクがロードされるまで少し待ってから描画（最大10回リトライ）
for attempt = 1, 10 do
    wait(0.3)
    local n = draw()
    print("配置したブロック数: " .. n)
    if n > 0 then break end
end

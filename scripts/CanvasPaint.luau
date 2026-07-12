-- CanvasPaint
-- Canvasペイントデモ: ツールを装備(キー1)して壁をクリックすると赤いドットを打つ
local tool = workspace:FindChild("PaintBrush")
if not tool then
    print("PaintBrush が見つかりません")
    return
end

User:AddTool(tool, 1)

-- NOTE: [PAINT]printは動作確認用の一時診断。確認後に削除する
tool.Activated:Connect(function()
    local ray = User:GetMouseRay()
    if not ray then
        print("[PAINT] GetMouseRay = nil")
        return
    end

    local hit = workspace:Raycast(ray.Origin, ray.Direction, User.Character)
    if not hit or not hit.Instance then
        print("[PAINT] Raycast = no hit / dir=" .. tostring(ray.Direction))
        return
    end
    print("[PAINT] hit: " .. hit.Instance.Name .. " pos=" .. tostring(hit.Position)
          .. " dir=" .. tostring(ray.Direction))

    local canvas = hit.Instance.Canvas
    if not canvas then
        print("[PAINT] no Canvas child")
        return
    end

    local uv = canvas:WorldToUV(hit.Position)
    if not uv then
        print("[PAINT] WorldToUV = nil")
        return
    end

    local px = math.floor(uv.X * canvas.Width)
    local py = math.floor(uv.Y * canvas.Height)
    local ok = canvas:SetPixel(px, py, Color4.new(1, 0, 0, 1))
    print("[PAINT] SetPixel(" .. px .. ", " .. py .. ") = " .. tostring(ok))
end)

print("CanvasPaint ready: press 1 to equip, click the wall to paint")

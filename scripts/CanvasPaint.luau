-- CanvasPaint
-- Canvasペイントデモ: ツールを装備(キー1)して壁をクリックすると赤いドットを打つ
local tool = workspace:FindChild("PaintBrush")
if not tool then
    print("PaintBrush が見つかりません")
    return
end

User:AddTool(tool, 1)

tool.Activated:Connect(function()
    local ray = User:GetMouseRay()
    if not ray then
        return
    end

    local hit = workspace:Raycast(ray.Origin, ray.Direction, User.Character)
    if not hit or not hit.Instance then
        return
    end

    local canvas = hit.Instance.Canvas
    if not canvas then
        return
    end

    local uv = canvas:WorldToUV(hit.Position)
    if not uv then
        return
    end

    local px = math.floor(uv.X * canvas.Width)
    local py = math.floor(uv.Y * canvas.Height)
    canvas:SetPixel(px, py, Color4.new(1, 0, 0, 1))
end)

print("CanvasPaint ready: press 1 to equip, click the wall to paint")

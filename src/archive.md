2. 接続クラス(Connect関数 -> RCBNScriptConnection)の追加
    例:
    ```lua
    System.Heartbeat:Connect(function(dt)
        print(dt)
    end)

    local Cube = workspace.Cube
    local Connection = nil
    Connection = Cube.Touched:Connect(function()
        print("Ouch!")
        Connection:Disconnect()
    end)
    ```
    接続クラスは接続元のインスタンスが破棄されるときに自動的に切断される。

    - スマート接続(Once関数/Until関数 -> RCBNScriptConnection)
    ```lua
    local Cube = workspace.Cube
    local Connection = nil
    Connection = Cube.Touched:Once(function()
        print("Ouch!")
        -- 自動で切断
    end)

    local ForceDisconnect = Instance.new("Event")
    ForceDisconnect.Name = "FD"

    System.Heartbeat:Until(ForceDisconnect, function(dt) 
        print(dt)
    end)

    ForceDisconnect:Fire() -- Heartbeatに接続された関数が切断される
    ```

3. ゲーム内GUIの実装
    ランタイムビューポートの中にImGuiで実装。

    - ScreenGuiObject : Instance(Abstract)
        - Active -> boolean (イベントを反応させるかどうか)
        - Position -> Vector2
        - Size -> Vector2
        - Norm -> enum class Norm {
            Pixel, (自動整形なし)
            Scale  (自動整形あり)
        };
        - Visible -> boolean
        - BackgroundColor -> Color4
        - Transparency -> float (BackgroundColorのAと同期)
        - ZIndex -> int (小さいほど優先して描画)

        - GuiButton : ScreenGuiObject(Abstract)
            - Activated -> RCBNScriptConnection
            (ボタンが押されたときのイベント)

            - TextButton : GuiButton
                - Text -> String
                - TextColor -> Color4
        - TextLabel : ScreenGuiObject
            - Text -> String
            - TextColor -> Color4
    - WorldGuiObject : Instance
        (この時点ではPositionは持たない。親のBaseCubeのPositionとFaceに依存する)
        - Size -> Vector2
        - Norm -> enum class Norm {
            Pixel, (自動整形なし)
            Scale  (自動整形あり)
        };
        - Active -> boolean (イベントを反応させるかどうか)
        - Visible -> boolean
        - BackgroundColor -> Color4
        - Transparency -> float (BackgroundColorのAと同期)
        - ZIndex -> int (小さいほど優先して描画)

        - SurfaceGui : WorldGuiObject
            - Face -> enum class Face {
                Front = 0,
                Back = 1,
                Top = 2,
                Bottom = 3,
                Right = 4,
                Left = 5
            };
            (子要素はWorldで描画される(TextLabel/TextButtonなど))
        - BillboardGui : WorldGuiObject
            - Mode -> enum class Mode {
                Parallel (画面に平行)
                Focus (カメラの方向を向く)
            };
-- Program (IPC) の使い方サンプル。
--
-- workspace.EchoServer は Program インスタンスで、assets/echo_server.exe（src/echo_server.cpp）を所有する。
-- メソッド一覧（すべて System.EnableIPCAPI が必要。失敗は Luau エラーなので pcall で捕捉できる）:
--   Start()               プロセスを起動する
--   Call(request, timeout?) 要求を送って応答を待つ。待つ間はコルーチンが止まるだけでゲームは止まらない
--   Send(request)         要求を送るだけ（応答は後で Receive で受け取る）
--   Receive()             受信キューの先頭を返す。無ければ nil（待たない）
--   Disconnect()/Connect() 通信を止める/再開する（プロセスは生きたまま）
--   Close(grace?)         終了を要求して待ち、終了コードを返す
--
-- 通信は「1 行 = 1 メッセージ」。文字列中の改行はエンジンが自動でエスケープ/復元するので、
-- スクリプトからは普通の改行入りの文字列として扱える。

local prog = workspace.EchoServer
local label = workspace.IpcGui.Log

local lines = {}
local function log(text)
    table.insert(lines, text)
    print("[ProgramIpcSample] " .. text)
    -- TextLabel.Text は複数行に対応している
    label.Text = table.concat(lines, "\n")
end

-- 1. 起動
log("== 1. Start")
prog:Start()
log("started: " .. prog.Path)

-- 2. Call: 要求を送って応答を待つ（同期）
log("== 2. Call")
log("ping            -> " .. prog:Call("ping"))
log("upper hello     -> " .. prog:Call("upper hello"))
log("add 2 3.5       -> " .. prog:Call("add 2 3.5"))

-- 3. 改行を含む文字列もそのまま往復できる
log("== 3. Multi-line message")
local echoed = prog:Call("echo first line\nsecond line")
log("echo (2 lines)  -> " .. echoed:gsub("\n", " / "))
assert(echoed == "first line\nsecond line")

-- 4. Send + Receive: 送信と受信を分ける（非同期）
log("== 4. Send / Receive")
prog:Send("upper sent with Send")
local received = prog:Receive()
log("Receive right after Send -> " .. tostring(received) .. "  (nil: まだ届いていない)")
while received == nil do
    wait() -- 1 フレーム待つ。この間に応答が届く
    received = prog:Receive()
end
log("Receive later           -> " .. received)

-- 5. タイムアウト: slow は応答に 2 秒かかる。Call の待ち時間を 0.5 秒にするとエラーになる
log("== 5. Call timeout")
local ok, err = pcall(function() return prog:Call("slow", 0.5) end)
log("Call(slow, 0.5) ok=" .. tostring(ok) .. "  error=" .. tostring(err))
-- 遅れて届く "slow done" は破棄される。次の Call は子が slow を終えた後に応答するので少し待つ。
log("count           -> " .. prog:Call("count") .. "  (直前の slow が終わるまで待った)")

-- 6. Disconnect / Connect: プロセスは生かしたまま通信を止める
log("== 6. Disconnect / Connect")
prog:Send("echo buffered while disconnected")
prog:Disconnect()
local sendOk, sendErr = pcall(function() prog:Send("ping") end)
log("Send while disconnected -> ok=" .. tostring(sendOk) .. "  error=" .. tostring(sendErr))
wait(0.3) -- この間に子の応答が届き、内部に溜まる
prog:Connect()
prog:Call("ping") -- 接続後に 1 往復して、溜まった応答を読み込ませる
log("Receive after Connect  -> " .. tostring(prog:Receive()))

-- 7. Close: stdin を閉じて終了を待つ。終了コードが返る（echo_server は処理した要求数を返す）
log("== 7. Close")
local exitCode = prog:Close()
log("exit code = " .. tostring(exitCode))
log("")
log("Done. 詳しい解説は README.md を参照。")

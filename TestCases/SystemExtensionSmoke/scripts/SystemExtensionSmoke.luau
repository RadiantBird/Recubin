-- System extension manual smoke test.  Every check is isolated with pcall so
-- one denied operation does not hide the remaining permission boundaries.
local results = {}
local displayResults = {}
local function show(name, ok, detail, screenDetail)
    local line = (ok and "PASS  " or "FAIL  ") .. name
    if detail ~= nil then line = line .. " : " .. tostring(detail) end
    table.insert(results, line)
    local screenLine = (ok and "PASS  " or "FAIL  ") .. name
    if screenDetail ~= nil then screenLine = screenLine .. " : " .. tostring(screenDetail) end
    table.insert(displayResults, screenLine)
    print("[SystemExtensionSmoke] " .. line)
end
local function succeeds(name, fn)
    local ok, value = pcall(fn)
    show(name, ok, ok and value or value)
    return ok, value
end
local function fails(name, fn)
    local ok, value = pcall(fn)
    show(name, not ok, ok and "unexpected success" or value)
    return not ok
end

local root = "SystemExtensionSmoke"
local copyPath = root .. "/copy.txt"
local movedPath = root .. "/moved.txt"
do
    local cleanupCall, cleanupValue = pcall(function() return IO.RemoveTree(root) end)
    show("Initial cleanup of prior run", not cleanupCall or cleanupValue, cleanupValue)
end
succeeds("CreateDirectory", function() return IO.CreateDirectory(root) end)
succeeds("WriteText", function() return IO.WriteText(root .. "/text.txt", "alpha") end)
succeeds("ReadText", function() assert(IO.ReadText(root .. "/text.txt") == "alpha") end)
succeeds("WriteBytes with NUL", function() return IO.WriteBytes(root .. "/bytes.bin", "a\0b") end)
succeeds("ReadBytes preserves NUL", function() assert(IO.ReadBytes(root .. "/bytes.bin") == "a\0b") end)
succeeds("AppendText", function()
    IO.AppendText(root .. "/text.txt", "-tail")
    assert(IO.ReadText(root .. "/text.txt") == "alpha-tail")
end)
succeeds("AppendBytes with NUL", function()
    IO.AppendBytes(root .. "/bytes.bin", "-\0tail")
    assert(IO.ReadBytes(root .. "/bytes.bin") == "a\0b-\0tail")
end)
succeeds("Exists", function() assert(IO.Exists(root .. "/text.txt")) end)
succeeds("IsFile", function() assert(IO.IsFile(root .. "/text.txt")) end)
succeeds("IsDirectory", function() assert(IO.IsDirectory(root)) end)
succeeds("List fields and name order", function()
    local entries = IO.List(root)
    local expected = { ["bytes.bin"] = true, ["text.txt"] = true }
    local previous = ""
    local seen = {}
    for _, entry in ipairs(entries) do
        assert(type(entry.Name) == "string" and type(entry.Path) == "string")
        assert(type(entry.IsDirectory) == "boolean" and type(entry.Size) == "number")
        assert(entry.Name >= previous)
        assert(expected[entry.Name] and not seen[entry.Name])
        seen[entry.Name] = true
        previous = entry.Name
    end
    assert(seen["bytes.bin"] and seen["text.txt"])
end)
succeeds("Copy without overwrite", function() return IO.Copy(root .. "/text.txt", copyPath) end)
fails("Copy overwrite rejected", function() return IO.Copy(root .. "/text.txt", copyPath) end)
succeeds("Copy with overwrite", function() return IO.Copy(root .. "/text.txt", copyPath, true) end)
succeeds("Move", function() return IO.Move(copyPath, movedPath) end)
succeeds("Remove file", function() return IO.Remove(movedPath) end)
succeeds("Remove empty directory", function()
    IO.CreateDirectory(root .. "/empty")
    return IO.Remove(root .. "/empty")
end)
fails("Remove non-empty directory rejected", function() return IO.Remove(root) end)
fails("Parent traversal rejected", function() return IO.Exists("../outside") end)
fails("Absolute path rejected", function() return IO.Exists("C:/outside") end)
succeeds("RemoveTree cleanup", function() return IO.RemoveTree(root) end)

local function notImplemented(name, fn)
    local ok, value = pcall(fn)
    local message = tostring(value):lower()
    show(name, not ok and string.find(message, "not implemented", 1, true) ~= nil, value)
end
notImplemented("IPC.Connect is not implemented", function() return IPC.Connect("smoke") end)
notImplemented("IPC.Send is not implemented", function() return IPC.Send("smoke", "message") end)
notImplemented("IPC.Receive is not implemented", function() return IPC.Receive("smoke") end)
notImplemented("IPC.Close is not implemented", function() return IPC.Close("smoke") end)

local saveData = workspace.SaveData
local previous = saveData.Content
show("TextFile seed or previous overlay is visible", previous ~= "", previous,
    string.sub(tostring(previous), 1, 40))
succeeds("TextFile.Content write", function() saveData.Content = "SystemExtensionSmoke overlay" end)
succeeds("TextFile.Content read after write", function()
    assert(saveData.Content == "SystemExtensionSmoke overlay")
end)
show("TextFile.StorageId is private", saveData.StorageId == nil)
fails("TextFile:Clone rejected", function() return saveData:Clone() end)
fails("Instance.new TextFile rejected", function() return Instance.new("TextFile") end)

local output = table.concat(results, "\n")
local midpoint = math.ceil(#displayResults / 2)
workspace.SmokeGui.SmokeResultsLeft.Text = table.concat(displayResults, "\n", 1, midpoint)
workspace.SmokeGui.SmokeResultsRight.Text = table.concat(displayResults, "\n", midpoint + 1)
print("[SystemExtensionSmoke] complete\n" .. output)

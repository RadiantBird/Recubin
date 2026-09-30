-- ReadWriteScript
local file: TextFile? = workspace:FindChild("ReadWriteTest")
if file then
    print(`Content: {file.Content}`)
end
-- file.Content += "Hi"

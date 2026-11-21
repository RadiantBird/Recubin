-- Only setup.lua invokes the upstream shader compiler build.
local root = path.absolute(".")
local source = path.join(root, "external", "sokol-tools")
local revision = "7d6464c17b949f9a434d25f8beab18653a44fbda"

function main(output)
    if not output or output == "" then
        raise("usage: xmake lua tools/setup-shdc.lua <output-executable>")
    end
    local actual = os.iorunv("git", {"rev-parse", "HEAD"}, {curdir = source}):trim()
    if actual ~= revision then
        raise("setup-shdc: expected sokol-tools commit %s, found %s", revision, actual)
    end

    local find_tool = import("lib.detect.find_tool")
    local deno = os.getenv("DENO")
    if not deno or deno == "" then
        local detected = find_tool("deno")
        deno = detected and detected.program
    end
    if not deno then
        raise("setup-shdc: Deno was not found. Set DENO to the executable path.")
    end

    os.vrunv("git", {"submodule", "update", "--init", "--recursive"}, {curdir = source})
    local args = {"run", "--allow-all", "--no-config", "jsr:@floooh/fibs@1.1.5"}
    local function fibs(arguments)
        os.vrunv(deno, table.join(args, arguments), {curdir = source})
    end
    fibs({"config", "win-msvc-release"})
    fibs({"build", "sokol-shdc"})

    local executable = path.join(source, ".fibs", "dist", "win-msvc-release", "sokol-shdc.exe")
    if not os.isfile(executable) then
        raise("setup-shdc: release build did not produce %s", executable)
    end
    os.mkdir(path.directory(path.absolute(output)))
    os.cp(executable, path.absolute(output))
end

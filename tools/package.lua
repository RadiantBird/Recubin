-- Recubin Studio packager.
--
-- This script is intentionally independent from the xmake target graph.  The
-- build must already have completed before it is invoked:
--   xmake lua tools/package.lua [Debug|Release]
-- The script uses xmake's Lua filesystem/process APIs and PowerShell's ZIP
-- implementation on Windows.

local root = path.absolute(".")
local windows = os.host() == "windows"
local config = (arg and arg[1]) or "Release"
if config ~= "Debug" and config ~= "Release" then
    raise("configuration must be Debug or Release")
end

local function require_file(file, description)
    if not os.isfile(file) then
        raise(description .. " not found: " .. file)
    end
end

local function mkdir(dir)
    os.mkdir(dir)
end

local function copy_file(source, destination)
    mkdir(path.directory(destination))
    os.cp(source, destination)
end

local function copy_tree(source, destination, skip)
    if not os.isdir(source) then
        return false
    end
    mkdir(destination)
    for _, directory in ipairs(os.dirs(path.join(source, "**"))) do
        local relative = path.relative(directory, source)
        local ignored = false
        for _, component in ipairs(path.split(relative)) do
            if skip and skip(component, relative) then
                ignored = true
                break
            end
        end
        if not ignored then
            mkdir(path.join(destination, relative))
        end
    end
    for _, file in ipairs(os.files(path.join(source, "**"))) do
        local relative = path.relative(file, source)
        local ignored = false
        for _, component in ipairs(path.split(relative)) do
            if skip and skip(component, relative) then
                ignored = true
                break
            end
        end
        if not ignored then
            copy_file(file, path.join(destination, relative))
        end
    end
    return true
end

local function copy_optional(source, destination)
    if os.isfile(source) then
        copy_file(source, destination)
    end
end

local function package_files(package_dir, archive)
    local parent = path.directory(archive)
    mkdir(parent)
    -- Keep the package directory itself as the archive root.  The old Python
    -- packager also emitted an empty `.autosave/` marker while excluding its
    -- local contents.
    local staging_parent = path.join(parent, ".RecubinStudio-archive")
    local staging = path.join(staging_parent, "RecubinStudio")
    os.rm(staging_parent)
    mkdir(staging)
    copy_tree(package_dir, staging, function(component)
        return component == ".autosave"
    end)
    mkdir(path.join(staging, ".autosave"))
    os.rm(archive)
    if windows then
        local command = string.format(
            "$ErrorActionPreference='Stop'; Add-Type -AssemblyName System.IO.Compression; " ..
            "Add-Type -AssemblyName System.IO.Compression.FileSystem; " ..
            "$zip=$null; try { $zip=[IO.Compression.ZipFile]::Open('%s',[IO.Compression.ZipArchiveMode]::Create); " ..
            "$root='%s'; Get-ChildItem -LiteralPath $root -Recurse -Force | ForEach-Object { " ..
            "$relative=$_.FullName.Substring('%s'.Length+1).Replace('\\','/'); " ..
            "if ($_.PSIsContainer) { [void]$zip.CreateEntry($relative+'/') } " ..
            "else { [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,$_.FullName,$relative) } } } " ..
            "finally { if ($null -ne $zip) { $zip.Dispose() } }",
            archive:gsub("'", "''"), staging_parent:gsub("'", "''"), staging_parent:gsub("'", "''"))
        local result = os.execv("powershell", {"-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", command})
        if result ~= 0 then
            raise("failed to create Windows package archive")
        end
    else
        local result = os.execv("zip", {"-q", "-r", archive, "RecubinStudio"}, {curdir = staging_parent})
        if result ~= 0 then
            raise("failed to create macOS package archive")
        end
    end
    os.rm(staging_parent)
end

local function prepare_package(package_dir)
    if os.isdir(package_dir) then
        for _, entry in ipairs(os.dirs(path.join(package_dir, "*"))) do
            if path.basename(entry) ~= ".autosave" then
                os.rm(entry)
            end
        end
        for _, file in ipairs(os.files(path.join(package_dir, "*"))) do
            os.rm(file)
        end
    else
        mkdir(package_dir)
    end
    mkdir(path.join(package_dir, ".autosave"))
end

local package_dir = path.join(root, "dist", "RecubinStudio")
local build_dir = windows and path.join(root, "build", config) or path.join(root, "build-mac")
local names = windows and {
    {"Recubin.exe", "Recubin.exe"},
    {"RecubinEngine.exe", "RecubinEngine.exe"},
    {"launcher.exe", "launcher.exe"},
    {"Watcher.exe", "Watcher.exe"},
} or {
    {"Recubin", "Recubin"},
    {"RecubinEngine", "RecubinEngine"},
}
for _, pair in ipairs(names) do
    require_file(path.join(build_dir, pair[1]), pair[1])
end

if windows then
    local redist = path.join(root, "redist", "vc_redist.x64.exe")
    require_file(redist, "Visual C++ Redistributable installer")
end

local shaders_source = path.join(root, "shaders")
local fonts_source = path.join(root, "assets", "fonts")
local logo_source = path.join(root, "assets", "image", "rcbnstudio.png")
if not os.isdir(shaders_source) or #os.files(path.join(shaders_source, "**")) == 0 then
    raise("shader directory is missing or empty: " .. shaders_source)
end
if not os.isdir(fonts_source) or #os.files(path.join(fonts_source, "**")) == 0 then
    raise("font directory is missing or empty: " .. fonts_source)
end
require_file(logo_source, "Studio logo")

-- Validate every required input before deleting or refreshing the destination.
prepare_package(package_dir)
for _, pair in ipairs(names) do
    copy_file(path.join(build_dir, pair[1]), path.join(package_dir, pair[2]))
end
if windows then
    copy_file(path.join(root, "redist", "vc_redist.x64.exe"), path.join(package_dir, "redist", "vc_redist.x64.exe"))
    copy_tree(path.join(root, "dlls"), path.join(package_dir, "dlls"))
end
copy_tree(shaders_source, path.join(package_dir, "shaders"))
copy_tree(fonts_source, path.join(package_dir, "assets", "fonts"))
-- Process SDK headers are consumed by Studio when it builds a Process DLL.
-- Keep them beside the executable so packaged Studio does not depend on CWD.
mkdir(path.join(package_dir, "sdk", "include"))
copy_file(path.join(root, "app", "include", "recubin.h"),
          path.join(package_dir, "sdk", "include", "recubin.h"))
copy_file(path.join(root, "app", "include", "recubin_process.h"),
          path.join(package_dir, "sdk", "include", "recubin_process.h"))
copy_file(path.join(root, "app", "include", "recubin_process_api.h"),
          path.join(package_dir, "sdk", "include", "recubin_process_api.h"))
for _, directory in ipairs({"scenes", "image", "models", "scripts"}) do
    mkdir(path.join(package_dir, "assets", directory))
end
copy_file(logo_source, path.join(package_dir, "assets", "image", "rcbnstudio.png"))
copy_optional(path.join(root, "imgui.ini"), path.join(package_dir, "imgui.ini"))
copy_optional(path.join(root, "LICENCE"), path.join(package_dir, "LICENSE"))
copy_optional(path.join(root, "LICENCE_3RD_PARTY"), path.join(package_dir, "LICENCE_3RD_PARTY"))

local readme = windows and [[Recubin.exeがエディター(スタジオ)なので、それをクリックすれば始められます。
RecubinEngine.exeはランタイム用なので触らずにそのままにしておいてください。

Recubin.exe is the editor (Studio), so you can start by clicking on it.
Recubin.exeが起動しない場合は、先にredist/vc_redist.x64.exeを実行してからRecubin.exeを起動してください。

RecubinEngine.exe is for the runtime, so please leave it as is and do not touch it.
If Recubin.exe does not start, run redist/vc_redist.x64.exe before launching Recubin.exe.
]] or [[Recubin is the editor (Studio). RecubinEngine is the runtime used by the in-editor game Packager. Keep both executables in this folder.
]]
io.writefile(path.join(package_dir, "readme.txt"), readme)

if not windows then
    for _, executable in ipairs({path.join(package_dir, "Recubin"), path.join(package_dir, "RecubinEngine")}) do
        os.execv("codesign", {"--force", "--sign", "-", executable})
        os.execv("codesign", {"--verify", "--strict", executable})
    end
end

local suffix = windows and "" or "-macos"
local archive = path.join(root, "dist", "RecubinStudio" .. suffix .. "-" .. os.date("%Y%m%d") .. ".zip")
package_files(package_dir, archive)
print("[SUCCESS] Packaged studio at " .. package_dir)
print("[SUCCESS] Created archive at " .. archive)

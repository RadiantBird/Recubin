set_project("Recubin")
set_version("0.1.0")
set_languages("cxx23")
add_rules("mode.debug", "mode.release")
if is_plat("windows") then
    set_runtimes("MD")
    add_cxflags("/utf-8", {tools = "cl"})
    add_cxxflags("/permissive-", {tools = "cl"})
end
option("depdir")
    set_default("lib")
    set_showmenu(true)
option_end()

-- Dependencies are prepared by `xmake lua setup.lua`.  xmake itself must not
-- download or build third-party code.
local function dependency_root()
    local configured = get_config("depdir")
    if configured and configured ~= "" then
        return path.absolute(configured)
    end
    return path.absolute("lib")
end

local depdir = dependency_root()
local external_dir = path.absolute("external")
local generated_dir = path.join(os.projectdir(), "build", "generated")

local build_mode = is_mode("debug") and "Debug" or "Release"
local function prepared_dependency_dir()
    if is_plat("windows") and is_arch("x64") then
        return path.join(depdir, "windows", "x64", "msvc", build_mode)
    end
    return path.join(depdir, build_mode)
end

local prepared_dir = prepared_dependency_dir()

local required_artifacts = {
    "box3d.lib", "Recast.lib", "Detour.lib", "glfw3.lib", "glew32s.lib",
    "Luau.Ast.lib", "Luau.Bytecode.lib", "Luau.Compiler.lib", "Luau.VM.lib",
    "Luau.Common.lib", "yaml-cpp.lib", "luar_compiler.dll", "sokol-shdc.exe"
}
rule("recubin.prepare")
    before_build(function (target)
        if not target:is_plat("windows") or not target:is_arch("x64") then
            raise("This dependency setup currently supports Windows x64 only.")
        end
        local manifest = path.join(prepared_dir, "manifest.txt")
        local setup_command = "Run `xmake lua setup.lua " .. build_mode .. "` first."
        local metadata = os.isfile(manifest) and io.readfile(manifest)
        local setup_hash = hash.sha256(path.join(os.projectdir(), "setup.lua"))
        if not metadata or not metadata:startswith("recubin-setup-v4\nready=true\n") or
           not metadata:find("\nconfiguration=" .. build_mode .. "\n", 1, true) or
           not metadata:find("\nsetup_sha256=" .. setup_hash .. "\n", 1, true) then
            return raise("Dependency cache is incomplete or does not match the build configuration: " .. manifest .. ". " .. setup_command)
        end
        local msvc = target:toolchain("msvc")
        local find_tool = import("lib.detect.find_tool")
        local tool = msvc and find_tool("cl", {program = target:tool("cxx"), envs = msvc:runenvs()})
        if not tool or not metadata:find("\ncompiler_sha256=" .. hash.sha256(tool.program) .. "\n", 1, true) then
            raise("Dependency cache was built with a different MSVC compiler. " .. setup_command)
        end
        for _, artifact in ipairs(required_artifacts) do
            local file = path.join(prepared_dir, artifact)
            if not os.isfile(file) or not metadata:find("\n" .. artifact .. "=" .. hash.sha256(file) .. "\n", 1, true) then
                raise("Dependency artifact is missing or differs from the cache manifest: " .. file .. ". " .. setup_command)
            end
        end
        local compiler = path.join(prepared_dir, "sokol-shdc.exe")
        local depend = import("core.project.depend")
        local output_dir = path.join(generated_dir, "shaders")
        os.mkdir(output_dir)
        local inputs = os.files(path.join(os.projectdir(), "shaders", "sokol", "**.glsl"))
        table.insert(inputs, compiler)
        for _, source in ipairs(os.files(path.join(os.projectdir(), "shaders", "sokol", "*.glsl"))) do
            local name = path.basename(source)
            if not name:startswith("_") then
                local output = path.join(output_dir, name .. ".h")
                depend.on_changed(function()
                    os.vrunv(compiler, {"--input", source, "--output", output,
                        "--slang", "glsl410:metal_macos", "--ifdef", "--errfmt", "msvc"})
                end, {files = inputs, dependfile = output .. ".d", changed = not os.isfile(output)})
            end
        end
    end)
rule_end()

local all_sources = {}
for _, source in ipairs(os.files("app/src/**/*.cpp")) do
    table.insert(all_sources, source)
end
for _, source in ipairs(os.files("app/src/**/*.c")) do
    table.insert(all_sources, source)
end

local per_target = {
    [path.normalize("app/src/main.cpp")] = true,
    [path.normalize("app/src/Studio/StudioLaunch.cpp")] = true,
    [path.normalize("app/src/Studio/StudioApplication.cpp")] = true,
    [path.normalize("app/src/game_main.cpp")] = true,
    [path.normalize("app/src/test_main.cpp")] = true,
    [path.normalize("app/src/watcher.cpp")] = true,
    [path.normalize("app/src/Core/User.cpp")] = true,
    [path.normalize("app/src/Core/Renderer.cpp")] = true,
    [path.normalize("app/src/Core/RecubinRuntime.cpp")] = true,
}
local core_sources = {}
for _, source in ipairs(all_sources) do
    local relative = path.normalize(path.relative(path.absolute(source), os.projectdir()))
    local vendor = relative:gsub("\\", "/")
    if not per_target[relative] and
       not relative:find("PhysXPhysicsBackend.cpp", 1, true) and
       not vendor:startswith("app/src/imgui/") and
       not vendor:startswith("app/src/Network/Vendor/enet/") and
       vendor ~= "app/src/ThirdParty/pugixml.cpp" and
       vendor ~= "app/src/ThirdParty/xatlas.cpp" then
        table.insert(core_sources, source)
    end
end

local vendor_sources = {
    "external/imgui/imgui.cpp",
    "external/imgui/imgui_demo.cpp",
    "external/imgui/imgui_draw.cpp",
    "external/imgui/imgui_tables.cpp",
    "external/imgui/imgui_widgets.cpp",
    "external/imgui/backends/imgui_impl_opengl3.cpp",
    "external/modded/ImGui/imgui_impl_glfw.cpp",
    "external/modded/ImGuizmo/ImGuizmo.cpp",
    "external/enet/*.c",
    "external/pugixml/src/pugixml.cpp",
    "external/xatlas/source/xatlas/xatlas.cpp",
}

local include_dirs = {
    "external/modded/ImGuizmo",
    "external/modded/ImGui",
    "external/imgui",
    "external/imgui/backends",
    "external/glew-cmake/include",
    "external/glfw/include",
    "external/enet/include",
    "external/pugixml/src",
    "external/xatlas/source/xatlas",
    "external/cgltf",
    "external/dr_libs",
    "external/miniaudio",
    "external/nanosvg/src",
    "external/stb",
    "external/yaml-cpp/include",
    external_dir,
    "app",
    "app/include",
    "app/include/imgui",
    "app/src",
    "external/box3d/include",
    "external/recastnavigation/Recast/Include",
    "external/recastnavigation/Detour/Include",
    "external/signalsmith-stretch/include",
    "external/signalsmith-linear/include",
    generated_dir,
}
local function configure_compilation(editor_disabled)
    add_includedirs(include_dirs, {public = false})
    add_defines("YAML_CPP_STATIC_DEFINE", "YAML_CPP_STATIC_LIBRARIES", "GLEW_STATIC", "NOMINMAX", "SOKOL_GLCORE")
    if is_plat("windows") then
        add_defines("RECUBIN_NO_LUAU_PRAGMA")
    end
    if editor_disabled then
        add_defines("EDITOR_DISABLED")
    end
end

local function configure_binary(editor_disabled)
    configure_compilation(editor_disabled)
    set_rundir(os.projectdir())
    add_deps("RecubinCore")
    add_linkdirs(prepared_dir)
    add_links("glfw3", "glew32s", "Luau.Ast", "Luau.Bytecode", "Luau.Compiler",
              "Luau.VM", "Luau.Common", "yaml-cpp", "Recast", "Detour", "box3d")
    if is_plat("windows") then
        add_syslinks("opengl32", "user32", "gdi32", "shell32", "ole32", "ws2_32", "winmm")
    elseif is_plat("macosx") then
        add_frameworks("Cocoa", "UniformTypeIdentifiers", "IOKit", "CoreFoundation", "CoreVideo", "OpenGL")
    end
    on_load(function(target)
        local linkdeps = {}
        for _, artifact in ipairs(required_artifacts) do
            if path.extension(artifact) == ".lib" then
                table.insert(linkdeps, path.join(prepared_dir, artifact))
            end
        end
        target:data_set("linkdepfiles", linkdeps)
    end)
    after_build(function(target)
        local target_dir = target:targetdir()
        os.mkdir(path.join(target_dir, "dlls"))
        os.cp(path.join(prepared_dir, "luar_compiler.dll"), path.join(target_dir, "dlls", "luar_compiler.dll"))
    end)
end

target("RecubinCore")
    set_kind("object")
    set_policy("build.fence", true)
    add_rules("recubin.prepare")
    add_files(core_sources)
    add_files(vendor_sources)
    add_files("app/src/Core/TimeStretchNode.cpp", {cxxflags = "/O2"})
    configure_compilation(false)
target_end()

target("Recubin")
    set_kind("binary")
    add_files("app/src/main.cpp", "app/src/Studio/StudioLaunch.cpp", "app/src/Studio/StudioApplication.cpp",
              "app/src/Core/User.cpp", "app/src/Core/Renderer.cpp")
    configure_binary(false)
    if is_plat("windows") then
        add_deps("Watcher", {inherit = false})
    end
target_end()

target("RecubinEngine")
    set_kind("binary")
    add_files("app/src/game_main.cpp", "app/src/Core/User.cpp", "app/src/Core/Renderer.cpp")
    configure_binary(true)
target_end()

target("RecubinTest")
    set_kind("binary")
    add_files("app/src/test_main.cpp", "app/src/Core/User.cpp", "app/src/Core/Renderer.cpp")
    configure_binary(true)
target_end()

target("RecubinRuntime")
    set_kind("static")
    add_files("app/src/Core/User.cpp", "app/src/Core/Renderer.cpp", "app/src/Core/RecubinRuntime.cpp")
    configure_binary(true)
target_end()

if is_plat("windows") then
    target("Watcher")
        set_kind("binary")
        add_files("app/src/watcher.cpp")
        add_includedirs("app/include")
        add_defines("WINVER=0x0600", "_WIN32_WINNT=0x0600")
        add_syslinks("comctl32", "user32")
end

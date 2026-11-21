-- Build the pinned dependencies. The application build only reads lib/.
local root = path.absolute(".")
local external = path.join(root, "external")
local work = path.join(root, "build", "setup")
local runtime = "MultiThreadedDLL"

local function run(program, args, directory)
    local status, detail = os.execv(program, args, {curdir = directory or root})
    if status ~= 0 then
        raise("setup: %s %s failed (%s): %s", program, table.concat(args, " "), tostring(status), detail or "")
    end
end

local function capture(program, args, directory)
    local output = os.iorunv(program, args, {curdir = directory or root})
    return output:trim()
end

local function digest(value)
    return hash.strhash128(value)
end

local function checkout_revisions()
    local revisions = {}
    local pending = {}
    local count = 0
    for _, line in ipairs(capture("git", {"ls-files", "--stage", "--", "external"}):split("\n")) do
        local revision, filename = line:match("^160000 (%x+) 0\t(.+)$")
        if revision then
            local name = filename:sub(#"external/" + 1)
            local directory = path.join(root, filename)
            if not os.exists(path.join(directory, ".git")) then
                table.insert(pending, filename)
            end
            revisions[name] = revision
            count = count + 1
        end
    end
    if count == 0 then
        raise("setup: no external Git submodules are registered")
    end
    if #pending > 0 then
        run("git", table.join({"submodule", "update", "--init", "--"}, pending))
    end
    for name, revision in pairs(revisions) do
        local directory = path.join(external, name)
        if capture("git", {"rev-parse", "HEAD"}, directory) ~= revision or
           capture("git", {"status", "--porcelain", "--ignore-submodules=all"}, directory) ~= "" then
            raise("setup: %s must be clean and at pinned revision %s", directory, revision)
        end
    end
    return revisions
end

local function toolchain()
    local source = path.join(work, "probe-source")
    local output = path.join(work, "probe")
    os.mkdir(source)
    io.writefile(path.join(source, "CMakeLists.txt"), [[
cmake_minimum_required(VERSION 3.26)
project(RecubinDependencyToolchain LANGUAGES C CXX)
if(NOT MSVC)
    message(FATAL_ERROR "Recubin dependency setup requires MSVC")
endif()
file(WRITE "${CMAKE_BINARY_DIR}/toolchain.txt"
    "generator=${CMAKE_GENERATOR}\n"
    "instance=${CMAKE_GENERATOR_INSTANCE}\n"
    "version=${CMAKE_CXX_COMPILER_VERSION}\n"
    "sdk=${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}\n"
    "compiler=${CMAKE_CXX_COMPILER}\n")
]])
    run("cmake", {"-S", source, "-B", output, "-A", "x64", "--fresh"})
    local details = io.readfile(path.join(output, "toolchain.txt"))
    local generator = details:match("generator=([^\r\n]+)")
    local instance = details:match("instance=([^\r\n]+)")
    local compiler = details:match("compiler=([^\r\n]+)")
    if not generator or not instance or not compiler or not os.isfile(compiler) then
        raise("setup: unable to resolve Visual Studio toolchain from %s", output)
    end
    return {
        generator = generator,
        instance = instance,
        compiler = compiler,
        key = digest(details .. hash.sha256(compiler) .. capture("cmake", {"--version"})),
    }
end

local function checksums(directory, outputs)
    local entries = {}
    for _, filename in ipairs(outputs) do
        local file = path.join(directory, filename)
        if not os.isfile(file) then
            return nil
        end
        table.insert(entries, filename .. "=" .. hash.sha256(file))
    end
    return table.concat(entries, "\n") .. "\n"
end

local function cached_build(cache_root, name, inputs, outputs, build)
    local key = digest(table.concat(inputs, "\n"))
    local output = path.join(cache_root, name, key)
    local manifest = path.join(output, "files.txt")
    local current = checksums(output, outputs)
    if current and os.isfile(manifest) and io.readfile(manifest) == current then
        print("setup: reusing " .. name)
        return output
    end

    os.mkdir(output)
    if os.isfile(manifest) then
        os.rm(manifest)
    end
    build(output, path.join(work, name, key))
    local result = checksums(output, outputs)
    if not result then
        raise("setup: missing output from " .. name)
    end
    io.writefile(manifest, result)
    return output
end

local function export_source(checkout, revision, patch, directory)
    local source = path.join(directory, "source")
    local archive = path.join(directory, "source.tar")
    os.mkdir(source)
    run("git", {"archive", "--format=tar", "--output=" .. archive, revision}, checkout)
    run("cmake", {"-E", "tar", "xf", archive}, source)
    run("git", {"init", "--quiet"}, source)
    run("git", {"apply", patch}, source)
    return source
end

local function build_cmake(recipe, configuration, compiler, output, build, revision, patch)
    local source = path.join(external, recipe.name)
    if patch then
        source = export_source(source, revision, patch, build .. "-patched")
    end
    if recipe.name == "box3d" then
        -- Match Recubin's previous target override of Box3D's standalone CRT.
        local wrapper = build .. "-source"
        os.mkdir(wrapper)
        io.writefile(path.join(wrapper, "CMakeLists.txt"), string.format([[
cmake_minimum_required(VERSION 3.26)
project(RecubinBox3D LANGUAGES C CXX)
add_subdirectory([=[%s]=] box3d)
set_property(TARGET box3d PROPERTY MSVC_RUNTIME_LIBRARY "%s")
]], source:gsub("\\", "/"), runtime))
        source = wrapper
    end
    local configure = {"-S", source, "-B", build,
        "-G", compiler.generator, "-A", "x64",
        "-DCMAKE_GENERATOR_INSTANCE=" .. compiler.instance,
        "-DBUILD_SHARED_LIBS=OFF", "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW",
        "-DCMAKE_C_FLAGS_INIT=/utf-8", "-DCMAKE_CXX_FLAGS_INIT=/utf-8",
        "-DCMAKE_MSVC_RUNTIME_LIBRARY=" .. runtime}
    table.join2(configure, recipe.options)
    run("cmake", configure)
    run("cmake", table.join({"--build", build, "--config", configuration, "--target"}, recipe.targets))
    for i, filename in ipairs(recipe.outputs) do
        local built_name = recipe.built_names and recipe.built_names[configuration] or filename
        local matches = os.files(path.join(build, "**", built_name))
        if #matches ~= 1 then
            raise("setup: expected one %s artifact from %s, found %d", built_name, recipe.name, #matches)
        end
        os.cp(matches[1], path.join(output, filename))
    end
end

local recipes = {
    {name = "box3d", targets = {"box3d"}, outputs = {"box3d.lib"},
        built_names = {Release = "box3d.lib", Debug = "box3dd.lib"},
        options = {"-DBOX3D_SAMPLES=OFF", "-DBOX3D_UNIT_TESTS=OFF", "-DBOX3D_DOCS=OFF",
            "-DBOX3D_BENCHMARKS=OFF", "-DBOX3D_PROFILE=OFF", "-DBOX3D_SANITIZE=OFF", "-DBOX3D_VALIDATE=OFF"}},
    {name = "recastnavigation", targets = {"Recast", "Detour"}, outputs = {"Recast.lib", "Detour.lib"},
        options = {"-DRECASTNAVIGATION_DEMO=OFF", "-DRECASTNAVIGATION_TESTS=OFF", "-DRECASTNAVIGATION_EXAMPLES=OFF"}},
    {name = "glfw", targets = {"glfw"}, outputs = {"glfw3.lib"},
        options = {"-DGLFW_BUILD_EXAMPLES=OFF", "-DGLFW_BUILD_TESTS=OFF", "-DGLFW_BUILD_DOCS=OFF", "-DUSE_MSVC_RUNTIME_LIBRARY_DLL=ON"}},
    {name = "glew-cmake", targets = {"libglew_static"}, outputs = {"glew32s.lib"},
        built_names = {Release = "glew.lib", Debug = "glewd.lib"},
        options = {"-Dglew-cmake_BUILD_SHARED=OFF", "-Dglew-cmake_BUILD_STATIC=ON", "-DONLY_LIBS=ON"}},
    {name = "luau", targets = {"Luau.Compiler", "Luau.VM"},
        patch = path.join(external, "modded", "Luau", "table-deepcopy.patch"),
        outputs = {"Luau.Ast.lib", "Luau.Bytecode.lib", "Luau.Compiler.lib", "Luau.VM.lib", "Luau.Common.lib"},
        options = {"-DLUAU_BUILD_CLI=OFF", "-DLUAU_BUILD_TESTS=OFF", "-DLUAU_STATIC_CRT=OFF"}},
    {name = "yaml-cpp", targets = {"yaml-cpp"}, outputs = {"yaml-cpp.lib"},
        built_names = {Release = "yaml-cpp.lib", Debug = "yaml-cppd.lib"},
        options = {"-DYAML_CPP_BUILD_TESTS=OFF", "-DYAML_CPP_BUILD_TOOLS=OFF", "-DYAML_CPP_BUILD_CONTRIB=OFF"}},
}

local function build_luar(revision, patch, output, directory)
    local checkout = path.join(external, "Luar Programming Language")
    local source = export_source(checkout, revision, patch, directory)
    local manifest = path.join(source, "luar-rs", "Cargo.toml")
    local artifacts = path.join(directory, "cargo")
    local triple = "x86_64-pc-windows-msvc"
    run("cargo", {"build", "--release", "--locked", "--target", triple,
        "--target-dir", artifacts, "--manifest-path", manifest})
    os.cp(path.join(artifacts, triple, "release", "luar_rs.dll"), path.join(output, "luar_compiler.dll"))
end

function main(configuration)
    configuration = configuration or "Release"
    if configuration ~= "Release" and configuration ~= "Debug" then
        raise("usage: xmake lua setup.lua [Debug|Release]")
    end
    if os.host() ~= "windows" or os.arch() ~= "x64" then
        raise("setup: this dependency builder currently supports Windows x64 only")
    end

    local revisions = checkout_revisions()
    local compiler = toolchain()
    local script_key = hash.sha256(path.join(root, "setup.lua"))
    local cache_root = path.join(root, "lib", "_recipes", "windows", "x64", "msvc")
    local destination = path.join(root, "lib", "windows", "x64", "msvc", configuration)
    local manifest = path.join(destination, "manifest.txt")
    if os.isfile(manifest) then
        os.rm(manifest)
    end
    os.mkdir(destination)
    local all_outputs = {}
    local function publish(directory, outputs)
        for _, name in ipairs(outputs) do
            local source = path.join(directory, name)
            local target = path.join(destination, name)
            if not os.isfile(target) or hash.sha256(target) ~= hash.sha256(source) then
                os.cp(source, target)
            end
            table.insert(all_outputs, name)
        end
    end
    for _, recipe in ipairs(recipes) do
        local outputs = recipe.outputs
        local revision = assert(revisions[recipe.name])
        local inputs = {revision, compiler.key, runtime, configuration, script_key}
        if recipe.patch then
            table.insert(inputs, hash.sha256(recipe.patch))
        end
        local built = cached_build(cache_root, recipe.name,
            inputs,
            outputs, function(output, build)
                build_cmake(recipe, configuration, compiler, output, build, revision, recipe.patch)
            end)
        publish(built, outputs)
    end

    local patch = path.join(external, "modded", "Luar", "recubin.patch")
    local luar_revision = assert(revisions["Luar Programming Language"])
    local luar_outputs = {"luar_compiler.dll"}
    local luar = cached_build(cache_root, "luar",
        {luar_revision, hash.sha256(patch), compiler.key, capture("cargo", {"--version"}),
            capture("rustc", {"-vV"}), script_key},
        luar_outputs, function(output, build)
            build_luar(luar_revision, patch, output, build)
        end)
    publish(luar, luar_outputs)

    local shader_script = path.join(root, "tools", "setup-shdc.lua")
    local shader_outputs = {"sokol-shdc.exe"}
    local shader = cached_build(cache_root, "sokol-shdc",
        {assert(revisions["sokol-tools"]), assert(revisions.sokol), compiler.key, hash.sha256(shader_script)},
        shader_outputs, function(output)
            run("xmake", {"lua", shader_script, path.join(output, "sokol-shdc.exe")})
        end)
    publish(shader, shader_outputs)

    local summary = {"recubin-setup-v4", "ready=true", "configuration=" .. configuration,
        "compiler_sha256=" .. hash.sha256(compiler.compiler), "setup_sha256=" .. script_key}
    local sources = {}
    for name, revision in pairs(revisions) do
        table.insert(sources, "source=" .. name .. " " .. revision)
    end
    table.sort(sources)
    table.join2(summary, sources)
    table.insert(summary, assert(checksums(destination, all_outputs)))
    local pending = manifest .. ".tmp"
    io.writefile(pending, table.concat(summary, "\n"))
    os.mv(pending, manifest)
    print("setup: dependency cache is ready at " .. destination)
end

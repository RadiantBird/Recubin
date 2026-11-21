-- Stage Windows dependencies for SDL GPU and custom PostEffect checks.
local root = path.absolute(".")
local work = path.join(root, "build", "setup")
local GLSLANG_VERSION = "15.4.0"

local function run(program, args, directory)
    local status, detail = os.execv(program, args, {curdir = directory or root})
    if status ~= 0 then
        raise("setup: %s %s failed (%s): %s", program, table.concat(args, " "), tostring(status), detail or "")
    end
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

local function gpu_archive(filename, url, expected)
    local archive = path.join(root, "build", filename)
    if not os.isfile(archive) then
        os.mkdir(path.directory(archive))
        local pending = archive .. ".download"
        run("curl.exe", {"--fail", "--location", "--silent", "--show-error", "--output", pending, url})
        if hash.sha256(pending) ~= expected then
            raise("setup gpu: unexpected SHA256 for " .. pending)
        end
        os.mv(pending, archive)
    end
    if hash.sha256(archive) ~= expected then
        raise("setup gpu: unexpected SHA256 for " .. archive)
    end
    return archive
end

local function setup_glslang(destination)
    local archive = gpu_archive("glslang-" .. GLSLANG_VERSION .. ".tar.gz",
        "https://github.com/KhronosGroup/glslang/archive/refs/tags/" .. GLSLANG_VERSION .. ".tar.gz",
        "b16c78e7604b9be9f546ee35ad8b6db6f39bbbbfb19e8d038b6fe2ea5bba4ff4")
    local gpuWork = path.join(work, "gpu")
    local source = path.join(gpuWork, "glslang-" .. GLSLANG_VERSION)
    local output = path.join(gpuWork, "glslang-" .. GLSLANG_VERSION .. "-windows-x64")
    run("cmake", {"-E", "tar", "xf", archive}, gpuWork)
    run("cmake", {"-S", source, "-B", output, "-A", "x64",
        "-DBUILD_SHARED_LIBS=ON", "-DENABLE_OPT=OFF", "-DENABLE_HLSL=OFF",
        "-DENABLE_GLSLANG_BINARIES=OFF", "-DGLSLANG_TESTS=OFF", "-DGLSLANG_ENABLE_INSTALL=ON"})
    run("cmake", {"--build", output, "--config", "Release", "--parallel", "6"})
    run("cmake", {"--install", output, "--config", "Release", "--prefix", path.join(destination, "glslang")})
end

function main()
    if os.host() ~= "windows" or os.arch() ~= "x64" then
        raise("setup gpu: this dependency recipe supports Windows x64")
    end
    local sdkHash = "4489ba0070b8ce047b8717e4b71fa7e35ac71362e7a6bb2a90a7a27f624b3d5d"
    local dxcHash = "9ad895a6b039e3a8f8c22a1009f866800b840a74b50db9218d13319e215ea8a4"
    local sdkArchive = gpu_archive("SDL3-devel-3.4.0-VC.zip",
        "https://github.com/libsdl-org/SDL/releases/download/release-3.4.0/SDL3-devel-3.4.0-VC.zip", sdkHash)
    local dxcArchive = gpu_archive("dxc_2025_07_14.zip",
        "https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.8.2505.1/dxc_2025_07_14.zip", dxcHash)
    local destination = path.join(root, "lib", "gpu", "windows", "x64")
    local staging = path.join(work, "gpu", "SDL3-3.4.0")
    local dxcStaging = path.join(work, "gpu", "dxc")
    os.mkdir(staging)
    os.mkdir(dxcStaging)
    run("cmake", {"-E", "tar", "xf", sdkArchive}, path.join(work, "gpu"))
    run("cmake", {"-E", "tar", "xf", dxcArchive}, dxcStaging)
    os.mkdir(destination)
    os.cp(path.join(staging, "include"), path.join(destination, "include"))
    os.cp(path.join(staging, "lib", "x64", "SDL3.lib"), path.join(destination, "SDL3.lib"))
    os.cp(path.join(staging, "lib", "x64", "SDL3.dll"), path.join(destination, "SDL3.dll"))
    os.cp(path.join(dxcStaging, "bin", "x64"), path.join(destination, "dxc"))
    setup_glslang(destination)
    local manifest = path.join(destination, "manifest.txt")
    local pending = manifest .. ".tmp"
    io.writefile(pending, table.concat({
        "recubin-gpu-setup-v1", "sdl_version=3.4.0", "sdl_sha256=" .. sdkHash,
        "dxc_version=2025-07-14", "dxc_sha256=" .. dxcHash, "glslang_version=" .. GLSLANG_VERSION, "ready=true",
        assert(checksums(destination, {"include/SDL3/SDL.h", "SDL3.lib", "SDL3.dll",
            "dxc/dxc.exe", "dxc/dxcompiler.dll", "dxc/dxil.dll",
            "glslang/include/glslang/Include/glslang_c_interface.h",
            "glslang/lib/glslang.lib", "glslang/lib/glslang-default-resource-limits.lib",
            "glslang/bin/glslang.dll", "glslang/bin/glslang-default-resource-limits.dll"})),
    }, "\n"))
    os.mv(pending, manifest)
    print("setup gpu: installed SDL3, DXC and glslang " .. GLSLANG_VERSION .. " at " .. destination)
end

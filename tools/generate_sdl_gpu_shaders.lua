-- Generate Windows SDL GPU shader code from a shared HLSL vertex/fragment source.

local function compile(dxc, source, output, entry, profile, spirv)
    local args = {"-T", profile, "-E", entry, "-Fo", output, source}
    if spirv then
        table.insert(args, 1, "-spirv")
        table.insert(args, 2, "-fspv-target-env=vulkan1.0")
    end
    local status, detail = os.execv(dxc, args)
    if status ~= 0 then raise("DXC failed for %s: %s", source, detail or "") end
end

local function bytes(filename)
    local data = io.readfile(filename, {encoding = "binary"})
    local values = {}
    for i = 1, #data do values[#values + 1] = string.byte(data, i) end
    return values
end

local function declaration(name, values)
    local lines = {"alignas(4) inline constexpr std::uint8_t " .. name .. "[] = {"}
    local row = {}
    for i, value in ipairs(values) do
        row[#row + 1] = tostring(value)
        if #row == 16 or i == #values then
            lines[#lines + 1] = "    " .. table.concat(row, ", ") .. ","
            row = {}
        end
    end
    lines[#lines + 1] = "};"
    return table.concat(lines, "\n")
end

function main(dxc, source, output, prefix, stage, msl_converter)
    if not output or not source or not dxc or not prefix or not msl_converter or msl_converter == "" then
        raise("usage: xmake lua tools/generate_sdl_gpu_shaders.lua <dxc> <hlsl> <output> <prefix> <stage> <spirv-to-msl>")
    end
    local work = path.join(path.directory(output), "." .. prefix)
    os.mkdir(work)
    local vsSpirv, fsSpirv = path.join(work, "vertex.spv"), path.join(work, "fragment.spv")
    local vsDxil, fsDxil = path.join(work, "vertex.dxil"), path.join(work, "fragment.dxil")
    local results = {}
    local function convert_msl(input, name, shader_stage)
        local msl = path.join(work, name .. ".msl")
        local status, detail = os.execv(msl_converter, {shader_stage, input, msl})
        if status ~= 0 then raise("SPIRV-Cross MSL conversion failed: %s", detail or "") end
        results[#results + 1] = declaration(name .. "_msl", bytes(msl))
    end
    if stage ~= "vertex" then
        compile(dxc, source, fsSpirv, "PSMain", "ps_6_0", true)
        compile(dxc, source, fsDxil, "PSMain", "ps_6_0", false)
        results[#results + 1] = declaration(prefix .. "_fragment_spirv", bytes(fsSpirv))
        results[#results + 1] = declaration(prefix .. "_fragment_dxil", bytes(fsDxil))
        convert_msl(fsSpirv, prefix .. "_fragment", "fragment")
    end
    if stage ~= "fragment" then
        compile(dxc, source, vsSpirv, "VSMain", "vs_6_0", true)
        compile(dxc, source, vsDxil, "VSMain", "vs_6_0", false)
        results[#results + 1] = declaration(prefix .. "_vertex_spirv", bytes(vsSpirv))
        results[#results + 1] = declaration(prefix .. "_vertex_dxil", bytes(vsDxil))
        convert_msl(vsSpirv, prefix .. "_vertex", "vertex")
    end
    os.mkdir(path.directory(output))
    io.writefile(output, "#pragma once\n#include <cstddef>\n#include <cstdint>\n\n" ..
        table.concat(results, "\n") .. "\n")
end

-- Generate shader code for the Windows SDL GPU smoke test.

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

function main(dxc, source, output)
    if not output or not source or not dxc then
        raise("usage: xmake lua tools/generate_gpu_smoke_shaders.lua <dxc> <hlsl> <output>")
    end
    local work = path.join(path.directory(output), ".gpu-smoke")
    os.mkdir(work)
    local vsSpirv, fsSpirv = path.join(work, "vertex.spv"), path.join(work, "fragment.spv")
    local vsDxil, fsDxil = path.join(work, "vertex.dxil"), path.join(work, "fragment.dxil")
    compile(dxc, source, vsSpirv, "VSMain", "vs_6_0", true)
    compile(dxc, source, fsSpirv, "PSMain", "ps_6_0", true)
    compile(dxc, source, vsDxil, "VSMain", "vs_6_0", false)
    compile(dxc, source, fsDxil, "PSMain", "ps_6_0", false)
    os.mkdir(path.directory(output))
    io.writefile(output, "#pragma once\n#include <cstddef>\n#include <cstdint>\n\n" ..
        declaration("gpu_smoke_vertex_spirv", bytes(vsSpirv)) .. "\n" ..
        declaration("gpu_smoke_fragment_spirv", bytes(fsSpirv)) .. "\n" ..
        declaration("gpu_smoke_vertex_dxil", bytes(vsDxil)) .. "\n" ..
        declaration("gpu_smoke_fragment_dxil", bytes(fsDxil)) .. "\n")
end

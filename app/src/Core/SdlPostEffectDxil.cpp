#include "Core/SdlPostEffectDxil.hpp"

#include <filesystem>
#include <exception>
#include <sstream>

#ifdef _WIN32
#include <Windows.h>
#include <dxcapi.h>
#include <wrl/client.h>

#include "spirv_cross.hpp"
#include "spirv_hlsl.hpp"
#endif

namespace
{
#ifdef _WIN32
using Microsoft::WRL::ComPtr;

class LibraryHandle
{
public:
    explicit LibraryHandle(HMODULE handle) : m_handle(handle) {}
    ~LibraryHandle()
    {
        if (m_handle != nullptr)
        {
            FreeLibrary(m_handle);
        }
    }

    LibraryHandle(const LibraryHandle&) = delete;
    LibraryHandle& operator=(const LibraryHandle&) = delete;

private:
    HMODULE m_handle = nullptr;
};

std::filesystem::path executableDirectory(std::string& error)
{
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;)
    {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0)
        {
            error = "SdlPostEffectDxil: GetModuleFileNameW failed (error " + std::to_string(GetLastError()) + ")";
            return {};
        }
        if (length < buffer.size() - 1)
        {
            return std::filesystem::path(buffer.data(), buffer.data() + length).parent_path();
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::string blobText(IDxcBlob* blob)
{
    if (blob == nullptr || blob->GetBufferPointer() == nullptr || blob->GetBufferSize() == 0)
    {
        return {};
    }
    return std::string(static_cast<const char*>(blob->GetBufferPointer()), blob->GetBufferSize());
}
#endif
}

bool compileSdlPostEffectDxil(std::span<const std::uint32_t> spirv,
                              std::vector<std::uint8_t>& dxil,
                              std::string& error)
{
    dxil.clear();
    error.clear();

    if (spirv.empty())
    {
        error = "SdlPostEffectDxil: SPIR-V module is empty";
        return false;
    }

#ifndef _WIN32
    error = "SdlPostEffectDxil: DXIL compilation is only available on Windows";
    return false;
#else
    std::string pathError;
    const std::filesystem::path dllPath = executableDirectory(pathError) / "dxcompiler.dll";
    if (!pathError.empty())
    {
        error = pathError;
        return false;
    }

    const HMODULE module = LoadLibraryW(dllPath.c_str());
    if (module == nullptr)
    {
        error = "SdlPostEffectDxil: unable to load " + dllPath.string() + " (error " +
                std::to_string(GetLastError()) + ")";
        return false;
    }
    LibraryHandle library(module);

    const auto createInstance = reinterpret_cast<DxcCreateInstanceProc>(
        GetProcAddress(module, "DxcCreateInstance"));
    if (createInstance == nullptr)
    {
        error = "SdlPostEffectDxil: dxcompiler.dll does not export DxcCreateInstance (" +
                dllPath.string() + ")";
        return false;
    }

    try
    {
        spirv_cross::CompilerHLSL compiler(spirv.data(), spirv.size());

        bool foundFragment = false;
        for (const spirv_cross::EntryPoint& entryPoint : compiler.get_entry_points_and_stages())
        {
            if (entryPoint.execution_model == spv::ExecutionModelFragment)
            {
                compiler.set_entry_point(entryPoint.name, entryPoint.execution_model);
                foundFragment = true;
                break;
            }
        }
        if (!foundFragment)
        {
            error = "SdlPostEffectDxil: SPIR-V module has no fragment entry point";
            return false;
        }

        // SDL_GPU's fragment-stage convention reserves set 2 for textures/samplers
        // and set 3 for uniform buffers. Keep those sets explicit in generated HLSL.
        const spirv_cross::ShaderResources resources = compiler.get_shader_resources();
        const auto setDescriptorSet = [&compiler](const auto& resource, const std::uint32_t set) {
            compiler.set_decoration(resource.id, spv::DecorationDescriptorSet, set);
        };
        for (const auto& resource : resources.sampled_images)
        {
            setDescriptorSet(resource, 2);
        }
        for (const auto& resource : resources.separate_images)
        {
            setDescriptorSet(resource, 2);
        }
        for (const auto& resource : resources.separate_samplers)
        {
            setDescriptorSet(resource, 2);
        }
        for (const auto& resource : resources.uniform_buffers)
        {
            setDescriptorSet(resource, 3);
        }

        auto options = compiler.get_hlsl_options();
        options.shader_model = 60;
        options.use_entry_point_name = false;
        compiler.set_hlsl_options(options);
        const std::string hlsl = compiler.compile();

        ComPtr<IDxcCompiler3> dxcCompiler;
        HRESULT result = createInstance(CLSID_DxcCompiler, __uuidof(IDxcCompiler3),
                                         reinterpret_cast<void**>(dxcCompiler.GetAddressOf()));
        if (FAILED(result))
        {
            error = "SdlPostEffectDxil: DxcCreateInstance(IDxcCompiler3) failed (HRESULT 0x";
            std::ostringstream stream;
            stream << std::hex << static_cast<unsigned long>(result);
            error += stream.str() + ")";
            return false;
        }

        DxcBuffer source{};
        source.Ptr = hlsl.data();
        source.Size = hlsl.size();
        source.Encoding = DXC_CP_UTF8;
        LPCWSTR arguments[] = {L"-E", L"main", L"-T", L"ps_6_0", L"-HV", L"2021", L"-Zpr"};

        ComPtr<IDxcResult> compileResult;
        result = dxcCompiler->Compile(&source, arguments,
                                      static_cast<UINT32>(sizeof(arguments) / sizeof(arguments[0])),
                                      nullptr, __uuidof(IDxcResult),
                                      reinterpret_cast<void**>(compileResult.GetAddressOf()));
        if (FAILED(result) || compileResult == nullptr)
        {
            error = "SdlPostEffectDxil: IDxcCompiler3::Compile failed (HRESULT 0x";
            std::ostringstream stream;
            stream << std::hex << static_cast<unsigned long>(result);
            error += stream.str() + ")";
            return false;
        }

        ComPtr<IDxcBlob> diagnostics;
        compileResult->GetOutput(DXC_OUT_ERRORS, __uuidof(IDxcBlob),
                                 reinterpret_cast<void**>(diagnostics.GetAddressOf()), nullptr);
        const std::string diagnosticText = blobText(diagnostics.Get());

        HRESULT status = E_FAIL;
        compileResult->GetStatus(&status);
        if (FAILED(status))
        {
            error = "SdlPostEffectDxil: DXC rejected generated HLSL";
            if (!diagnosticText.empty())
            {
                error += ":\n" + diagnosticText;
            }
            return false;
        }

        ComPtr<IDxcBlob> object;
        result = compileResult->GetOutput(DXC_OUT_OBJECT, __uuidof(IDxcBlob),
                                           reinterpret_cast<void**>(object.GetAddressOf()), nullptr);
        if (FAILED(result) || object == nullptr || object->GetBufferPointer() == nullptr || object->GetBufferSize() == 0)
        {
            error = "SdlPostEffectDxil: DXC produced no DXIL object";
            if (!diagnosticText.empty())
            {
                error += ":\n" + diagnosticText;
            }
            return false;
        }

        const auto* bytes = static_cast<const std::uint8_t*>(object->GetBufferPointer());
        dxil.assign(bytes, bytes + object->GetBufferSize());
        return true;
    }
    catch (const spirv_cross::CompilerError& exception)
    {
        error = "SdlPostEffectDxil: SPIRV-Cross failed: ";
        error += exception.what();
        return false;
    }
    catch (const std::exception& exception)
    {
        error = "SdlPostEffectDxil: shader conversion failed: ";
        error += exception.what();
        return false;
    }
#endif
}

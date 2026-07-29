#include "include/Core/LuarCompiler.hpp"
#include <Util/Platform.hpp>
#include <Util/IPlatform.hpp>
#include <iostream>
#include <vector>

static constexpr size_t OUT_BUF_SIZE = 1024 * 1024; // 1 MB

LuarCompiler::LuarCompiler() {
    m_dll = getPlatform().loadDynamicLibrary("luar_compiler.dll");
    if (!m_dll) {
        std::cerr << "[LuarCompiler] Failed to load luar_compiler.dll\n";
        return;
    }
    m_fnCompile = reinterpret_cast<FnCompile>(getPlatform().getSymbol(m_dll, "luar_compile"));
    m_fnCompileWithPath = reinterpret_cast<FnCompileWithPath>(getPlatform().getSymbol(m_dll, "luar_compile_with_path"));
    m_fnGetErrors = reinterpret_cast<FnGetErrors>(getPlatform().getSymbol(m_dll, "luar_get_errors"));
    if (!m_fnCompile || !m_fnGetErrors) {
        std::cerr << "[LuarCompiler] Missing exports in luar_compiler.dll\n";
        getPlatform().freeDynamicLibrary(m_dll);
        m_dll = nullptr;
    }
}

LuarCompiler::~LuarCompiler() {
    if (m_dll) getPlatform().freeDynamicLibrary(m_dll);
}

std::string LuarCompiler::compile(const std::string& luarSource) {
    if (!m_dll) return {};

    std::vector<char> buf(OUT_BUF_SIZE, '\0');
    int result = m_fnCompile(luarSource.c_str(), buf.data(), OUT_BUF_SIZE);
    if (result != 0) {
        char errBuf[4096] = {};
        m_fnGetErrors(errBuf, sizeof(errBuf));
        std::cerr << "[LuarCompiler] Compile error:\n" << errBuf << "\n";
        return {};
    }
    return std::string(buf.data());
}

std::string LuarCompiler::compile(const std::string& luarSource, const std::string& sourcePath) {
    if (!m_dll) return {};
    if (!m_fnCompileWithPath) {
        return compile(luarSource);
    }

    std::vector<char> buf(OUT_BUF_SIZE, '\0');
    int result = m_fnCompileWithPath(luarSource.c_str(), sourcePath.c_str(), buf.data(), OUT_BUF_SIZE);
    if (result != 0) {
        char errBuf[4096] = {};
        m_fnGetErrors(errBuf, sizeof(errBuf));
        std::cerr << "[LuarCompiler] Compile error:\n" << errBuf << "\n";
        return {};
    }
    return std::string(buf.data());
}

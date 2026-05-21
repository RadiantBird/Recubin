#include "include/Core/LuarCompiler.hpp"
#include <iostream>
#include <vector>

static constexpr size_t OUT_BUF_SIZE = 1024 * 1024; // 1 MB

LuarCompiler::LuarCompiler() {
    m_dll = LoadLibraryA("luar_compiler.dll");
    if (!m_dll) {
        std::cerr << "[LuarCompiler] Failed to load luar_compiler.dll (error " << GetLastError() << ")\n";
        return;
    }
    m_fnCompile   = reinterpret_cast<FnCompile>  (GetProcAddress(m_dll, "luar_compile"));
    m_fnGetErrors = reinterpret_cast<FnGetErrors>(GetProcAddress(m_dll, "luar_get_errors"));
    if (!m_fnCompile || !m_fnGetErrors) {
        std::cerr << "[LuarCompiler] Missing exports in luar_compiler.dll\n";
        FreeLibrary(m_dll);
        m_dll = nullptr;
    }
}

LuarCompiler::~LuarCompiler() {
    if (m_dll) FreeLibrary(m_dll);
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

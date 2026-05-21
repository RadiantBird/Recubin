#pragma once
#include <string>
#include <windows.h>

// Luar→Luauトランスパイラ DLLのラッパー
// DLL: luar_compiler.dll (Rust製, luar-rsクレート)
class LuarCompiler {
public:
    LuarCompiler();
    ~LuarCompiler();

    // .luarソース → Luauソース。失敗時は空文字列を返す
    std::string compile(const std::string& luarSource);

    // DLLのロードに成功しているか
    bool isLoaded() const { return m_dll != nullptr; }

private:
    HMODULE m_dll = nullptr;

    using FnCompile   = int(*)(const char* src, char* out_buf, size_t out_len);
    using FnGetErrors = int(*)(char* buf, size_t buf_len);

    FnCompile   m_fnCompile   = nullptr;
    FnGetErrors m_fnGetErrors = nullptr;
};

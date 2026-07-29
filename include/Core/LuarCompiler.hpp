#pragma once
#include <cstddef>
#include <string>

// Luar→Luauトランスパイラ DLLのラッパー
// DLL: luar_compiler.dll (Rust製, luar-rsクレート)
class LuarCompiler {
public:
    LuarCompiler();
    ~LuarCompiler();

    // .luarソース → Luauソース。失敗時は空文字列を返す
    std::string compile(const std::string& luarSource);

    // importを解決するため、.luarファイルのパスを指定してコンパイルする
    std::string compile(const std::string& luarSource, const std::string& sourcePath);

    // DLLのロードに成功しているか
    bool isLoaded() const { return m_dll != nullptr; }

private:
    void* m_dll = nullptr;

    using FnCompile         = int(*)(const char* src, char* out_buf, size_t out_len);
    using FnCompileWithPath = int(*)(const char* src, const char* source_path, char* out_buf, size_t out_len);
    using FnGetErrors       = int(*)(char* buf, size_t buf_len);

    FnCompile         m_fnCompile         = nullptr;
    FnCompileWithPath m_fnCompileWithPath = nullptr;
    FnGetErrors       m_fnGetErrors       = nullptr;
};

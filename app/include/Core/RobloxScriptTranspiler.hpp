#pragma once

#include <string>
#include <vector>

namespace RobloxScriptTranspiler {

struct Result {
    std::string source;
    std::vector<std::string> warnings;
};

// Roblox の Luau ソースコードを解析し、Recubin の基本 API へ変換する。
// コメントや文字列リテラルを誤置換しないよう Luau AST の Location を用いて局所置換を行う。
Result transpile(const std::string& source);

} // namespace RobloxScriptTranspiler

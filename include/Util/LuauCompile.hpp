#pragma once
#include <cstddef>
#include <string>
#include <vector>

// Luauスクリプトのコンパイル共通処理（LuauEngineとPackagerで共有する）。
//
// `workspace.Gravity` のように「グローバルのフィールド読み取り」は、既定のコンパイルだと
// ロード時に一度だけ解決されて定数化(import最適化)され、以後__indexが呼ばれず値が古いままになる。
// エンジンがInstanceを公開するグローバルはmutableGlobalsに登録し、この最適化を無効にする。
namespace LuauCompile {

// エンジンが常にInstanceを公開するグローバル名。
const std::vector<std::string>& instanceGlobals();

// luau_compileと同じ戻り値（失敗時は先頭バイト0+エラー文。free()で解放する）。
// extraMutableGlobalsにはsetGlobalInstanceで登録した任意名のグローバルを渡す。
char* compile(const std::string& source, size_t& bytecodeSize,
              const std::vector<std::string>& extraMutableGlobals = {});

} // namespace LuauCompile

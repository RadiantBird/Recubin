#pragma once
#include <string>

// Windowsのファイルを開くダイアログを表示し、選択されたパスを返す（キャンセル時は空文字列）
std::string browseFile(const wchar_t* filterName, const wchar_t* filterSpec);

// Program IPC サンプル用の子プロセス。
//
// Recubin の Program インスタンスは、この exe を起動して標準入出力パイプで文字列をやり取りする。
//   - 要求は標準入力から 1 行 = 1 メッセージで届く。
//   - 応答は標準出力へ 1 行 = 1 メッセージで返す（必ず 1 要求につき 1 行。改行ごとに flush する）。
//   - メッセージ中の改行は「\n」、バックスラッシュは「\\」、CR は「\r」にエスケープされて届く。
//     このサンプルは中身を解釈せず、エスケープされたままの文字列を扱う（エコーしても壊れない）。
//   - 標準入力が閉じられたら（Luau の Program:Close）終了する。終了コードは処理した要求数。
//
// 対応コマンド（先頭の単語で判別。大文字小文字は区別する）:
//   ping            -> pong
//   echo <text>     -> <text>
//   upper <text>    -> <text> を大文字にしたもの（ASCII のみ）
//   add <a> <b>     -> a + b
//   count           -> ここまでに処理した要求数（この要求は含まない）
//   slow            -> 2 秒待ってから "slow done"（Program:Call のタイムアウト確認用）
//   その他          -> error: unknown command "<line>"
//
// ビルド: 同じフォルダの build.bat（Visual Studio の開発者コマンドプロンプトから）。

#include <chrono>
#include <cctype>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

namespace {

std::string toUpperAscii(std::string text) {
    for (char& c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return text;
}

// "word rest of line" を word と rest に分ける。
void splitCommand(const std::string& line, std::string& word, std::string& rest) {
    const size_t space = line.find(' ');
    if (space == std::string::npos) {
        word = line;
        rest.clear();
        return;
    }
    word = line.substr(0, space);
    rest = line.substr(space + 1);
}

std::string handle(const std::string& line, int handledSoFar) {
    std::string word, rest;
    splitCommand(line, word, rest);

    if (word == "ping") return "pong";
    if (word == "echo") return rest;
    if (word == "upper") return toUpperAscii(rest);
    if (word == "add") {
        std::istringstream numbers(rest);
        double a = 0.0, b = 0.0;
        if (!(numbers >> a >> b)) return "error: usage: add <a> <b>";
        std::ostringstream out;
        out << (a + b);
        return out.str();
    }
    if (word == "count") return std::to_string(handledSoFar);
    if (word == "slow") {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        return "slow done";
    }
    return "error: unknown command \"" + line + "\"";
}

} // namespace

int main() {
    std::string line;
    int handled = 0;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string reply = handle(line, handled);
        ++handled;
        // 応答は必ず 1 行。末尾の改行で区切り、すぐに flush する。
        std::cout << reply << '\n' << std::flush;
    }
    // 標準入力が閉じた = Program:Close。終了コードとして処理数を返す。
    return handled;
}

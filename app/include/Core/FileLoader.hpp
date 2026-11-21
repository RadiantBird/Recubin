#pragma once
#include <string>
#include <vector>

/**
 * @brief ファイル読み込みを統合して管理する静的クラス
 */
class FileLoader {
public:
    /**
     * @brief テキストファイル（YAML, GLSL, Luauなど）をstringとして読み込む
     */
    static std::string readText(const std::string& filePath);

    /**
     * @brief バイナリファイルを読み込む
     */
    static std::vector<char> readBinary(const std::string& filePath);
};

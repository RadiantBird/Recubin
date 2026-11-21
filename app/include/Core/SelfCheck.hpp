#pragma once
#include <cstdint>
#include <string>
#include <vector>

class Instance;

// パッケージ前後でシーンが壊れていないかを比べるための自己チェック用ユーティリティ。
// RecubinEngine の --self-check（ツリー・画面の書き出し）と、RecubinTest の
// --package-integrity（パッケージ前後の比較）で共有する。
namespace SelfCheck {

// インスタンスツリーをシーンと同じYAML形で書き出す。ファイル参照（AssetDependencies の
// file/sequence/directory キー）は、パッケージでパスが書き換わっても比べられるよう、
// 現在の作業ディレクトリ基準で読んだ内容のハッシュ（"sha256:<hex>"）に置き換える。
// 読めないファイルは "missing:<元のパス>"、スクリプトはコンパイルで中身が変わるため
// "script:present" / "missing:<元のパス>" にする。
std::string describeTree(Instance& root);

struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;  // 行は下から上（glReadPixels の並び）
};

// 生のRGBA（先頭に幅・高さ）として保存・読み込みする（比較用）
bool writeRawImage(const std::string& path, const Image& image);
bool readRawImage(const std::string& path, Image& image);

struct ImageDiff {
    bool sizeMatches = false;
    double meanAbsolute = 0.0;    // 全チャンネルの平均絶対差（0〜255）
    double differingRatio = 0.0;  // いずれかのチャンネルが threshold を超えて違う画素の割合
};
ImageDiff compare(const Image& a, const Image& b, int threshold = 24);

// 輝度の標準偏差（真っ黒・単色など「何も描かれていない」画面の検出用）
double luminanceStdDev(const Image& image);

} // namespace SelfCheck

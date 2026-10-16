#pragma once

#include <filesystem>
#include <vector>

namespace SvgRaster {

// SVGファイルをRGBA8(非プリマルチプライド、行は下から上=OpenGL向き)にラスタライズする。
// 解像度はSVG自身のサイズを基準に、長辺が MIN_LONG_EDGE〜MAX_LONG_EDGE に収まるよう決める。
// 失敗時は警告ログを出して false を返す。
bool rasterizeFile(const std::filesystem::path& path,
                   std::vector<unsigned char>& rgba, int& width, int& height);

bool isSvgPath(const std::filesystem::path& path);

} // namespace SvgRaster

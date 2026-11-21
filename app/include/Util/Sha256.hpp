#pragma once
#include <cstddef>
#include <string>

// SHA-256。アセットの整合性検証と内容重複の判定に使う（暗号用途ではなく破損検知）。
namespace Sha256 {
    // 小文字16進64文字を返す。
    std::string hex(const void* data, std::size_t size);
}

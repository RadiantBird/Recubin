#include <Util/UUID.hpp>
#include <random>
#include <array>
#include <sstream>
#include <iomanip>
#include <cctype>

namespace RecubinUUID {
std::string generate() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<unsigned> dist(0, 255);
    std::array<unsigned char, 16> b{};
    for (auto& x : b) x = static_cast<unsigned char>(dist(gen));
    b[6] = static_cast<unsigned char>((b[6] & 0x0f) | 0x40);
    b[8] = static_cast<unsigned char>((b[8] & 0x3f) | 0x80);
    std::ostringstream out;
    for (size_t i = 0; i < b.size(); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out << '-';
        out << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(b[i]);
    }
    return out.str();
}

bool isValid(const std::string& value) {
    if (value.size() != 36) return false;
    for (size_t i = 0; i < value.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (value[i] != '-') return false; continue; }
        if (!std::isxdigit(static_cast<unsigned char>(value[i]))) return false;
    }
    return true;
}
}

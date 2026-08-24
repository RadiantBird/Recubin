#include <Util/PngWriter.hpp>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <limits>

namespace {
void put32(std::vector<std::uint8_t>& b, std::uint32_t v) {
    b.push_back(v >> 24); b.push_back(v >> 16); b.push_back(v >> 8); b.push_back(v);
}
std::uint32_t crc(const std::uint8_t* p, std::size_t n) {
    std::uint32_t c = 0xffffffffu;
    for (std::size_t i=0;i<n;++i) { c ^= p[i]; for (int k=0;k<8;++k) c=(c>>1)^(0xedb88320u&-(c&1)); }
    return c ^ 0xffffffffu;
}
void chunk(std::vector<std::uint8_t>& out, const char* type, const std::vector<std::uint8_t>& data) {
    put32(out, static_cast<std::uint32_t>(data.size()));
    const auto start = out.size();
    out.insert(out.end(), type, type + 4); out.insert(out.end(), data.begin(), data.end());
    put32(out, crc(out.data()+start, out.size()-start));
}
}
namespace PngWriter {
bool writeRgba8(const std::string& path, int width, int height, const std::vector<std::uint8_t>& rgba, std::string* error) {
    if (path.empty()) { if (error) *error = "PNG path is empty"; return false; }
    if (width <= 0 || height <= 0) { if(error)*error="invalid RGBA dimensions"; return false; }
    const auto w = static_cast<std::size_t>(width), h = static_cast<std::size_t>(height);
    if (w > std::numeric_limits<std::size_t>::max() / 4u || h > std::numeric_limits<std::size_t>::max() / (w * 4u)) { if(error)*error="image size overflow"; return false; }
    const std::size_t expected = w * h * 4u;
    if (rgba.size() != expected) { if(error)*error="invalid RGBA dimensions"; return false; }
    std::error_code ec; const auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, ec);
    if (ec) { if(error)*error="cannot create PNG directory"; return false; }
    std::vector<std::uint8_t> png = {137,80,78,71,13,10,26,10};
    std::vector<std::uint8_t> ihdr; put32(ihdr,width); put32(ihdr,height); ihdr.insert(ihdr.end(),{8,6,0,0,0}); chunk(png,"IHDR",ihdr);
    if (w > (std::numeric_limits<std::size_t>::max() - 1u) / 4u || h > std::numeric_limits<std::size_t>::max() / (w * 4u + 1u)) { if(error)*error="PNG row size overflow"; return false; }
    std::vector<std::uint8_t> raw; raw.reserve((w*4u+1u)*h);
    for(int y=0;y<height;++y){ raw.push_back(0); raw.insert(raw.end(),rgba.begin()+(height-1-y)*width*4,rgba.begin()+(height-y)*width*4); }
    std::vector<std::uint8_t> z={120,1};
    std::size_t pos=0; while(pos<raw.size()){ const std::size_t len=std::min<std::size_t>(65535,raw.size()-pos); const bool last=pos+len==raw.size(); z.push_back(last?1:0); z.push_back(len); z.push_back(len>>8); const std::uint16_t inv=static_cast<std::uint16_t>(~len); z.push_back(inv); z.push_back(inv>>8); z.insert(z.end(),raw.begin()+pos,raw.begin()+pos+len); pos+=len; }
    std::uint32_t ad=1,bd=0; for(auto v:raw){ad=(ad+v)%65521;bd=(bd+ad)%65521;} put32(z,(bd<<16)|ad); chunk(png,"IDAT",z); chunk(png,"IEND",{});
    std::ofstream f(path,std::ios::binary); if(!f){if(error)*error="cannot open PNG output";return false;} f.write(reinterpret_cast<const char*>(png.data()),png.size()); f.flush(); if(!f && error)*error="cannot write PNG output"; return static_cast<bool>(f);
}
}

#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

#include <Math/Vector3.hpp>
#include <Math/Quaternion.hpp>

// 手書きバイトパッキング(リトルエンディアン前提、既存のChat/DummyPositionと同じ方式)
struct ByteWriter {
    std::vector<uint8_t> data;

    void writeU8(uint8_t v) { data.push_back(v); }
    void writeU16(uint16_t v) {
        size_t off = data.size();
        data.resize(off + sizeof(v));
        std::memcpy(data.data() + off, &v, sizeof(v));
    }
    void writeU32(uint32_t v) {
        size_t off = data.size();
        data.resize(off + sizeof(v));
        std::memcpy(data.data() + off, &v, sizeof(v));
    }
    void writeF32(float v) {
        size_t off = data.size();
        data.resize(off + sizeof(v));
        std::memcpy(data.data() + off, &v, sizeof(v));
    }
    void writeVector3(const Vector3& v) { writeF32(v.x); writeF32(v.y); writeF32(v.z); }
    void writeQuat(const Quaternion& q) { writeF32(q.x); writeF32(q.y); writeF32(q.z); writeF32(q.w); }
};

// 読み出し。残量不足なら false を返す
struct ByteReader {
    const uint8_t* p;
    size_t remaining;

    bool readU8(uint8_t& out) {
        if (remaining < sizeof(out)) return false;
        std::memcpy(&out, p, sizeof(out));
        p += sizeof(out); remaining -= sizeof(out);
        return true;
    }
    bool readU16(uint16_t& out) {
        if (remaining < sizeof(out)) return false;
        std::memcpy(&out, p, sizeof(out));
        p += sizeof(out); remaining -= sizeof(out);
        return true;
    }
    bool readU32(uint32_t& out) {
        if (remaining < sizeof(out)) return false;
        std::memcpy(&out, p, sizeof(out));
        p += sizeof(out); remaining -= sizeof(out);
        return true;
    }
    bool readF32(float& out) {
        if (remaining < sizeof(out)) return false;
        std::memcpy(&out, p, sizeof(out));
        p += sizeof(out); remaining -= sizeof(out);
        return true;
    }
    bool readVector3(Vector3& out) {
        return readF32(out.x) && readF32(out.y) && readF32(out.z);
    }
    bool readQuat(Quaternion& out) {
        return readF32(out.x) && readF32(out.y) && readF32(out.z) && readF32(out.w);
    }
};

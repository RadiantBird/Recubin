#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct CursorImageData {
    std::vector<std::uint8_t> rgba;
    int width = 0;
    int height = 0;
    int hotspotX = 0;
    int hotspotY = 0;
    std::uint64_t revision = 0;

    bool isValid() const;
};

class CursorImageProcessor {
public:
    static constexpr std::size_t SLOT_COUNT = 10;
    static constexpr int DEFAULT_LOGICAL_SIZE = 32;
    static constexpr int MAX_LOGICAL_SIZE = 512;
    static constexpr int MAX_PHYSICAL_SIZE = 4096;

    CursorImageProcessor();
    ~CursorImageProcessor();
    CursorImageProcessor(const CursorImageProcessor&) = delete;
    CursorImageProcessor& operator=(const CursorImageProcessor&) = delete;

    const CursorImageData* prepare(std::size_t slotIndex, const std::string& path,
                                   int logicalHotspotX, int logicalHotspotY,
                                   int logicalSize, float contentScale);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

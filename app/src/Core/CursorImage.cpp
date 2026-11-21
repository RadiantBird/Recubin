#include <Core/CursorImage.hpp>

#include <Util/AssetGuard.hpp>
#include <Util/AssetPath.hpp>
#include <Util/Logger.hpp>
#include <include/stb_image.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <filesystem>

namespace {
std::atomic<std::uint64_t> g_nextCursorRevision{1};

std::uint64_t nextCursorRevision() {
    std::uint64_t revision = g_nextCursorRevision.fetch_add(1, std::memory_order_relaxed);
    while (revision == 0)
        revision = g_nextCursorRevision.fetch_add(1, std::memory_order_relaxed);
    return revision;
}

std::vector<std::uint8_t> resizeRgba(const std::uint8_t* source, int sourceWidth,
                                     int sourceHeight, int targetWidth, int targetHeight) {
    const std::size_t byteCount = static_cast<std::size_t>(targetWidth) *
                                  static_cast<std::size_t>(targetHeight) * 4;
    std::vector<std::uint8_t> result(byteCount);
    for (int y = 0; y < targetHeight; ++y) {
        const float sourceY = std::clamp(
            (static_cast<float>(y) + 0.5f) * sourceHeight / targetHeight - 0.5f,
            0.0f, static_cast<float>(sourceHeight - 1));
        const int y0 = static_cast<int>(std::floor(sourceY));
        const int y1 = std::min(y0 + 1, sourceHeight - 1);
        const float fy = sourceY - y0;
        for (int x = 0; x < targetWidth; ++x) {
            const float sourceX = std::clamp(
                (static_cast<float>(x) + 0.5f) * sourceWidth / targetWidth - 0.5f,
                0.0f, static_cast<float>(sourceWidth - 1));
            const int x0 = static_cast<int>(std::floor(sourceX));
            const int x1 = std::min(x0 + 1, sourceWidth - 1);
            const float fx = sourceX - x0;
            for (int channel = 0; channel < 4; ++channel) {
                const auto at = [&](int sx, int sy) -> float {
                    return source[(static_cast<std::size_t>(sy) * sourceWidth + sx) * 4 + channel];
                };
                const float top = at(x0, y0) + (at(x1, y0) - at(x0, y0)) * fx;
                const float bottom = at(x0, y1) + (at(x1, y1) - at(x0, y1)) * fx;
                result[(static_cast<std::size_t>(y) * targetWidth + x) * 4 + channel] =
                    static_cast<std::uint8_t>(std::lround(top + (bottom - top) * fy));
            }
        }
    }
    return result;
}
}

bool CursorImageData::isValid() const {
    if (width <= 0 || height <= 0 || revision == 0) return false;
    const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    return pixelCount <= rgba.max_size() / 4 && rgba.size() == pixelCount * 4 &&
           hotspotX >= 0 && hotspotX < width && hotspotY >= 0 && hotspotY < height;
}

struct CursorImageProcessor::Impl {
    struct CacheKey {
        std::string path;
        std::filesystem::file_time_type writeTime{};
        bool hasWriteTime = false;
        int hotspotX = 0;
        int hotspotY = 0;
        int size = DEFAULT_LOGICAL_SIZE;
        float scale = 1.0f;

        bool operator==(const CacheKey& other) const {
            return path == other.path && writeTime == other.writeTime &&
                   hasWriteTime == other.hasWriteTime && hotspotX == other.hotspotX &&
                   hotspotY == other.hotspotY && size == other.size && scale == other.scale;
        }
        bool hasSameSettings(const CacheKey& other) const {
            return path == other.path && hotspotX == other.hotspotX && hotspotY == other.hotspotY &&
                   size == other.size && scale == other.scale;
        }
    };
    struct CacheEntry {
        enum class Failure { None, Invalid, Blocked, Missing, Decode };
        CacheKey key;
        bool attempted = false;
        Failure failure = Failure::None;
        CursorImageData data;
    };
    std::array<CacheEntry, SLOT_COUNT> cache{};
};

CursorImageProcessor::CursorImageProcessor() : m_impl(std::make_unique<Impl>()) {}
CursorImageProcessor::~CursorImageProcessor() = default;

const CursorImageData* CursorImageProcessor::prepare(std::size_t slotIndex, const std::string& path,
                                                      int logicalHotspotX, int logicalHotspotY,
                                                      int logicalSize, float contentScale) {
    if (!m_impl || slotIndex >= SLOT_COUNT) return nullptr;
    Impl::CacheKey key;
    key.path = AssetPath::normalize(path);
    key.hotspotX = std::max(0, logicalHotspotX);
    key.hotspotY = std::max(0, logicalHotspotY);
    key.size = std::clamp(logicalSize, 1, MAX_LOGICAL_SIZE);
    const bool validScale = std::isfinite(contentScale) && contentScale > 0.0f;
    key.scale = validScale ? contentScale : 0.0f;

    auto& entry = m_impl->cache[slotIndex];
    if (entry.attempted && entry.key.hasSameSettings(key) &&
        (entry.failure == Impl::CacheEntry::Failure::Invalid ||
         entry.failure == Impl::CacheEntry::Failure::Blocked))
        return nullptr;
    const double physicalLong = validScale ?
        std::round(static_cast<double>(key.size) * key.scale) : 0.0;
    if (key.path.empty() || !validScale || !std::isfinite(physicalLong) ||
        physicalLong < 1.0 || physicalLong > MAX_PHYSICAL_SIZE) {
        entry.key = key;
        entry.attempted = true;
        entry.failure = Impl::CacheEntry::Failure::Invalid;
        entry.data = {};
        return nullptr;
    }

    const bool pathAllowed = AssetGuard::allow(key.path);
    if (!pathAllowed) {
        entry.key = key;
        entry.attempted = true;
        entry.failure = Impl::CacheEntry::Failure::Blocked;
        entry.data = {};
        return nullptr;
    }
    std::filesystem::path filePath;
    filePath = AssetPath::fromStored(key.path);
    std::error_code error;
    key.writeTime = std::filesystem::last_write_time(filePath, error);
    key.hasWriteTime = !error;

    if (entry.attempted && entry.key == key)
        return entry.data.isValid() ? &entry.data : nullptr;
    entry.key = key;
    entry.attempted = true;
    entry.failure = Impl::CacheEntry::Failure::None;
    entry.data = {};

    if (!key.hasWriteTime) {
        entry.failure = Impl::CacheEntry::Failure::Missing;
        return nullptr;
    }

    int sourceWidth = 0, sourceHeight = 0, channels = 0;
    stbi_set_flip_vertically_on_load(0);
    stbi_uc* source = stbi_load(filePath.string().c_str(), &sourceWidth, &sourceHeight, &channels, 4);
    stbi_set_flip_vertically_on_load(1);
    if (!source || sourceWidth <= 0 || sourceHeight <= 0) {
        if (source) stbi_image_free(source);
        entry.failure = Impl::CacheEntry::Failure::Decode;
        RCBN_WARN("CursorImageProcessor: failed to load cursor image " + key.path);
        return nullptr;
    }

    const int targetLong = static_cast<int>(physicalLong);
    const int targetWidth = sourceWidth >= sourceHeight ? targetLong :
        std::max(1, static_cast<int>(std::lround(
            static_cast<double>(targetLong) * sourceWidth / sourceHeight)));
    const int targetHeight = sourceHeight >= sourceWidth ? targetLong :
        std::max(1, static_cast<int>(std::lround(
            static_cast<double>(targetLong) * sourceHeight / sourceWidth)));
    if (targetWidth == sourceWidth && targetHeight == sourceHeight) {
        const std::size_t byteCount = static_cast<std::size_t>(sourceWidth) * sourceHeight * 4;
        entry.data.rgba.assign(source, source + byteCount);
    } else {
        entry.data.rgba = resizeRgba(source, sourceWidth, sourceHeight, targetWidth, targetHeight);
    }
    stbi_image_free(source);

    entry.data.width = targetWidth;
    entry.data.height = targetHeight;
    entry.data.hotspotX = static_cast<int>(std::clamp(
        std::round(static_cast<double>(key.hotspotX) * key.scale), 0.0,
        static_cast<double>(targetWidth - 1)));
    entry.data.hotspotY = static_cast<int>(std::clamp(
        std::round(static_cast<double>(key.hotspotY) * key.scale), 0.0,
        static_cast<double>(targetHeight - 1)));
    entry.data.revision = nextCursorRevision();
    return &entry.data;
}

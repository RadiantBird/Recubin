#include <Util/Platform.hpp>
#include <Util/IPlatform.hpp>
#include <Util/MockPlatform.hpp>
#include <memory>
#include <cstdlib>

#ifdef _WIN32
#include <Util/WindowsPlatform.hpp>
#elif defined(__APPLE__)
#include <Util/MacPlatform.hpp>
#endif

IPlatform& getPlatform() {
    static std::unique_ptr<IPlatform> instance = []() -> std::unique_ptr<IPlatform> {
        if (std::getenv("RECUBIN_MOCK_PLATFORM")) return std::make_unique<MockPlatform>();
#ifdef _WIN32
        return std::make_unique<WindowsPlatform>();
#elif defined(__APPLE__)
        return std::make_unique<MacPlatform>();
#else
        return std::make_unique<MockPlatform>();
#endif
    }();
    return *instance;
}

#include <Util/Platform.hpp>
#include <Util/IPlatform.hpp>
#include <Util/MockPlatform.hpp>
#include <memory>
#include <cstdlib>

#ifdef _WIN32
#include <Util/WindowsPlatform.hpp>
#endif

IPlatform& getPlatform() {
    static std::unique_ptr<IPlatform> instance = []() -> std::unique_ptr<IPlatform> {
#ifdef _WIN32
        if (std::getenv("RECUBIN_MOCK_PLATFORM")) return std::make_unique<MockPlatform>();
        return std::make_unique<WindowsPlatform>();
#else
        return std::make_unique<MockPlatform>();
#endif
    }();
    return *instance;
}

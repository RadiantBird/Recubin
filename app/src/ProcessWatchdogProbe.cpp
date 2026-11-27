#include <Core/ProcessWatchdog.hpp>

#include <chrono>
#include <cstdlib>
#include <string_view>
#include <thread>

#if defined(_WIN32)
#include <Windows.h>
#endif

int main(int argc, char** argv) {
    const bool timeout = argc > 1 && std::string_view(argv[1]) == "--timeout";
    ProcessWatchdog watchdog(std::chrono::milliseconds(100));
    watchdog.start([] {
#if defined(_WIN32)
        TerminateProcess(GetCurrentProcess(), 0xC0000409u);
#else
        std::abort();
#endif
    });
    if (timeout) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        return 2;
    }
    for (int i = 0; i < 10; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        watchdog.heartbeat();
    }
    watchdog.stop();
    return watchdog.timedOut() ? 1 : 0;
}

#include <Core/ProcessHandler.hpp>
#include <Core/ProcessRuntimeManager.hpp>
#include <Core/ProcessWatchdog.hpp>
#include <Instances/Process.hpp>
#include <Util/AssetPath.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <thread>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace fs = std::filesystem;

int terminateProcess() {
#if defined(_WIN32)
    TerminateProcess(GetCurrentProcess(), 0xC0000409u);
#endif
    return 2;
}

bool stopHandler(ProcessHandler& handler) {
    handler.requestStop();
    for (int i = 0; i < 300 && handler.isRunning(); ++i) {
        handler.pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return !handler.isRunning() && handler.unload();
}

int runHandler(const fs::path& dll, bool two) {
    ProcessHandler first;
    if (!first.load(dll) || !first.start()) return 1;
    ProcessHandler second;
    if (two && (!second.load(dll) || !second.start())) return 1;
    for (int i = 0; i < 10; ++i) {
        first.pump();
        if (two) second.pump();
    }
    const bool ok = stopHandler(first) && (!two || stopHandler(second));
    return ok ? 0 : 1;
}

int runManager(const fs::path& source, const fs::path& dll, bool infinite) {
    auto process = std::make_shared<Process>();
    process->Name = "FixtureProcess";
    process->Path = AssetPath::toStored(source);
    process->DllPath = AssetPath::toStored(dll);
    auto system = System{};
    system.EnableExecutingProcess = true;
    ProcessRuntimeManager::Config config;
    config.assetBase = fs::current_path();
    config.includeDirectories.push_back(fs::absolute("app/include"));
    config.watchdogTimeout = std::chrono::milliseconds(100);
    ProcessRuntimeManager manager;
    if (!manager.prepare({process}, system, config)) {
        std::cerr << "prepare failed: " << manager.lastError() << '\n';
        return 1;
    }
    if (!manager.start()) {
        std::cerr << "start failed: " << manager.lastError() << '\n';
        return 1;
    }
    if (infinite) {
        // The fixture blocks inside start(); the manager watchdog must terminate us.
        for (;;) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    for (int i = 0; i < 10; ++i) manager.pump();
    manager.stop();
    return 0;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) return 1;
    const std::string mode = AssetPath::toStored(fs::path(std::wstring(argv[1])));
    const fs::path first = fs::path(std::wstring(argv[2]));
    if (mode == "--handler") return runHandler(first, false);
    if (mode == "--handler-two") return runHandler(first, true);
    if (mode == "--manager" && argc >= 4)
        return runManager(fs::path(std::wstring(argv[3])), first, false);
    if (mode == "--manager-infinite" && argc >= 4)
        return runManager(fs::path(std::wstring(argv[3])), first, true);
    if (mode == "--infinite") {
        ProcessHandler handler;
        if (!handler.load(first)) return 1;
        ProcessWatchdog watchdog(std::chrono::milliseconds(100));
        watchdog.start([] { (void)terminateProcess(); });
        handler.start();
        return 1;
    }
    return 1;
}

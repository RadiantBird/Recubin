#pragma once

#include <Core/ProcessBuilder.hpp>
#include <Core/ProcessHandler.hpp>
#include <Core/ProcessWatchdog.hpp>

#include <Instances/Process.hpp>
#include <Instances/System.hpp>

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Owns the Process DLL handlers for one Play session. It is deliberately
// independent from Studio/game_main so both runtimes can use the same policy.
class ProcessRuntimeManager {
public:
    struct Config {
        std::filesystem::path assetBase;
        std::vector<std::filesystem::path> sdkHeaders;
        std::vector<std::filesystem::path> includeDirectories;
        // Studio may build missing DLLs; packaged games only validate existing artifacts.
        bool allowBuild = true;
        std::filesystem::path compilerPath;
        ProcessHandler::InstanceCallbacks instanceCallbacks;
        std::chrono::milliseconds watchdogTimeout{2000};
        std::function<void(const std::string&)> reportError;
        std::function<void()> requestEngineStop;
    };

    ProcessRuntimeManager();
    ~ProcessRuntimeManager();
    ProcessRuntimeManager(const ProcessRuntimeManager&) = delete;
    ProcessRuntimeManager& operator=(const ProcessRuntimeManager&) = delete;

    bool prepare(const std::vector<std::shared_ptr<Process>>& processes,
                 const System& system, const Config& config);
    bool start();
    bool pump(double deltaSeconds = -1.0);
    void stop();

    bool isPrepared() const noexcept;
    bool isRunning() const noexcept;
    bool hasFatalError() const noexcept;
    const std::string& lastError() const noexcept;

private:
    struct Entry;
    void report(const std::string& message);
    void failFatal(const std::string& message);

    std::vector<std::unique_ptr<Entry>> m_entries;
    std::unique_ptr<ProcessWatchdog> m_watchdog;
    Config m_config;
    std::string m_lastError;
    bool m_prepared = false;
    bool m_running = false;
    bool m_stopping = false;
    bool m_fatal = false;
};

// Collects Process instances recursively from an Instance tree.
void collectProcessInstances(const std::shared_ptr<Instance>& root,
                             std::vector<std::shared_ptr<Process>>& result);

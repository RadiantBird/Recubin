#include <Core/ProcessRuntimeManager.hpp>

#include <Core/ProcessHandler.hpp>
#include <Util/AssetPath.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <thread>

#if defined(_WIN32)
#include <Windows.h>
#endif

struct ProcessRuntimeManager::Entry {
    std::shared_ptr<Process> instance;
    std::unique_ptr<ProcessHandler> handler;
};

ProcessRuntimeManager::ProcessRuntimeManager() = default;

ProcessRuntimeManager::~ProcessRuntimeManager() {
    stop();
}

void collectProcessInstances(const std::shared_ptr<Instance>& root,
                             std::vector<std::shared_ptr<Process>>& result) {
    if (!root) return;
    if (auto process = std::dynamic_pointer_cast<Process>(root)) result.push_back(process);
    for (const auto& [name, child] : root->getChildren()) {
        (void)name;
        collectProcessInstances(child, result);
    }
}

bool ProcessRuntimeManager::prepare(
    const std::vector<std::shared_ptr<Process>>& processes,
    const System& system, const Config& config) {
    stop();
    m_config = config;
    m_watchdog = std::make_unique<ProcessWatchdog>(config.watchdogTimeout);
    m_lastError.clear();
    m_fatal = false;
    m_prepared = false;

    std::vector<std::shared_ptr<Process>> enabled;
    for (const auto& process : processes) {
        if (process && process->Enabled) enabled.push_back(process);
    }
    if (enabled.empty()) {
        m_prepared = true;
        return true;
    }
    if (!system.EnableExecutingProcess || system.UseNetwork) {
        report(system.UseNetwork
            ? "Process execution is unavailable while System.UseNetwork is enabled; skipping Process instances."
            : "Process execution is disabled by System.EnableExecutingProcess; skipping Process instances.");
        m_prepared = true;
        return true;
    }

    for (const auto& process : enabled) {
        ProcessBuilder::BuildRequest request;
        request.sourcePath = AssetPath::fromStored(process->Path);
        request.dllPath = AssetPath::fromStored(process->DllPath);
        request.assetBase = config.assetBase;
        request.headerPaths = config.sdkHeaders;
        request.includeDirectories = config.includeDirectories;
        request.compilerPath = config.compilerPath;
        if (m_config.allowBuild) {
            const auto build = ProcessBuilder::buildIfNeeded(request);
            if (!build.success) {
                report("Failed to prepare Process '" + process->Name + "':\n" + build.log);
                stop();
                return false;
            }
        } else {
            const auto dll = AssetPath::resolveAgainst(process->DllPath, config.assetBase);
            std::error_code error;
            if (!std::filesystem::is_regular_file(dll, error) || error) {
                report("Packaged Process DLL is missing for '" + process->Name + "': " +
                       AssetPath::toStored(dll));
                stop();
                return false;
            }
        }

        auto entry = std::make_unique<Entry>();
        entry->instance = process;
        entry->handler = std::make_unique<ProcessHandler>();
        m_entries.push_back(std::move(entry));
    }
    m_prepared = true;
    return true;
}

bool ProcessRuntimeManager::start() {
    if (!m_prepared || m_running || m_stopping) return m_prepared && m_running;
    if (m_entries.empty()) {
        m_running = false;
        return true;
    }
    m_lastError.clear();
    if (!m_watchdog) {
        m_watchdog = std::make_unique<ProcessWatchdog>(m_config.watchdogTimeout);
    }
    m_watchdog->start([] {
        const char message[] = "Recubin Process watchdog timeout; terminating engine.\n";
#if defined(_WIN32)
        const HANDLE errorHandle = GetStdHandle(STD_ERROR_HANDLE);
        if (errorHandle && errorHandle != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(errorHandle, message, static_cast<DWORD>(sizeof(message) - 1),
                      &written, nullptr);
        }
        TerminateProcess(GetCurrentProcess(), 0xC0000409u);
#else
        std::fwrite(message, 1, sizeof(message) - 1, stderr);
        std::fflush(stderr);
        std::abort();
#endif
    });
    for (std::size_t index = 0; index < m_entries.size(); ++index) {
        auto& entry = *m_entries[index];
        const auto dll = AssetPath::resolveAgainst(entry.instance->DllPath,
                                                   m_config.assetBase);
        if (!entry.handler->load(dll)) {
            report("Failed to load Process '" + entry.instance->Name + "': " +
                   entry.handler->lastError());
            stop();
            return false;
        }
        entry.handler->setInstanceCallbacks(m_config.instanceCallbacks);
        if (!entry.handler->start()) {
            report("Failed to start Process '" + entry.instance->Name + "': " +
                   entry.handler->lastError());
            stop();
            return false;
        }
    }
    m_watchdog->heartbeat();
    m_running = true;
    return true;
}

bool ProcessRuntimeManager::pump(double deltaSeconds) {
    if (!m_running || m_stopping) return false;
    bool anyRunning = false;
    for (auto& entry : m_entries) {
        entry->handler->rebindHost();
        if (entry->handler->pump(deltaSeconds)) anyRunning = true;
    }
    m_watchdog->heartbeat();
    if (!anyRunning) {
        stop();
        return false;
    }
    return true;
}

void ProcessRuntimeManager::stop() {
    if (m_stopping) return;
    m_stopping = true;
    for (auto& entry : m_entries) {
        if (entry->handler) entry->handler->requestStop();
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    bool running = true;
    while (running && std::chrono::steady_clock::now() < deadline) {
        running = false;
        for (auto& entry : m_entries) {
            if (entry->handler && entry->handler->isRunning()) {
                entry->handler->pump();
                running = true;
            }
        }
        if (running) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    for (auto& entry : m_entries) {
        if (entry->handler) {
            if (!entry->handler->unload() && m_lastError.empty())
                m_lastError = entry->handler->lastError();
        }
    }
    if (m_watchdog) m_watchdog->stop();
    m_entries.clear();
    m_running = false;
    m_prepared = false;
    m_stopping = false;
}

bool ProcessRuntimeManager::isPrepared() const noexcept { return m_prepared; }
bool ProcessRuntimeManager::isRunning() const noexcept { return m_running; }
bool ProcessRuntimeManager::hasFatalError() const noexcept { return m_fatal; }
const std::string& ProcessRuntimeManager::lastError() const noexcept { return m_lastError; }

void ProcessRuntimeManager::report(const std::string& message) {
    m_lastError = message;
    if (m_config.reportError) m_config.reportError(message);
}

void ProcessRuntimeManager::failFatal(const std::string& message) {
    if (m_fatal) return;
    m_fatal = true;
    report(message);
    if (m_config.requestEngineStop) m_config.requestEngineStop();
}

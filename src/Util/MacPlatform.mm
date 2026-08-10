#ifdef __APPLE__

#include <Util/MacPlatform.hpp>
#import <Cocoa/Cocoa.h>
#include <dispatch/dispatch.h>
#include <dlfcn.h>
#include <cerrno>
#include <cctype>
#include <csignal>
#include <fcntl.h>
#include <functional>
#include <memory>
#include <optional>
#include <spawn.h>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace {

std::string trimAsciiWhitespace(const std::string& value) {
    size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) ++first;
    size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) --last;
    return value.substr(first, last - first);
}

std::string normalizedExtension(const std::string& value) {
    std::string extension = trimAsciiWhitespace(value);
    while (!extension.empty() && extension.front() == '.') extension.erase(extension.begin());
    return extension;
}

NSMutableArray<NSString*>* allowedExtensions(const std::vector<FileFilter>& filters, bool& unrestricted) {
    NSMutableArray<NSString*>* extensions = [NSMutableArray array];
    unrestricted = filters.empty();
    for (const FileFilter& filter : filters) {
        std::istringstream patterns(filter.spec);
        std::string pattern;
        while (std::getline(patterns, pattern, ';')) {
            pattern = trimAsciiWhitespace(pattern);
            if (pattern == "*.*" || pattern == "*") {
                unrestricted = true;
                return extensions;
            }
            if (pattern.rfind("*.", 0) == 0) pattern.erase(0, 2);
            else if (!pattern.empty() && pattern.front() == '.') pattern.erase(0, 1);
            if (pattern.empty() || pattern.find('*') != std::string::npos) continue;

            NSString* extension = [NSString stringWithUTF8String:pattern.c_str()];
            if (extension && ![extensions containsObject:extension]) [extensions addObject:extension];
        }
    }
    return extensions;
}

std::string fileSystemPath(NSString* path) {
    if (!path) return {};
    const char* bytes = [path fileSystemRepresentation];
    return bytes ? std::string(bytes) : std::string();
}

std::string runStringOperationOnMain(const std::function<std::string()>& operation) {
    if ([NSThread isMainThread]) return operation();

    auto result = std::make_shared<std::string>();
    dispatch_sync(dispatch_get_main_queue(), ^{
        *result = operation();
    });
    return *result;
}

void runVoidOperationOnMain(const std::function<void()>& operation) {
    if ([NSThread isMainThread]) {
        operation();
        return;
    }
    dispatch_sync(dispatch_get_main_queue(), ^{
        operation();
    });
}

void configureFileTypes(NSSavePanel* panel, const std::vector<FileFilter>& filters) {
    bool unrestricted = false;
    NSMutableArray<NSString*>* extensions = allowedExtensions(filters, unrestricted);
    if (!unrestricted && extensions.count > 0) {
        panel.allowedFileTypes = extensions;
        panel.allowsOtherFileTypes = NO;
    }
}

class MacChildProcess final : public IChildProcess {
public:
    explicit MacChildProcess(pid_t processId)
        : m_processId(processId) {}

    ~MacChildProcess() override {
        refreshExitCode();
    }

    bool isRunning() override {
        refreshExitCode();
        return !m_exitCode.has_value();
    }

    std::optional<int> exitCode() override {
        refreshExitCode();
        return m_exitCode;
    }

    bool requestClose() override {
        if (!isRunning()) return true;
        @autoreleasepool {
            NSRunningApplication* application =
                [NSRunningApplication runningApplicationWithProcessIdentifier:m_processId];
            if (application && [application terminate]) return true;
        }
        return kill(m_processId, SIGTERM) == 0 || errno == ESRCH;
    }

    bool terminate() override {
        if (!isRunning()) return true;
        return kill(m_processId, SIGKILL) == 0 || errno == ESRCH;
    }

private:
    void refreshExitCode() {
        if (m_processId <= 0 || m_exitCode.has_value()) return;
        int status = 0;
        const pid_t result = waitpid(m_processId, &status, WNOHANG);
        if (result != m_processId) return;

        if (WIFEXITED(status)) {
            m_exitCode = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            m_exitCode = 128 + WTERMSIG(status);
        }
    }

    pid_t m_processId = -1;
    std::optional<int> m_exitCode;
};

} // namespace

std::string MacPlatform::openFileDialog(const std::vector<FileFilter>& filters) {
    return runStringOperationOnMain([&filters]() -> std::string {
        @autoreleasepool {
            NSOpenPanel* panel = [NSOpenPanel openPanel];
            panel.canChooseFiles = YES;
            panel.canChooseDirectories = NO;
            panel.allowsMultipleSelection = NO;
            configureFileTypes(panel, filters);
            if ([panel runModal] != NSModalResponseOK) return {};
            return fileSystemPath(panel.URL.path);
        }
    });
}

std::string MacPlatform::saveFileDialog(const std::vector<FileFilter>& filters,
                                        const std::string& defaultExt) {
    return runStringOperationOnMain([&filters, &defaultExt]() -> std::string {
        @autoreleasepool {
            NSSavePanel* panel = [NSSavePanel savePanel];
            configureFileTypes(panel, filters);
            panel.extensionHidden = NO;
            if ([panel runModal] != NSModalResponseOK) return {};

            NSString* path = panel.URL.path;
            std::string extension = normalizedExtension(defaultExt);
            if (!extension.empty() && path.pathExtension.length == 0) {
                NSString* defaultExtension = [NSString stringWithUTF8String:extension.c_str()];
                if (!defaultExtension) return {};
                path = [path stringByAppendingPathExtension:defaultExtension];
            }
            return fileSystemPath(path);
        }
    });
}

std::string MacPlatform::openFolderDialog() {
    return runStringOperationOnMain([]() -> std::string {
        @autoreleasepool {
            NSOpenPanel* panel = [NSOpenPanel openPanel];
            panel.canChooseFiles = NO;
            panel.canChooseDirectories = YES;
            panel.allowsMultipleSelection = NO;
            if ([panel runModal] != NSModalResponseOK) return {};
            return fileSystemPath(panel.URL.path);
        }
    });
}

void MacPlatform::revealInFileManager(const std::string& path) {
    runVoidOperationOnMain([&path]() {
        @autoreleasepool {
            NSString* filePath = [NSString stringWithUTF8String:path.c_str()];
            if (!filePath) return;
            [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:filePath]];
        }
    });
}

ApplicationIconResult MacPlatform::setApplicationIcon(const std::string& path) {
    ApplicationIconResult result = ApplicationIconResult::Failed;
    runVoidOperationOnMain([&path, &result]() {
        @autoreleasepool {
            NSApplication* application = [NSApplication sharedApplication];
            if (path.empty()) {
                application.applicationIconImage = nil;
                result = ApplicationIconResult::Applied;
                return;
            }

            NSString* filePath = [NSString stringWithUTF8String:path.c_str()];
            if (!filePath) return;

            NSImage* image = [[[NSImage alloc] initWithContentsOfFile:filePath] autorelease];
            if (!image) return;

            application.applicationIconImage = image;
            result = ApplicationIconResult::Applied;
        }
    });
    return result;
}

void MacPlatform::setupConsoleUtf8() {}

void MacPlatform::setupDllSearchPath() {}

void* MacPlatform::loadDynamicLibrary(const std::string& name) {
    return dlopen(name.c_str(), RTLD_NOW | RTLD_LOCAL);
}

void* MacPlatform::getSymbol(void* handle, const std::string& symbolName) {
    return handle ? dlsym(handle, symbolName.c_str()) : nullptr;
}

void MacPlatform::freeDynamicLibrary(void* handle) {
    if (handle) dlclose(handle);
}

std::unique_ptr<IChildProcess> MacPlatform::launchChildProcess(
    const ChildProcessLaunchOptions& options) {
    if (options.executable.empty() ||
        (options.outputLogPath.has_value() && options.outputLogPath->empty())) {
        return nullptr;
    }

    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) return nullptr;

    bool actionsValid =
        posix_spawn_file_actions_addopen(
            &actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0) == 0;
    const char* outputPath = options.outputLogPath.has_value()
        ? options.outputLogPath->c_str() : "/dev/null";
    const int outputFlags = options.outputLogPath.has_value()
        ? O_WRONLY | O_CREAT | O_TRUNC : O_WRONLY;
    if (actionsValid) {
        actionsValid = posix_spawn_file_actions_addopen(
            &actions, STDOUT_FILENO, outputPath, outputFlags, 0644) == 0;
    }
    if (actionsValid) {
        actionsValid = posix_spawn_file_actions_adddup2(
            &actions, STDOUT_FILENO, STDERR_FILENO) == 0;
    }
    if (actionsValid && !options.workingDirectory.empty()) {
        actionsValid = posix_spawn_file_actions_addchdir_np(
            &actions, options.workingDirectory.c_str()) == 0;
    }

    std::vector<std::string> argumentStorage;
    argumentStorage.reserve(options.arguments.size() + 1);
    argumentStorage.push_back(options.executable);
    argumentStorage.insert(
        argumentStorage.end(), options.arguments.begin(), options.arguments.end());

    std::vector<char*> arguments;
    arguments.reserve(argumentStorage.size() + 1);
    for (std::string& argument : argumentStorage) arguments.push_back(argument.data());
    arguments.push_back(nullptr);

    pid_t processId = -1;
    const int spawnResult = actionsValid
        ? posix_spawn(&processId, options.executable.c_str(), &actions, nullptr,
                      arguments.data(), environ)
        : EINVAL;
    posix_spawn_file_actions_destroy(&actions);
    if (spawnResult != 0) return nullptr;

    return std::make_unique<MacChildProcess>(processId);
}

#endif // __APPLE__

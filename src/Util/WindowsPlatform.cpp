#ifdef _WIN32

#include <Util/WindowsPlatform.hpp>
#include <windows26.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <filesystem>
#include <optional>
#include <vector>

namespace {

std::wstring utf8ToWide(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size = MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), nullptr, 0);
    std::wstring w(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), w.data(), size);
    return w;
}

std::string wideToUtf8(PWSTR w) {
    if (!w) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return {};
    std::string s(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), len, nullptr, nullptr);
    return s;
}

std::wstring quoteCommandLineArgument(const std::wstring& argument) {
    const bool needsQuotes = argument.empty() ||
        argument.find_first_of(L" \t\n\v\"") != std::wstring::npos;
    if (!needsQuotes) return argument;

    std::wstring quoted;
    quoted.push_back(L'\"');
    size_t backslashCount = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashCount;
            continue;
        }
        if (character == L'\"') {
            quoted.append(backslashCount * 2 + 1, L'\\');
            quoted.push_back(L'\"');
        } else {
            quoted.append(backslashCount, L'\\');
            quoted.push_back(character);
        }
        backslashCount = 0;
    }
    quoted.append(backslashCount * 2, L'\\');
    quoted.push_back(L'\"');
    return quoted;
}

std::wstring makeCommandLine(const ChildProcessLaunchOptions& options) {
    std::wstring commandLine = quoteCommandLineArgument(utf8ToWide(options.executable));
    for (const std::string& argument : options.arguments) {
        commandLine.push_back(L' ');
        commandLine += quoteCommandLineArgument(utf8ToWide(argument));
    }
    return commandLine;
}

struct CloseWindowContext {
    DWORD processId = 0;
    bool posted = false;
};

BOOL CALLBACK requestProcessWindowClose(HWND window, LPARAM parameter) {
    auto* context = reinterpret_cast<CloseWindowContext*>(parameter);
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId != context->processId) return TRUE;
    if (PostMessageW(window, WM_CLOSE, 0, 0)) context->posted = true;
    return TRUE;
}

class WindowsChildProcess final : public IChildProcess {
public:
    WindowsChildProcess(HANDLE process, DWORD processId)
        : m_process(process), m_processId(processId) {}

    ~WindowsChildProcess() override {
        if (m_process) CloseHandle(m_process);
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
        CloseWindowContext context{m_processId, false};
        EnumWindows(requestProcessWindowClose, reinterpret_cast<LPARAM>(&context));
        return context.posted;
    }

    bool terminate() override {
        if (!isRunning()) return true;
        return TerminateProcess(m_process, 1) != FALSE;
    }

private:
    void refreshExitCode() {
        if (!m_process || m_exitCode.has_value()) return;
        if (WaitForSingleObject(m_process, 0) != WAIT_OBJECT_0) return;

        DWORD code = 0;
        if (GetExitCodeProcess(m_process, &code)) {
            m_exitCode = static_cast<int>(code);
        }
    }

    HANDLE m_process = nullptr;
    DWORD m_processId = 0;
    std::optional<int> m_exitCode;
};

// filtersが空の場合、IFileDialog::SetFileTypesを呼ばない(=すべてのファイルを表示)。
// COMDLG_FILTERSPECはLPCWSTRを保持するだけなので、変換したwstringの寿命をpfd->Show()まで保つ。
std::string runFileDialog(const CLSID& clsid, const std::vector<FileFilter>& filters,
                           const std::string& defaultExt, bool pickFolders) {
    std::string result;
    IFileDialog* pfd = nullptr;
    if (FAILED(CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd)))) {
        return result;
    }

    if (pickFolders) {
        DWORD opts = 0;
        pfd->GetOptions(&opts);
        pfd->SetOptions(opts | FOS_PICKFOLDERS | FOS_PATHMUSTEXIST);
    } else if (!filters.empty()) {
        std::vector<std::wstring> names, specs;
        names.reserve(filters.size());
        specs.reserve(filters.size());
        for (const FileFilter& f : filters) {
            names.push_back(utf8ToWide(f.name));
            specs.push_back(utf8ToWide(f.spec));
        }
        std::vector<COMDLG_FILTERSPEC> comFilters;
        comFilters.reserve(filters.size());
        for (size_t i = 0; i < filters.size(); i++) {
            comFilters.push_back({ names[i].c_str(), specs[i].c_str() });
        }
        pfd->SetFileTypes((UINT)comFilters.size(), comFilters.data());
    }

    if (!defaultExt.empty()) {
        std::wstring wExt = utf8ToWide(defaultExt);
        pfd->SetDefaultExtension(wExt.c_str());
    }

    if (SUCCEEDED(pfd->Show(nullptr))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(pfd->GetResult(&item))) {
            PWSTR wpath = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &wpath))) {
                result = wideToUtf8(wpath);
                CoTaskMemFree(wpath);
            }
            item->Release();
        }
    }
    pfd->Release();
    return result;
}

} // namespace

std::string WindowsPlatform::openFileDialog(const std::vector<FileFilter>& filters) {
    return runFileDialog(CLSID_FileOpenDialog, filters, "", false);
}

std::string WindowsPlatform::saveFileDialog(const std::vector<FileFilter>& filters, const std::string& defaultExt) {
    return runFileDialog(CLSID_FileSaveDialog, filters, defaultExt, false);
}

std::string WindowsPlatform::openFolderDialog() {
    return runFileDialog(CLSID_FileOpenDialog, {}, "", true);
}

void WindowsPlatform::revealInFileManager(const std::string& path) {
    // 相対パスのままShellExecuteWに渡すと、関連付け先アプリが自分自身のカレント
    // ディレクトリ基準で解決しようとして見つからない場合がある(呼び出し元プロセスの
    // CWD基準では解決されない)。事前に絶対パスへ変換して渡すことでこれを防ぐ。
    std::wstring wp = utf8ToWide(path);
    std::error_code ec;
    std::filesystem::path abs = std::filesystem::absolute(std::filesystem::path(wp), ec);
    if (!ec) wp = abs.wstring();
    ShellExecuteW(nullptr, L"open", wp.c_str(), nullptr, nullptr, SW_SHOW);
}

ApplicationIconResult WindowsPlatform::setApplicationIcon(const std::string& path) {
    (void)path;
    return ApplicationIconResult::Unsupported;
}

void WindowsPlatform::setupConsoleUtf8() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}

void WindowsPlatform::setupDllSearchPath() {
    std::vector<wchar_t> executablePath(32768);
    DWORD length = GetModuleFileNameW(nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));
    if (length == 0 || length >= executablePath.size()) return;

    std::filesystem::path dllDir = std::filesystem::path(std::wstring(executablePath.data(), length)).parent_path() / L"dlls";
    if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS)) return;
    AddDllDirectory(dllDir.c_str());
}

void* WindowsPlatform::loadDynamicLibrary(const std::string& name) {
    return reinterpret_cast<void*>(LoadLibraryA(name.c_str()));
}

void* WindowsPlatform::getSymbol(void* handle, const std::string& symbolName) {
    if (!handle) return nullptr;
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(handle), symbolName.c_str()));
}

void WindowsPlatform::freeDynamicLibrary(void* handle) {
    if (handle) FreeLibrary(reinterpret_cast<HMODULE>(handle));
}

std::unique_ptr<IChildProcess> WindowsPlatform::launchChildProcess(
    const ChildProcessLaunchOptions& options) {
    if (options.executable.empty() ||
        (options.outputLogPath.has_value() && options.outputLogPath->empty())) {
        return nullptr;
    }

    SECURITY_ATTRIBUTES securityAttributes{};
    securityAttributes.nLength = sizeof(securityAttributes);
    securityAttributes.bInheritHandle = TRUE;

    HANDLE inputHandle = CreateFileW(
        L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &securityAttributes, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (inputHandle == INVALID_HANDLE_VALUE) return nullptr;

    const std::wstring outputPath = options.outputLogPath.has_value()
        ? utf8ToWide(*options.outputLogPath) : std::wstring(L"NUL");
    const DWORD outputDisposition = options.outputLogPath.has_value()
        ? CREATE_ALWAYS : OPEN_EXISTING;
    HANDLE outputHandle = CreateFileW(
        outputPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &securityAttributes, outputDisposition, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (outputHandle == INVALID_HANDLE_VALUE) {
        CloseHandle(inputHandle);
        return nullptr;
    }

    SIZE_T attributeBytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
    std::vector<unsigned char> attributeStorage(attributeBytes);
    auto* attributeList = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(
        attributeStorage.data());
    if (attributeBytes == 0 ||
        !InitializeProcThreadAttributeList(attributeList, 1, 0, &attributeBytes)) {
        CloseHandle(outputHandle);
        CloseHandle(inputHandle);
        return nullptr;
    }

    HANDLE inheritedHandles[] = {inputHandle, outputHandle};
    if (!UpdateProcThreadAttribute(
            attributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            inheritedHandles, sizeof(inheritedHandles), nullptr, nullptr)) {
        DeleteProcThreadAttributeList(attributeList);
        CloseHandle(outputHandle);
        CloseHandle(inputHandle);
        return nullptr;
    }

    STARTUPINFOEXW startupInfo{};
    startupInfo.StartupInfo.cb = sizeof(startupInfo);
    startupInfo.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.StartupInfo.hStdInput = inputHandle;
    startupInfo.StartupInfo.hStdOutput = outputHandle;
    startupInfo.StartupInfo.hStdError = outputHandle;
    startupInfo.lpAttributeList = attributeList;

    const std::wstring executable = utf8ToWide(options.executable);
    std::wstring commandLineString = makeCommandLine(options);
    std::vector<wchar_t> commandLine(commandLineString.begin(), commandLineString.end());
    commandLine.push_back(L'\0');
    const std::wstring workingDirectory = utf8ToWide(options.workingDirectory);

    PROCESS_INFORMATION processInfo{};
    const BOOL created = CreateProcessW(
        executable.c_str(), commandLine.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT,
        nullptr, workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
        &startupInfo.StartupInfo, &processInfo);

    DeleteProcThreadAttributeList(attributeList);
    CloseHandle(outputHandle);
    CloseHandle(inputHandle);
    if (!created) return nullptr;

    CloseHandle(processInfo.hThread);
    return std::make_unique<WindowsChildProcess>(
        processInfo.hProcess, processInfo.dwProcessId);
}

#endif // _WIN32

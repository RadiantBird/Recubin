#ifdef _WIN32

#include <Util/WindowsPlatform.hpp>
#include <windows26.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <filesystem>
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

#endif // _WIN32

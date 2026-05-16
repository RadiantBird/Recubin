#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>     // SHGetKnownFolderPath
#include <shobjidl.h>   // IShellLink
#include <objbase.h>    // CoInitialize
#include <shellapi.h>   // ShellExecuteExW, SHELLEXECUTEINFOW
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

static const wchar_t* ENGINE_EXE = L"RecubinEngine.exe";
static const char*    FLAG_FILE  = "launcher_initialized.flag";

// Create a desktop shortcut (.lnk) pointing to RecubinEngine.exe
static void createDesktopShortcut() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    // Replace launcher.exe with RecubinEngine.exe in the path
    fs::path enginePath = fs::path(exePath).parent_path() / ENGINE_EXE;

    // Get Desktop folder
    PWSTR desktopPath = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desktopPath))) return;

    fs::path lnkPath = fs::path(desktopPath) / (enginePath.stem().wstring() + L".lnk");
    CoTaskMemFree(desktopPath);

    CoInitialize(nullptr);

    IShellLinkW* pLink = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IShellLinkW, (void**)&pLink))) {
        CoUninitialize();
        return;
    }

    pLink->SetPath(enginePath.wstring().c_str());
    pLink->SetWorkingDirectory(enginePath.parent_path().wstring().c_str());

    IPersistFile* pFile = nullptr;
    if (SUCCEEDED(pLink->QueryInterface(IID_IPersistFile, (void**)&pFile))) {
        pFile->Save(lnkPath.wstring().c_str(), TRUE);
        pFile->Release();
    }
    pLink->Release();
    CoUninitialize();
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    // First-launch check
    bool firstLaunch = !fs::exists(FLAG_FILE);
    if (firstLaunch) {
        int result = MessageBoxW(nullptr,
            L"デスクトップにショートカットを作成しますか？",
            L"Recubin Launcher",
            MB_YESNO | MB_ICONQUESTION);
        if (result == IDYES) {
            createDesktopShortcut();
        }
        // Write flag file so we don't ask again
        HANDLE h = CreateFileA(FLAG_FILE, GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }

    // Launch the engine
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    fs::path enginePath = fs::path(exePath).parent_path() / ENGINE_EXE;

    std::wstring enginePathStr = enginePath.wstring();
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"open";
    sei.lpFile = enginePathStr.c_str();
    sei.nShow  = SW_SHOWNORMAL;
    ShellExecuteExW(&sei);

    return 0;
}

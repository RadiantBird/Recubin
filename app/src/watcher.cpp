#include <windows.h>
#include <commctrl.h>
#include <cstdlib>
#include <cstdio>
#include <cwchar>

int main(int argc, char* argv[]) {
    if (argc < 2) return 0;

    DWORD pid = std::atoi(argv[1]);

    HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!h) return 0;
    
    WaitForSingleObject(h, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeProcess(h, &exitCode);

    if (exitCode != 0) {
        wchar_t msg[512];

        std::swprintf(
            msg, 512,
            L"Recubin Studio が異常終了しました。\n\n"
            L"終了コード: 0x%08X%s",
            exitCode,
            L"\n\nよかったら報告してね<(_ _)>"
        );

        TASKDIALOGCONFIG dialogConfig{};
        dialogConfig.cbSize = sizeof(dialogConfig);
        dialogConfig.dwCommonButtons = TDCBF_OK;
        dialogConfig.pszWindowTitle = L"Recubin Studio - Error";
        dialogConfig.pszMainIcon = TD_ERROR_ICON;
        dialogConfig.pszContent = msg;
        // Keep the notification comfortably wider than the default narrow layout.
        dialogConfig.cxWidth = 360;

        const HRESULT dialogResult = TaskDialogIndirect(
            &dialogConfig,
            nullptr,
            nullptr,
            nullptr
        );
        if (FAILED(dialogResult)) {
            // Task dialogs require the Common Controls v6 activation context. Fall
            // back to the legacy notification if that context is unavailable.
            MessageBoxW(
                nullptr,
                msg,
                L"Recubin Studio - Error",
                MB_OK | MB_ICONERROR | MB_SYSTEMMODAL
            );
        }
    }

    CloseHandle(h);
    return 0;
}

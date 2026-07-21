#include <windows.h>
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

        MessageBoxW(
            nullptr,
            msg,
            L"Recubin Studio - Error",
            MB_OK | MB_ICONERROR | MB_SYSTEMMODAL
        );
    }

    CloseHandle(h);
    return 0;
}
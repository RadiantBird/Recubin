#include <Util/FileDialog.hpp>
#include <windows26.h>
#include <shobjidl.h>

std::string browseFile(const wchar_t* filterName, const wchar_t* filterSpec) {
    std::string result;
    IFileOpenDialog* pfd = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                   CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd)))) {
        COMDLG_FILTERSPEC filter = { filterName, filterSpec };
        pfd->SetFileTypes(1, &filter);
        if (SUCCEEDED(pfd->Show(nullptr))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(pfd->GetResult(&item))) {
                PWSTR wpath = nullptr;
                item->GetDisplayName(SIGDN_FILESYSPATH, &wpath);
                int len = WideCharToMultiByte(CP_UTF8, 0, wpath, -1, nullptr, 0, nullptr, nullptr);
                if (len > 1) { result.resize(len - 1); WideCharToMultiByte(CP_UTF8, 0, wpath, -1, result.data(), len, nullptr, nullptr); }
                CoTaskMemFree(wpath);
                item->Release();
            }
        }
        pfd->Release();
    }
    return result;
}

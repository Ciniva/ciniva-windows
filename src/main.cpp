#include "app/main_window.h"
#include "app/timing.h"

#include <objbase.h>
#include <shellapi.h>

#include <cwchar>
#include <string>

namespace {

std::string narrow(const wchar_t* w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

ciniva::LaunchOptions parse_args() {
    ciniva::LaunchOptions o;
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; ++i) {
        const wchar_t* a = argv[i];
        if (std::wcsncmp(a, L"--start=", 8) == 0) o.start = std::wcstod(a + 8, nullptr);
        else if (std::wcscmp(a, L"--exit-after-first-frame") == 0) o.exit_after_first_frame = true;
        else if (a[0] != L'-') o.url = narrow(a);
    }
    LocalFree(argv);
    return o;
}

}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int show) {
    ciniva::timing::init();
    ciniva::timing::mark("wWinMain");
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    ciniva::MainWindow window;
    if (!window.create(parse_args(), show)) return 1;

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    ciniva::timing::mark("exit");
    return static_cast<int>(msg.wParam);
}

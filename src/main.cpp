#include "app/main_window.h"
#include "app/timing.h"
#include "app/updater.h"
#include "core/text.h"

#include <objbase.h>
#include <shellapi.h>

#include <cwchar>
#include <string>

namespace {

struct Args {
    ciniva::LaunchOptions launch;
    bool install = false;
};

Args parse_args() {
    Args o;
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; ++i) {
        const wchar_t* a = argv[i];
        if (std::wcsncmp(a, L"--start=", 8) == 0) o.launch.start = std::wcstod(a + 8, nullptr);
        else if (std::wcscmp(a, L"--exit-after-first-frame") == 0) o.launch.exit_after_first_frame = true;
        else if (std::wcscmp(a, L"--install") == 0) o.install = true;
        else if (a[0] != L'-') o.launch.url = ciniva::narrow(a);
    }
    LocalFree(argv);
    return o;
}

}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int show) {
    ciniva::timing::init();
    ciniva::timing::mark("wWinMain");
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    Args args = parse_args();
    if (args.install) {
        std::wstring error;
        bool ok = ciniva::Updater::install_per_user(&error);
        if (!ok) MessageBoxW(nullptr, error.c_str(), L"Ciniva", MB_ICONERROR);
        CoUninitialize();
        return ok ? 0 : 1;
    }
    ciniva::Updater::remove_leftovers();

    ciniva::MainWindow window;
    if (!window.create(args.launch, show)) return 1;

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    ciniva::timing::mark("exit");
    return static_cast<int>(msg.wParam);
}

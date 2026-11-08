#include "app/video_host.h"

namespace ciniva {

static const wchar_t* kClass = L"CinivaVideoHost";

bool VideoHost::create(HWND parent) {
    static bool registered = false;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    if (!registered) {
        WNDCLASSEXW wc = {sizeof wc};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = inst;
        wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        wc.lpszClassName = kClass;
        if (!RegisterClassExW(&wc)) return false;
        registered = true;
    }
    // Disabled so that mouse input falls through to the main window, which owns all interaction.
    hwnd_ = CreateWindowExW(0, kClass, L"", WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_DISABLED, 0, 0, 0, 0,
                            parent, nullptr, inst, nullptr);
    return hwnd_ != nullptr;
}

void VideoHost::set_bounds(const RECT& rc) {
    if (hwnd_)
        SetWindowPos(hwnd_, nullptr, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
}

void VideoHost::show(bool visible) {
    if (hwnd_) ShowWindow(hwnd_, visible ? SW_SHOWNA : SW_HIDE);
}

}

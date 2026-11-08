#include "app/main_window.h"

#include "app/timing.h"
#include "version.h"

#include <dwmapi.h>
#include <windowsx.h>

#include <cstdio>

namespace ciniva {

namespace {

constexpr UINT WM_APP_MPV_WAKEUP = WM_APP + 1;
constexpr UINT WM_APP_MPV_READY = WM_APP + 2;
constexpr const wchar_t* kClass = L"CinivaMainWindow";

constexpr D2D1_COLOR_F kBackground = {0.071f, 0.071f, 0.078f, 1.0f};
constexpr D2D1_COLOR_F kAccent = {0.961f, 0.722f, 0.290f, 1.0f};
constexpr D2D1_COLOR_F kSecondary = {0.62f, 0.62f, 0.66f, 1.0f};

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

}

bool MainWindow::create(const LaunchOptions& options, int show) {
    options_ = options;
    HINSTANCE inst = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc = {sizeof wc};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hbrBackground = CreateSolidBrush(RGB(18, 18, 20));
    wc.lpszClassName = kClass;
    if (!RegisterClassExW(&wc)) return false;

    float s = static_cast<float>(GetDpiForSystem()) / 96.0f;
    int w = static_cast<int>(1280 * s), h = static_cast<int>(780 * s);
    hwnd_ = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, kClass, L"Ciniva", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                            CW_USEDEFAULT, CW_USEDEFAULT, w, h, nullptr, nullptr, inst, this);
    if (!hwnd_) return false;

    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof dark);

    if (!renderer_.init(hwnd_)) {
        MessageBoxW(hwnd_, L"Direct3D 11 is not available on this PC.", L"Ciniva", MB_ICONERROR);
        return false;
    }
    timing::mark("renderer ready");

    auto dw = renderer_.dwrite();
    dw->CreateTextFormat(L"Segoe UI Variable Display", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                         DWRITE_FONT_STRETCH_NORMAL, 44.0f, L"en-us", &title_format_);
    dw->CreateTextFormat(L"Segoe UI Variable Text", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                         DWRITE_FONT_STRETCH_NORMAL, 15.0f, L"en-us", &body_format_);
    for (auto* f : {title_format_.Get(), body_format_.Get()}) {
        f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    video_.create(hwnd_);
    status_ = options_.url.empty() ? L"Pass a media URL on the command line to play it." : L"Starting player…";

    ShowWindow(hwnd_, show);
    UpdateWindow(hwnd_);
    if (!options_.url.empty()) start_player();
    return true;
}

LRESULT CALLBACK MainWindow::proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    MainWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        self = static_cast<MainWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    return self ? self->handle(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT MainWindow::handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT:
        paint();
        ValidateRect(hwnd_, nullptr);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        renderer_.resize();
        layout();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_DPICHANGED: {
        renderer_.set_dpi(HIWORD(wp));
        auto* rc = reinterpret_cast<RECT*>(lp);
        SetWindowPos(hwnd_, nullptr, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        float s = static_cast<float>(GetDpiForWindow(hwnd_)) / 96.0f;
        mmi->ptMinTrackSize = {static_cast<LONG>(640 * s), static_cast<LONG>(400 * s)};
        return 0;
    }
    case WM_KEYDOWN:
        on_key(wp);
        return 0;
    case WM_LBUTTONDBLCLK:
        toggle_fullscreen();
        return 0;
    case WM_APP_MPV_READY:
        if (player_) player_->on_ready_message(wp);
        return 0;
    case WM_APP_MPV_WAKEUP:
        if (player_) player_->drain_events();
        return 0;
    case WM_CLOSE:
        player_.reset();
        DestroyWindow(hwnd_);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

void MainWindow::layout() {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    video_.set_bounds(rc);
}

void MainWindow::paint() {
    auto* ctx = renderer_.begin();
    if (!ctx) return;
    ctx->Clear(video_.visible() ? D2D1::ColorF(0, 0, 0) : kBackground);
    if (!video_.visible()) {
        auto size = renderer_.size_dip();
        ComPtr<ID2D1SolidColorBrush> accent, secondary;
        ctx->CreateSolidColorBrush(kAccent, &accent);
        ctx->CreateSolidColorBrush(kSecondary, &secondary);
        float mid = size.height / 2;
        ctx->DrawText(L"Ciniva", 6, title_format_.Get(), D2D1::RectF(0, mid - 60, size.width, mid), accent.Get());
        ctx->DrawText(status_.c_str(), static_cast<UINT32>(status_.size()), body_format_.Get(),
                      D2D1::RectF(24, mid + 4, size.width - 24, mid + 40), secondary.Get());
    }
    if (!renderer_.end()) {
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (!first_paint_done_) {
        first_paint_done_ = true;
        timing::mark("first frame presented");
        if (options_.exit_after_first_frame && options_.url.empty()) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    }
}

void MainWindow::start_player() {
    mpv::Player::Callbacks cb;
    cb.ready = [this](bool ok, const std::string& error) {
        if (!ok) {
            set_status(L"Player failed: " + widen(error));
            return;
        }
        layout();
        video_.show(true);
        InvalidateRect(hwnd_, nullptr, FALSE);
        player_->load(options_.url, options_.start);
    };
    cb.first_frame = [this] {
        auto& st = player_->state();
        timing::log("video: %s, primaries=%s, gamma=%s, hwdec=%s", st.video_format.c_str(), st.primaries.c_str(),
                    st.gamma.c_str(), st.hwdec.c_str());
        if (options_.exit_after_first_frame) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    };
    cb.ended = [this](const std::string& reason) {
        video_.show(false);
        set_status(reason == "eof" ? L"Playback finished." : L"Playback stopped: " + widen(reason));
        if (options_.exit_after_first_frame) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    };
    player_ = std::make_unique<mpv::Player>(hwnd_, WM_APP_MPV_WAKEUP, WM_APP_MPV_READY, std::move(cb));
    player_->start(video_.hwnd());
}

void MainWindow::on_key(WPARAM key) {
    bool shift = GetKeyState(VK_SHIFT) < 0;
    bool ctrl = GetKeyState(VK_CONTROL) < 0;
    double step = ctrl ? 60 : shift ? 30 : 10;
    switch (key) {
    case VK_SPACE:
        if (player_) player_->toggle_pause();
        break;
    case VK_LEFT:
        if (player_) player_->seek_relative(-step);
        break;
    case VK_RIGHT:
        if (player_) player_->seek_relative(step);
        break;
    case 'I':
        if (player_) player_->toggle_stats();
        break;
    case 'F':
        toggle_fullscreen();
        break;
    case VK_ESCAPE:
        if (fullscreen_) toggle_fullscreen();
        break;
    }
}

void MainWindow::toggle_fullscreen() {
    LONG_PTR style = GetWindowLongPtrW(hwnd_, GWL_STYLE);
    if (!fullscreen_) {
        MONITORINFO mi = {sizeof mi};
        if (!GetWindowPlacement(hwnd_, &saved_placement_) ||
            !GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY), &mi))
            return;
        SetWindowLongPtrW(hwnd_, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
        SetWindowPos(hwnd_, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        fullscreen_ = true;
    } else {
        SetWindowLongPtrW(hwnd_, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(hwnd_, &saved_placement_);
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        fullscreen_ = false;
    }
}

void MainWindow::set_status(std::wstring text) {
    status_ = std::move(text);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

}

#include "app/main_window.h"

#include "app/dispatch.h"
#include "app/timing.h"
#include "core/text.h"
#include "screens/login_screen.h"
#include "screens/shell_screen.h"
#include "ui/text_field.h"
#include "ui/widgets.h"
#include "version.h"

#include <dwmapi.h>
#include <windowsx.h>

namespace ciniva {

namespace {

constexpr UINT WM_APP_MPV_WAKEUP = WM_APP + 1;
constexpr UINT WM_APP_MPV_READY = WM_APP + 2;
constexpr UINT_PTR kToastTimer = 1;
constexpr const wchar_t* kClass = L"CinivaMainWindow";

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
    wc.hbrBackground = CreateSolidBrush(RGB(17, 17, 19));
    wc.lpszClassName = kClass;
    if (!RegisterClassExW(&wc)) return false;

    float s = static_cast<float>(GetDpiForSystem()) / 96.0f;
    int w = static_cast<int>(1360 * s), h = static_cast<int>(840 * s);
    hwnd_ = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, kClass, L"Ciniva", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                            CW_USEDEFAULT, CW_USEDEFAULT, w, h, nullptr, nullptr, inst, this);
    if (!hwnd_) return false;
    dispatch::init(hwnd_);

    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof dark);

    if (!renderer_.init(hwnd_)) {
        MessageBoxW(hwnd_, L"Direct3D 11 is not available on this PC.", L"Ciniva", MB_ICONERROR);
        return false;
    }
    timing::mark("renderer ready");

    images_ = std::make_unique<ui::ImageCache>([this] { invalidate(); });
    ui_ = std::make_unique<ui::Ui>(renderer_, *images_);
    video_.create(hwnd_);

    model_.on_changed = [this] {
        sync_screen();
        invalidate();
    };
    login_ = std::make_unique<LoginScreen>(hwnd_, model_);
    shell_ = std::make_unique<ShellScreen>(hwnd_, model_);

    ShowWindow(hwnd_, show);
    UpdateWindow(hwnd_);
    if (!options_.url.empty()) start_player();
    else model_.bootstrap();
    return true;
}

void MainWindow::sync_screen() {
    Screen* want = nullptr;
    if (!player_ || !video_.visible()) {
        if (model_.phase == AppModel::Phase::SignedOut) want = login_.get();
        else if (model_.phase == AppModel::Phase::Ready) want = shell_.get();
    }
    if (want != current_) {
        if (current_) current_->on_hide();
        ui_->reset_interaction();
        current_ = want;
        if (current_) current_->on_show();
    }
    if (!model_.toasts.empty() && !toast_timer_) {
        SetTimer(hwnd_, kToastTimer, 250, nullptr);
        toast_timer_ = true;
    }
}

void MainWindow::invalidate() { InvalidateRect(hwnd_, nullptr, FALSE); }

D2D1_POINT_2F MainWindow::to_dip(LPARAM lp) const {
    float s = renderer_.scale();
    return {static_cast<float>(GET_X_LPARAM(lp)) / s, static_cast<float>(GET_Y_LPARAM(lp)) / s};
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
        invalidate();
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
        mmi->ptMinTrackSize = {static_cast<LONG>(760 * s), static_cast<LONG>(480 * s)};
        return 0;
    }
    case WM_CTLCOLOREDIT:
        return ui::TextField::handle_ctlcolor(reinterpret_cast<HDC>(wp));
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT && ui_) {
            SetCursor(LoadCursorW(nullptr, ui_->hovering_clickable() ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_MOUSEMOVE:
        if (!tracking_mouse_) {
            TRACKMOUSEEVENT tme = {sizeof tme, TME_LEAVE, hwnd_, 0};
            TrackMouseEvent(&tme);
            tracking_mouse_ = true;
        }
        if (ui_ && ui_->on_mouse_move(to_dip(lp))) invalidate();
        return 0;
    case WM_MOUSELEAVE:
        tracking_mouse_ = false;
        if (ui_) ui_->on_mouse_leave();
        invalidate();
        return 0;
    case WM_LBUTTONDOWN:
        SetFocus(hwnd_);
        SetCapture(hwnd_);
        if (ui_) ui_->on_mouse_down(to_dip(lp));
        invalidate();
        return 0;
    case WM_LBUTTONUP:
        ReleaseCapture();
        if (ui_) ui_->on_mouse_up(to_dip(lp));
        invalidate();
        return 0;
    case WM_LBUTTONDBLCLK:
        if (player_ && video_.visible()) toggle_fullscreen();
        else if (ui_) ui_->on_double_click(to_dip(lp));
        return 0;
    case WM_XBUTTONUP:
        if (GET_XBUTTON_WPARAM(wp) == XBUTTON1) on_key(VK_BROWSER_BACK);
        return TRUE;
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd_, &pt);
        float s = renderer_.scale();
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        bool horizontal = msg == WM_MOUSEHWHEEL || (GET_KEYSTATE_WPARAM(wp) & MK_SHIFT);
        float amount = static_cast<float>(delta) / WHEEL_DELTA * 120.0f;
        if (msg == WM_MOUSEHWHEEL) amount = -amount;
        bool precise = delta % WHEEL_DELTA != 0;
        if (ui_ && ui_->on_wheel({pt.x / s, pt.y / s}, amount, horizontal, precise)) invalidate();
        return 0;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (msg == WM_SYSKEYDOWN && wp == VK_RETURN) {
            toggle_fullscreen();
            return 0;
        }
        if (msg == WM_KEYDOWN) {
            on_key(wp);
            return 0;
        }
        break;
    case WM_TIMER:
        if (wp == kToastTimer) {
            model_.expire_toasts(timing::now_seconds());
            if (model_.toasts.empty()) {
                KillTimer(hwnd_, kToastTimer);
                toast_timer_ = false;
            }
            invalidate();
        }
        return 0;
    case dispatch::WM_APP_INVOKE:
        dispatch::handle_invoke(lp);
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
    auto size = renderer_.size_dip();
    ui::Rect bounds = {0, 0, size.width, size.height};
    ui_->begin_frame(ctx, timing::now_seconds());
    if (video_.visible()) {
        ctx->Clear(D2D1::ColorF(0, 0, 0));
    } else if (current_) {
        current_->paint(*ui_, bounds);
    } else {
        ui_->fill(bounds, ui::theme::background);
        float mid = size.height / 2;
        ui_->text("Ciniva", ui::rect(0, mid - 50, size.width, 44), {ui::Font::Huge, ui::theme::accent, ui::Align::Center});
        ui::widgets::spinner(*ui_, {size.width / 2, mid + 24}, 10);
    }
    ui::widgets::toasts(*ui_, bounds, model_.toasts);
    ui_->end_frame();
    if (!renderer_.end()) {
        invalidate();
        return;
    }
    if (ui_->wants_frame()) invalidate();
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
            model_.toast("Player failed: " + error, Tone::Error);
            return;
        }
        layout();
        video_.show(true);
        sync_screen();
        invalidate();
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
        if (reason != "eof") model_.toast("Playback stopped: " + reason, Tone::Error);
        if (options_.exit_after_first_frame) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        sync_screen();
        invalidate();
    };
    player_ = std::make_unique<mpv::Player>(hwnd_, WM_APP_MPV_WAKEUP, WM_APP_MPV_READY, std::move(cb));
    player_->start(video_.hwnd());
}

void MainWindow::on_key(WPARAM key) {
    if (player_ && video_.visible()) {
        bool shift = GetKeyState(VK_SHIFT) < 0;
        bool ctrl = GetKeyState(VK_CONTROL) < 0;
        double step = ctrl ? 60 : shift ? 30 : 10;
        switch (key) {
        case VK_SPACE: player_->toggle_pause(); break;
        case VK_LEFT: player_->seek_relative(-step); break;
        case VK_RIGHT: player_->seek_relative(step); break;
        case 'I': player_->toggle_stats(); break;
        case 'F': toggle_fullscreen(); break;
        case VK_ESCAPE:
            if (fullscreen_) toggle_fullscreen();
            break;
        }
        return;
    }
    if (key == VK_F11) {
        toggle_fullscreen();
        return;
    }
    if (current_ && current_->on_key(key)) {
        invalidate();
        return;
    }
    if (ui_ && ui_->on_key(key)) invalidate();
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

}

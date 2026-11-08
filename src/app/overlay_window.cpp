#include "app/overlay_window.h"

#include "app/timing.h"

#include <windowsx.h>

namespace ciniva {

namespace {
constexpr const wchar_t* kClass = L"CinivaPlayerOverlay";
constexpr UINT_PTR kTick = 1;
}

bool OverlayWindow::create(HWND owner, Handlers handlers) {
    owner_ = owner;
    handlers_ = std::move(handlers);
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc = {sizeof wc};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);
    hwnd_ = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, kClass, L"", WS_POPUP, 0, 0,
                            1, 1, owner, nullptr, inst, this);
    if (!hwnd_) return false;
    sync_to_owner();
    if (!renderer_.init(hwnd_, true)) return false;
    images_ = std::make_unique<ui::ImageCache>([this] { invalidate(); });
    ui_ = std::make_unique<ui::Ui>(renderer_, *images_);
    return true;
}

void OverlayWindow::show(bool visible) {
    if (visible == visible_) return;
    visible_ = visible;
    if (visible) {
        sync_to_owner();
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        SetTimer(hwnd_, kTick, 250, nullptr);
        invalidate();
    } else {
        KillTimer(hwnd_, kTick);
        ShowWindow(hwnd_, SW_HIDE);
        ui_->reset_interaction();
    }
}

void OverlayWindow::sync_to_owner() {
    RECT rc;
    GetClientRect(owner_, &rc);
    MapWindowPoints(owner_, nullptr, reinterpret_cast<POINT*>(&rc), 2);
    RECT cur;
    GetWindowRect(hwnd_, &cur);
    if (EqualRect(&cur, &rc)) return;
    SetWindowPos(hwnd_, nullptr, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER);
}

void OverlayWindow::invalidate() {
    if (hwnd_ && visible_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void OverlayWindow::set_dpi(UINT dpi) {
    renderer_.set_dpi(dpi);
    invalidate();
}

D2D1_POINT_2F OverlayWindow::to_dip(LPARAM lp) const {
    float s = renderer_.scale();
    return {static_cast<float>(GET_X_LPARAM(lp)) / s, static_cast<float>(GET_Y_LPARAM(lp)) / s};
}

LRESULT CALLBACK OverlayWindow::proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    OverlayWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        self = static_cast<OverlayWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    return self ? self->handle(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

void OverlayWindow::paint() {
    auto* ctx = renderer_.begin();
    if (!ctx) return;
    auto size = renderer_.size_dip();
    ctx->Clear(D2D1::ColorF(0, 0, 0, 0));
    ui_->begin_frame(ctx, timing::now_seconds());
    if (handlers_.paint) handlers_.paint(*ui_, {0, 0, size.width, size.height});
    ui_->end_frame();
    if (!renderer_.end()) {
        invalidate();
        return;
    }
    if (ui_->wants_frame()) invalidate();
}

LRESULT OverlayWindow::handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT:
        paint();
        ValidateRect(hwnd_, nullptr);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        renderer_.resize();
        invalidate();
        return 0;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_TIMER:
        if (wp == kTick && handlers_.tick) handlers_.tick();
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            if (handlers_.cursor_hidden && handlers_.cursor_hidden()) SetCursor(nullptr);
            else SetCursor(LoadCursorW(nullptr, ui_->hovering_clickable() ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_MOUSEMOVE: {
        POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (!tracking_) {
            TRACKMOUSEEVENT tme = {sizeof tme, TME_LEAVE, hwnd_, 0};
            TrackMouseEvent(&tme);
            tracking_ = true;
        }
        // Windows sends synthetic moves when windows change under a still cursor; only real motion counts.
        if (p.x != last_mouse_.x || p.y != last_mouse_.y) {
            last_mouse_ = p;
            if (handlers_.activity) handlers_.activity();
        }
        ui_->on_mouse_move(to_dip(lp));
        invalidate();
        return 0;
    }
    case WM_MOUSELEAVE:
        tracking_ = false;
        ui_->on_mouse_leave();
        invalidate();
        return 0;
    case WM_LBUTTONDOWN:
        if (GetForegroundWindow() != owner_) SetForegroundWindow(owner_);
        SetFocus(owner_);
        SetCapture(hwnd_);
        if (handlers_.activity) handlers_.activity();
        ui_->on_mouse_down(to_dip(lp));
        invalidate();
        return 0;
    case WM_LBUTTONUP: {
        ReleaseCapture();
        bool had_press = ui_->on_mouse_up(to_dip(lp));
        if (!had_press && handlers_.click) handlers_.click(to_dip(lp));
        invalidate();
        return 0;
    }
    case WM_LBUTTONDBLCLK:
        if (!ui_->on_double_click(to_dip(lp)) && handlers_.double_click) handlers_.double_click(to_dip(lp));
        invalidate();
        return 0;
    case WM_XBUTTONUP:
        SendMessageW(owner_, msg, wp, lp);
        return TRUE;
    case WM_MOUSEWHEEL: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd_, &pt);
        float s = renderer_.scale();
        float amount = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA * 120.0f;
        if (!ui_->on_wheel({pt.x / s, pt.y / s}, amount, false, GET_WHEEL_DELTA_WPARAM(wp) % WHEEL_DELTA != 0) &&
            handlers_.wheel)
            handlers_.wheel({pt.x / s, pt.y / s}, amount);
        if (handlers_.activity) handlers_.activity();
        invalidate();
        return 0;
    }
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

}

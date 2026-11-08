#include "ui/text_field.h"

#include "core/text.h"

#include <commctrl.h>

#include <cmath>

namespace ciniva::ui {

namespace {
constexpr COLORREF kFieldBg = RGB(31, 31, 35);
constexpr COLORREF kFieldText = RGB(242, 242, 245);
}

TextField::~TextField() {
    if (hwnd_) DestroyWindow(hwnd_);
    if (font_) DeleteObject(font_);
}

bool TextField::create(HWND parent, Options options) {
    parent_ = parent;
    options_ = std::move(options);
    DWORD style = WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL | (options_.password ? ES_PASSWORD : 0);
    // Layered so the control gets its own DWM surface above the parent's flip-model swapchain.
    hwnd_ = CreateWindowExW(WS_EX_LAYERED, WC_EDITW, L"", style, 0, 0, 0, 0, parent, nullptr, GetModuleHandleW(nullptr),
                            nullptr);
    if (!hwnd_) return false;
    SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA);
    SendMessageW(hwnd_, EM_SETCUEBANNER, FALSE, reinterpret_cast<LPARAM>(options_.placeholder.c_str()));
    SetWindowSubclass(hwnd_, subclass, 1, reinterpret_cast<DWORD_PTR>(this));
    return true;
}

LRESULT TextField::handle_ctlcolor(HDC dc) {
    static HBRUSH brush = CreateSolidBrush(kFieldBg);
    SetTextColor(dc, kFieldText);
    SetBkColor(dc, kFieldBg);
    return reinterpret_cast<LRESULT>(brush);
}

void TextField::update_font(float scale) {
    if (font_ && std::fabs(scale - font_scale_) < 0.01f) return;
    if (font_) DeleteObject(font_);
    font_scale_ = scale;
    font_ = CreateFontW(-static_cast<int>(std::lround(15 * scale)), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    SendMessageW(hwnd_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
}

void TextField::paint(Ui& ui, const Rect& r, bool enabled) {
    if (!hwnd_) return;
    bool has_focus = focused();
    ui.fill_round(r, 8, theme::surface);
    ui.stroke_round(r, 8, has_focus ? theme::accent : theme::stroke, has_focus ? 1.5f : 1);

    float s = ui.renderer().scale();
    update_font(s);
    float line = 15 * s * 1.4f;
    RECT px;
    px.left = static_cast<LONG>(std::lround((r.left + 12) * s));
    px.right = static_cast<LONG>(std::lround((r.right - 12) * s));
    px.top = static_cast<LONG>(std::lround((r.top + r.bottom) / 2 * s - line / 2));
    px.bottom = px.top + static_cast<LONG>(std::lround(line));
    if (!EqualRect(&px, &placed_) || !IsWindowVisible(hwnd_)) {
        placed_ = px;
        SetWindowPos(hwnd_, HWND_TOP, px.left, px.top, px.right - px.left, px.bottom - px.top,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    EnableWindow(hwnd_, enabled);
}

void TextField::hide() {
    if (hwnd_ && IsWindowVisible(hwnd_)) {
        if (focused()) SetFocus(parent_);
        ShowWindow(hwnd_, SW_HIDE);
    }
}

void TextField::focus() {
    if (hwnd_) SetFocus(hwnd_);
}

bool TextField::focused() const { return hwnd_ && GetFocus() == hwnd_; }

void TextField::select_all() {
    if (hwnd_) SendMessageW(hwnd_, EM_SETSEL, 0, -1);
}

std::wstring TextField::text() const {
    if (!hwnd_) return {};
    int n = GetWindowTextLengthW(hwnd_);
    std::wstring s(n, L'\0');
    if (n) GetWindowTextW(hwnd_, s.data(), n + 1);
    return s;
}

std::string TextField::text_utf8() const { return narrow(text()); }

void TextField::set_text(const std::wstring& text) {
    if (hwnd_) SetWindowTextW(hwnd_, text.c_str());
}

LRESULT CALLBACK TextField::subclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
    auto* self = reinterpret_cast<TextField*>(data);
    switch (msg) {
    case WM_GETDLGCODE:
        return DLGC_WANTALLKEYS | DefSubclassProc(hwnd, msg, wp, lp);
    case WM_KEYDOWN:
        if (wp == VK_RETURN) {
            if (self->options_.on_submit) self->options_.on_submit();
            return 0;
        }
        if (wp == VK_ESCAPE) {
            if (self->options_.on_escape) self->options_.on_escape();
            return 0;
        }
        if (wp == VK_TAB) {
            if (self->options_.on_tab) self->options_.on_tab(GetKeyState(VK_SHIFT) < 0);
            return 0;
        }
        if (wp == 'A' && GetKeyState(VK_CONTROL) < 0) {
            SendMessageW(hwnd, EM_SETSEL, 0, -1);
            return 0;
        }
        break;
    case WM_CHAR:
        if (wp == '\r' || wp == '\t' || wp == 27 || wp == 1) return 0;
        break;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(self->parent_, nullptr, FALSE);
        break;
    }
    LRESULT r = DefSubclassProc(hwnd, msg, wp, lp);
    if (msg == WM_CHAR || (msg == WM_KEYDOWN && (wp == VK_DELETE || wp == VK_BACK)) || msg == WM_PASTE || msg == WM_CUT) {
        if (self->options_.on_change) self->options_.on_change();
    }
    return r;
}

}

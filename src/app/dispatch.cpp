#include "app/dispatch.h"

namespace ciniva::dispatch {

static HWND g_window = nullptr;

void init(HWND ui_window) { g_window = ui_window; }

void post(std::function<void()> fn) {
    auto* boxed = new std::function<void()>(std::move(fn));
    if (!g_window || !PostMessageW(g_window, WM_APP_INVOKE, 0, reinterpret_cast<LPARAM>(boxed))) delete boxed;
}

void handle_invoke(LPARAM lp) {
    std::unique_ptr<std::function<void()>> fn(reinterpret_cast<std::function<void()>*>(lp));
    (*fn)();
}

void background(std::function<void()> fn) {
    auto* boxed = new std::function<void()>(std::move(fn));
    auto run = [](PTP_CALLBACK_INSTANCE, void* ctx) {
        std::unique_ptr<std::function<void()>> f(static_cast<std::function<void()>*>(ctx));
        (*f)();
    };
    if (!TrySubmitThreadpoolCallback(run, boxed, nullptr)) {
        std::unique_ptr<std::function<void()>> f(boxed);
        (*f)();
    }
}

}

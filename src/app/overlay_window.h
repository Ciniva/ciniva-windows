#pragma once

#include "app/renderer.h"
#include "ui/images.h"
#include "ui/ui.h"

#include <functional>
#include <memory>

namespace ciniva {

// A transparent owned popup composited by DirectComposition above the video child window.
class OverlayWindow {
public:
    struct Handlers {
        std::function<void(ui::Ui&, const ui::Rect&)> paint;
        std::function<void()> activity;
        std::function<void()> tick;
        std::function<bool()> cursor_hidden;
        std::function<bool(D2D1_POINT_2F)> click;
        std::function<void(D2D1_POINT_2F)> double_click;
        std::function<void(D2D1_POINT_2F, float)> wheel;
    };

    bool create(HWND owner, Handlers handlers);
    void show(bool visible);
    bool visible() const { return visible_; }
    void sync_to_owner();
    void invalidate();
    void set_dpi(UINT dpi);
    HWND hwnd() const { return hwnd_; }
    ui::Ui& ui() { return *ui_; }

private:
    static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);
    void paint();
    D2D1_POINT_2F to_dip(LPARAM lp) const;

    HWND hwnd_ = nullptr;
    HWND owner_ = nullptr;
    Handlers handlers_;
    Renderer renderer_;
    std::unique_ptr<ui::ImageCache> images_;
    std::unique_ptr<ui::Ui> ui_;
    bool visible_ = false;
    bool tracking_ = false;
    bool pending_paint_ = false;
    POINT last_mouse_ = {-1, -1};
};

}

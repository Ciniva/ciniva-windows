#pragma once

#include "ui/ui.h"

#include <functional>
#include <string>

namespace ciniva::ui {

class TextField {
public:
    struct Options {
        std::wstring placeholder;
        bool password = false;
        std::function<void()> on_submit;
        std::function<void()> on_change;
        std::function<void()> on_escape;
        std::function<void(bool back)> on_tab;
    };

    TextField() = default;
    TextField(const TextField&) = delete;
    TextField& operator=(const TextField&) = delete;
    ~TextField();

    bool create(HWND parent, Options options);
    void paint(Ui& ui, const Rect& r, bool enabled = true);
    void hide();
    void focus();
    bool focused() const;
    void select_all();

    std::wstring text() const;
    std::string text_utf8() const;
    void set_text(const std::wstring& text);

    static LRESULT handle_ctlcolor(HDC dc);

private:
    static LRESULT CALLBACK subclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data);
    void update_font(float scale);

    HWND hwnd_ = nullptr;
    HWND parent_ = nullptr;
    HFONT font_ = nullptr;
    float font_scale_ = 0;
    Options options_;
    RECT placed_ = {};
};

}

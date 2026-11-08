#include "ui/widgets.h"

#include <algorithm>
#include <cmath>

namespace ciniva::ui::widgets {

void glyph(Ui& ui, const wchar_t* g, const Rect& r, Color c, Font font) {
    ui.text(std::wstring_view(g), r, {font, c, Align::Center});
}

float button_width(Ui& ui, std::string_view label, bool has_glyph) {
    return ui.measure(label, Font::BodyBold).width + 36 + (has_glyph ? 24 : 0);
}

ButtonState button(Ui& ui, Id id, const Rect& r, std::string_view label, Style style, std::function<void()> on_click,
                   bool enabled, const wchar_t* g) {
    ButtonState st = ui.region(id, r, enabled ? std::move(on_click) : nullptr);
    Color bg, fg;
    switch (style) {
    case Style::Primary:
        bg = theme::accent;
        fg = theme::accent_text;
        if (st.hover) bg = mix(bg, {1, 1, 1, 1}, 0.12f);
        break;
    case Style::Danger:
        bg = with_alpha(theme::error, st.hover ? 0.30f : 0.18f);
        fg = theme::error;
        break;
    case Style::Ghost:
        bg = st.hover ? Color{1, 1, 1, 0.08f} : Color{0, 0, 0, 0};
        fg = theme::text;
        break;
    default:
        bg = st.hover ? theme::surface_hover : Color{1, 1, 1, 0.10f};
        fg = theme::text;
        break;
    }
    if (st.pressed) bg = mix(bg, {0, 0, 0, 1}, 0.15f);
    if (!enabled) {
        bg = with_alpha(bg, 0.5f);
        fg = with_alpha(fg, 0.6f);
    }
    float radius = std::min(height(r) / 2, 10.0f);
    ui.fill_round(r, radius, bg);
    if (g) {
        float tw = ui.measure(label, Font::BodyBold).width;
        float total = tw + (label.empty() ? 16 : 24);
        float x = (r.left + r.right - total) / 2;
        glyph(ui, g, rect(x, r.top, 16, height(r)), fg, Font::IconSmall);
        if (!label.empty()) ui.text(label, rect(x + 24, r.top, tw + 2, height(r)), {Font::BodyBold, fg});
    } else {
        ui.text(label, r, {Font::BodyBold, fg, Align::Center});
    }
    if (st.focused) ui.focus_ring(r, radius);
    return st;
}

ButtonState icon_button(Ui& ui, Id id, const Rect& r, const wchar_t* g, std::function<void()> on_click, bool active,
                        Font font) {
    ButtonState st = ui.region(id, r, std::move(on_click));
    float radius = std::min(width(r), height(r)) / 2;
    if (st.hover || active) ui.fill_round(r, radius, Color{1, 1, 1, st.pressed ? 0.20f : active ? 0.16f : 0.10f});
    glyph(ui, g, r, active ? theme::accent : theme::text, font);
    if (st.focused) ui.focus_ring(r, radius);
    return st;
}

void spinner(Ui& ui, D2D1_POINT_2F c, float radius, Color color) {
    double t = ui.now();
    for (int i = 0; i < 8; ++i) {
        float a = static_cast<float>(i) / 8 * 6.2831853f;
        float phase = static_cast<float>(std::fmod(t * 1.2 - i / 8.0 + 8, 1.0));
        ui.circle({c.x + std::cos(a) * radius, c.y + std::sin(a) * radius}, radius * 0.18f,
                  with_alpha(color, 0.25f + 0.75f * (1 - phase)));
    }
    ui.request_frame();
}

void progress_bar(Ui& ui, const Rect& r, double fraction, Color fill, Color track) {
    float radius = height(r) / 2;
    ui.fill_round(r, radius, track);
    float w = static_cast<float>(std::clamp(fraction, 0.0, 1.0)) * width(r);
    if (w > 0) ui.fill_round(rect(r.left, r.top, std::max(w, height(r)), height(r)), radius, fill);
}

float chip_width(Ui& ui, std::string_view label) { return ui.measure(label, Font::SmallBold).width + 16; }

void chip(Ui& ui, const Rect& r, std::string_view label, Color bg, Color fg) {
    ui.fill_round(r, 6, bg);
    ui.text(label, r, {Font::SmallBold, fg, Align::Center});
}

void toasts(Ui& ui, const Rect& b, const std::vector<Toast>& list) {
    float y = b.bottom - 24;
    for (auto it = list.rbegin(); it != list.rend(); ++it) {
        float age = static_cast<float>(ui.now() - it->created);
        float left = static_cast<float>(it->expires - ui.now());
        float alpha = std::clamp(std::min(age / 0.2f, left / 0.3f), 0.0f, 1.0f);
        float w = std::min(460.0f, width(b) - 48);
        auto size = ui.measure(it->text, Font::Body, w - 40, true, 3);
        float h = size.height + 24;
        float slide = (1 - std::clamp(age / 0.2f, 0.0f, 1.0f)) * 12;
        Rect r = rect(b.right - 24 - w, y - h + slide, w, h);
        ui.fill_round(inset(r, -1, -1), 11, with_alpha({0, 0, 0, 0.5f}, alpha));
        ui.fill_round(r, 10, with_alpha({0.17f, 0.17f, 0.19f, 0.98f}, alpha));
        Color dot = it->tone == Tone::Error ? theme::error : it->tone == Tone::Success ? theme::success : theme::accent;
        ui.circle({r.left + 18, r.top + h / 2}, 4, with_alpha(dot, alpha));
        ui.text(it->text, rect(r.left + 32, r.top + 12, w - 44, h - 24),
                {Font::Body, with_alpha(theme::text, alpha), Align::Leading, true, false, 3});
        y -= h + 10;
        if (alpha < 1) ui.request_frame();
    }
}

}

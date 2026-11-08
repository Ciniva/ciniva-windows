#pragma once

#include "app/app_model.h"
#include "ui/ui.h"

#include <functional>
#include <string>

namespace ciniva::ui {

namespace icon {
constexpr const wchar_t* play = L"\uE768";
constexpr const wchar_t* pause = L"\uE769";
constexpr const wchar_t* back = L"\uE72B";
constexpr const wchar_t* search = L"\uE721";
constexpr const wchar_t* home = L"\uE80F";
constexpr const wchar_t* movie = L"\uE8B2";
constexpr const wchar_t* tv = L"\uE7F4";
constexpr const wchar_t* settings = L"\uE713";
constexpr const wchar_t* refresh = L"\uE72C";
constexpr const wchar_t* close = L"\uE711";
constexpr const wchar_t* volume = L"\uE767";
constexpr const wchar_t* mute = L"\uE74F";
constexpr const wchar_t* fullscreen = L"\uE740";
constexpr const wchar_t* exit_fullscreen = L"\uE73F";
constexpr const wchar_t* subtitles = L"\uE7F0";
constexpr const wchar_t* audio = L"\uE8D6";
constexpr const wchar_t* next = L"\uE893";
constexpr const wchar_t* previous = L"\uE892";
constexpr const wchar_t* download = L"\uE896";
constexpr const wchar_t* chevron_down = L"\uE70D";
constexpr const wchar_t* chevron_right = L"\uE76C";
constexpr const wchar_t* chevron_left = L"\uE76B";
constexpr const wchar_t* more = L"\uE712";
constexpr const wchar_t* check = L"\uE73E";
constexpr const wchar_t* plugin = L"\uEA86";
constexpr const wchar_t* sort = L"\uE8CB";
constexpr const wchar_t* person = L"\uE77B";
constexpr const wchar_t* list = L"\uE8FD";
constexpr const wchar_t* skip_forward = L"\uEB9D";
constexpr const wchar_t* skip_back = L"\uEB9E";
constexpr const wchar_t* eye = L"\uE7B3";
}

namespace widgets {

enum class Style { Primary, Secondary, Ghost, Danger };

ButtonState button(Ui& ui, Id id, const Rect& r, std::string_view label, Style style, std::function<void()> on_click,
                   bool enabled = true, const wchar_t* glyph = nullptr);
ButtonState icon_button(Ui& ui, Id id, const Rect& r, const wchar_t* glyph, std::function<void()> on_click,
                        bool active = false, Font font = Font::Icon);
float button_width(Ui& ui, std::string_view label, bool has_glyph = false);

void spinner(Ui& ui, D2D1_POINT_2F center, float radius, Color c = theme::secondary);
void progress_bar(Ui& ui, const Rect& r, double fraction, Color fill = theme::accent, Color track = {1, 1, 1, 0.18f});
void chip(Ui& ui, const Rect& r, std::string_view label, Color bg = {1, 1, 1, 0.10f}, Color fg = theme::text);
float chip_width(Ui& ui, std::string_view label);
void toasts(Ui& ui, const Rect& bounds, const std::vector<Toast>& list);
void glyph(Ui& ui, const wchar_t* g, const Rect& r, Color c, Font font = Font::Icon);

}

}

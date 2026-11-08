#pragma once

#include "app/app_model.h"
#include "ui/ui.h"

#include <functional>
#include <string>

namespace ciniva::ui {

namespace icon {
constexpr const wchar_t* play = L"";
constexpr const wchar_t* pause = L"";
constexpr const wchar_t* back = L"";
constexpr const wchar_t* search = L"";
constexpr const wchar_t* home = L"";
constexpr const wchar_t* movie = L"";
constexpr const wchar_t* tv = L"";
constexpr const wchar_t* settings = L"";
constexpr const wchar_t* refresh = L"";
constexpr const wchar_t* close = L"";
constexpr const wchar_t* volume = L"";
constexpr const wchar_t* mute = L"";
constexpr const wchar_t* fullscreen = L"";
constexpr const wchar_t* exit_fullscreen = L"";
constexpr const wchar_t* subtitles = L"";
constexpr const wchar_t* audio = L"";
constexpr const wchar_t* next = L"";
constexpr const wchar_t* previous = L"";
constexpr const wchar_t* download = L"";
constexpr const wchar_t* chevron_down = L"";
constexpr const wchar_t* chevron_right = L"";
constexpr const wchar_t* chevron_left = L"";
constexpr const wchar_t* more = L"";
constexpr const wchar_t* check = L"";
constexpr const wchar_t* plugin = L"";
constexpr const wchar_t* sort = L"";
constexpr const wchar_t* person = L"";
constexpr const wchar_t* list = L"";
constexpr const wchar_t* skip_forward = L"";
constexpr const wchar_t* skip_back = L"";
constexpr const wchar_t* eye = L"";
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

#include "screens/components.h"

#include "core/text.h"
#include "screens/pages.h"
#include "ui/widgets.h"

#include <algorithm>
#include <cmath>

namespace ciniva {

using namespace ui;

std::optional<std::string> Page::image(const std::optional<std::string>& path, const char* size) const {
    auto* c = client();
    return c ? c->image_url(path, size) : std::nullopt;
}

void Page::paint_page(Ui& ui, const Rect& r) {
    if (loaded_version_ != model().library_version) {
        loaded_version_ = model().library_version;
        load();
    }
    if (!loaded_ || !error_.empty()) {
        paint_state(ui, r);
        return;
    }
    ui.scroll_area(r, scroll_, false);
    float top = r.top - scroll_.offset;
    float end = paint_content(ui, r, top);
    scroll_.content = end - top;
    ui.end_scroll_area();
}

void Page::paint_state(Ui& ui, const Rect& r) {
    float mid = (r.top + r.bottom) / 2;
    if (!error_.empty()) {
        ui.text("Couldn't load " + title(), rect(r.left, mid - 40, width(r), 28), {Font::Heading, theme::text, Align::Center});
        ui.text(error_, rect(r.left + 40, mid - 6, width(r) - 80, 40),
                {Font::Body, theme::secondary, Align::Center, true, false, 2});
        widgets::button(ui, make_id("page.retry"), rect((r.left + r.right) / 2 - 50, mid + 44, 100, 34), "Try again",
                        widgets::Style::Secondary, [this] {
                            error_.clear();
                            loaded_ = false;
                            load();
                        });
        return;
    }
    widgets::spinner(ui, {(r.left + r.right) / 2, mid}, 11);
}

namespace cards {

float poster_height(float w) { return w * 1.5f + 46; }
float wide_height(float w) { return w * 9 / 16 + 46; }

static void watched_mark(Ui& ui, const Rect& img) {
    D2D1_POINT_2F c = {img.right - 16, img.top + 16};
    ui.circle(c, 11, theme::accent);
    widgets::glyph(ui, icon::check, rect(c.x - 11, c.y - 11, 22, 22), theme::accent_text, Font::IconSmall);
}

static void count_badge(Ui& ui, const Rect& img, int n) {
    std::string s = std::to_string(n);
    float w = std::max(22.0f, ui.measure(s, Font::SmallBold).width + 12);
    Rect b = rect(img.right - w - 6, img.top + 6, w, 20);
    ui.fill_round(b, 10, theme::accent);
    ui.text(s, b, {Font::SmallBold, theme::accent_text, Align::Center});
}

static void bottom_progress(Ui& ui, const Rect& img, std::optional<double> p, float radius) {
    if (!p || *p <= 0) return;
    Rect bar = rect(img.left, img.bottom - 4, width(img), 4);
    ui.push_clip(rect(img.left, img.top, width(img), height(img)));
    ui.fill(bar, {0, 0, 0, 0.6f});
    ui.fill(rect(bar.left, bar.top, width(bar) * static_cast<float>(std::clamp(*p, 0.0, 1.0)), 4), theme::accent);
    ui.pop_clip();
    (void)radius;
}

void poster(Ui& ui, Id id, const Rect& r, const Poster& p, std::function<void()> on_click) {
    if (r.bottom < ui.clip().top || r.top > ui.clip().bottom || r.right < ui.clip().left || r.left > ui.clip().right) {
        ui.region(id, r, std::move(on_click));
        return;
    }
    float w = width(r);
    Rect img = rect(r.left, r.top, w, w * 1.5f);
    auto st = ui.region(id, r, std::move(on_click));
    ui.image(p.image, img, 8, static_cast<int>(w * ui.renderer().scale() * 1.25f));
    if (st.hover) ui.fill_round(img, 8, {1, 1, 1, st.pressed ? 0.04f : 0.08f});
    if (p.watched) watched_mark(ui, img);
    else if (p.unwatched > 0) count_badge(ui, img, p.unwatched);
    bottom_progress(ui, img, p.progress, 8);
    if (st.focused || st.hover) ui.stroke_round(img, 8, st.focused ? theme::accent : Color{1, 1, 1, 0.25f}, st.focused ? 2.0f : 1.0f);
    ui.text(p.title, rect(r.left, img.bottom + 8, w, 20), {Font::BodyBold});
    if (!p.subtitle.empty()) ui.text(p.subtitle, rect(r.left, img.bottom + 27, w, 18), {Font::Small, theme::secondary});
}

void wide(Ui& ui, Id id, const Rect& r, const std::string& title, const std::string& subtitle,
          const std::optional<std::string>& image, std::optional<double> progress, std::function<void()> on_click) {
    float w = width(r);
    Rect img = rect(r.left, r.top, w, w * 9 / 16);
    auto st = ui.region(id, r, std::move(on_click));
    if (r.bottom < ui.clip().top || r.top > ui.clip().bottom || r.right < ui.clip().left || r.left > ui.clip().right) return;
    ui.image(image, img, 8, static_cast<int>(w * ui.renderer().scale()));
    ui.fill_round(img, 8, {0, 0, 0, st.hover ? 0.05f : 0.18f});
    D2D1_POINT_2F c = {(img.left + img.right) / 2, (img.top + img.bottom) / 2};
    ui.circle(c, st.hover ? 25.0f : 22.0f, {0, 0, 0, 0.45f});
    widgets::glyph(ui, icon::play, rect(c.x - 20 + 2, c.y - 20, 40, 40), {1, 1, 1, 0.95f});
    bottom_progress(ui, img, progress, 8);
    if (st.focused || st.hover) ui.stroke_round(img, 8, st.focused ? theme::accent : Color{1, 1, 1, 0.25f}, st.focused ? 2.0f : 1.0f);
    ui.text(title, rect(r.left, img.bottom + 8, w, 20), {Font::BodyBold});
    ui.text(subtitle, rect(r.left, img.bottom + 27, w, 18), {Font::Small, theme::secondary});
}

void download(Ui& ui, Id id, const Rect& r, const DownloadItem& d, std::function<void()> on_click) {
    float w = width(r);
    Rect img = rect(r.left, r.top, w, w * 9 / 16);
    auto st = ui.region(id, r, std::move(on_click));
    if (r.bottom < ui.clip().top || r.top > ui.clip().bottom || r.right < ui.clip().left || r.left > ui.clip().right) return;
    ui.fill_round(img, 8, st.hover ? Color{0.20f, 0.20f, 0.22f, 1} : Color{0.14f, 0.14f, 0.16f, 1});
    int percent = static_cast<int>(d.progress * 100);
    D2D1_POINT_2F c = {(img.left + img.right) / 2, (img.top + img.bottom) / 2 - 12};
    widgets::glyph(ui, d.playable ? icon::play : icon::download, rect(c.x - 20, c.y - 20, 40, 40), {1, 1, 1, 0.9f});
    ui.text(std::to_string(percent) + "%", rect(img.left, c.y + 20, w, 24), {Font::Subheading, theme::text, Align::Center});
    bottom_progress(ui, img, d.progress, 8);
    if (st.focused || st.hover) ui.stroke_round(img, 8, st.focused ? theme::accent : Color{1, 1, 1, 0.25f}, st.focused ? 2.0f : 1.0f);
    std::string ready = d.available_secs ? format_clock(*d.available_secs) + " ready" : "starting";
    ui.text(d.label(), rect(r.left, img.bottom + 8, w, 20), {Font::BodyBold});
    ui.text(d.playable ? "Watch now \xC2\xB7 " + ready : "Downloading " + std::to_string(percent) + "% \xC2\xB7 " + ready,
            rect(r.left, img.bottom + 27, w, 18), {Font::Small, theme::secondary});
}

Poster movie(const Client* c, const MovieCard& m) {
    Poster p;
    p.title = m.title;
    p.subtitle = m.year ? std::to_string(*m.year) : "";
    p.image = c ? c->image_url(m.poster_path, "w342") : std::nullopt;
    p.progress = m.progress();
    p.watched = m.watched.value_or(false);
    return p;
}

Poster show(const Client* c, const ShowCard& s) {
    Poster p;
    p.title = s.title;
    std::string eps = std::to_string(s.episodes) + (s.episodes == 1 ? " episode" : " episodes");
    p.subtitle = s.year ? std::to_string(*s.year) + " \xC2\xB7 " + eps : eps;
    p.image = c ? c->image_url(s.poster_path, "w342") : std::nullopt;
    p.watched = s.episodes > 0 && s.unwatched() == 0;
    p.unwatched = s.unwatched();
    return p;
}

void open_movie(Nav& nav, int id) { nav.push(make_movie_page(nav, id)); }
void open_show(Nav& nav, int id) { nav.push(make_show_page(nav, id)); }

void play_continue(AppModel& model, const ContinueItem& item) {
    PlaybackRequest r;
    r.file_id = item.file_id;
    r.title = item.title;
    r.subtitle = item.subtitle;
    r.show_id = item.show_id;
    model.play(r);
}

std::string continue_subtitle(const ContinueItem& item) {
    std::string s;
    if (item.next_up.value_or(false)) s = "Up next";
    if (item.subtitle && !item.subtitle->empty()) s += (s.empty() ? "" : " \xC2\xB7 ") + *item.subtitle;
    if (s.empty()) s = format_clock(static_cast<double>(item.duration_ms - item.position_ms) / 1000) + " left";
    return s;
}

float shelf(Ui& ui, std::string_view key, const Rect& r, float y, const std::string& title, Scroll& scroll, size_t count,
            float item_width, float item_height, const std::function<void(size_t, const Rect&)>& draw) {
    const float pad = 32, gap = 18;
    ui.text(title, rect(r.left + pad, y, width(r) - pad * 2, 28), {Font::Heading});
    y += 38;
    Rect row = {r.left, y - 6, r.right, y + item_height + 6};
    ui.scroll_area(row, scroll, true);
    float x0 = r.left + pad - scroll.offset;
    for (size_t i = 0; i < count; ++i) {
        float x = x0 + i * (item_width + gap);
        draw(i, rect(x, y, item_width, item_height));
    }
    scroll.content = count * (item_width + gap) - gap + pad * 2;
    ui.end_scroll_area();
    if (scroll.offset > 1) {
        ui.fill_gradient_h(rect(row.left, row.top, 28, height(row)), theme::background, with_alpha(theme::background, 0));
    }
    if (scroll.offset < scroll.max() - 1) {
        ui.fill_gradient_h(rect(row.right - 28, row.top, 28, height(row)), with_alpha(theme::background, 0), theme::background);
    }
    (void)key;
    return 38 + item_height + 10;
}

float poster_grid(Ui& ui, const Rect& r, float y, size_t count, const std::function<void(size_t, const Rect&)>& draw) {
    const float pad = 32, gap = 22, min_w = 150, max_w = 180;
    float avail = width(r) - pad * 2;
    int cols = std::max(1, static_cast<int>((avail + gap) / (min_w + gap)));
    float w = std::min(max_w, (avail - gap * (cols - 1)) / cols);
    float h = poster_height(w);
    size_t rows = (count + cols - 1) / cols;
    Rect clip = ui.clip();
    for (size_t i = 0; i < count; ++i) {
        size_t row = i / cols, col = i % cols;
        float top = y + row * (h + 26);
        if (top + h < clip.top - h || top > clip.bottom + h) continue;
        draw(i, rect(r.left + pad + col * (w + gap), top, w, h));
    }
    return rows ? rows * (h + 26) - 26 : 0;
}

void hero(Ui& ui, const Rect& r, const std::optional<std::string>& url) {
    ui.image(url, r, 0, static_cast<int>(std::min(width(r) * ui.renderer().scale(), 1920.0f)), theme::background);
    ui.fill_gradient(rect(r.left, r.top, width(r), height(r) * 0.35f), {0, 0, 0, 0.35f}, {0, 0, 0, 0});
    ui.fill_gradient(rect(r.left, r.top + height(r) * 0.35f, width(r), height(r) * 0.65f + 1),
                     with_alpha(theme::background, 0), theme::background);
}

float cast_row(Ui& ui, const Client* c, const Rect& r, float y, const std::vector<CastMember>& cast, Scroll& scroll) {
    if (cast.empty()) return 0;
    size_t n = std::min<size_t>(cast.size(), 24);
    return shelf(ui, "cast", r, y, "Cast", scroll, n, 96, 128, [&](size_t i, const Rect& cell) {
        const auto& m = cast[i];
        Rect img = rect(cell.left + 8, cell.top, 80, 80);
        ui.image(c ? c->image_url(m.profile_path, "w185") : std::nullopt, img, 40, static_cast<int>(80 * ui.renderer().scale()));
        if (!m.profile_path) {
            std::string initial = m.name && !m.name->empty() ? m.name->substr(0, 1) : "?";
            ui.text(initial, img, {Font::Heading, theme::tertiary, Align::Center});
        }
        ui.text(m.name.value_or(""), rect(cell.left, img.bottom + 8, 96, 18), {Font::SmallBold, theme::text, Align::Center});
        if (m.character)
            ui.text(*m.character, rect(cell.left, img.bottom + 25, 96, 16), {Font::Caption, theme::secondary, Align::Center});
    });
}

float badges(Ui& ui, float x, float y, float max_width, const std::vector<std::string>& list) {
    float cx = x, cy = y;
    for (auto& b : list) {
        float w = widgets::chip_width(ui, b);
        if (cx + w > x + max_width && cx > x) {
            cx = x;
            cy += 30;
        }
        Rect r = rect(cx, cy, w, 24);
        ui.stroke_round(r, 5, {1, 1, 1, 0.35f});
        ui.text(b, r, {Font::SmallBold, theme::text, Align::Center});
        cx += w + 8;
    }
    return list.empty() ? 0 : cy - y + 24;
}

}

}

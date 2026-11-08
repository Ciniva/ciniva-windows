#include "screens/pages.h"

#include "app/dispatch.h"
#include "app/timing.h"
#include "core/settings.h"
#include "core/text.h"
#include "ui/widgets.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>

namespace ciniva {

using namespace ui;

namespace {

constexpr float kPad = 32;
const char* kDot = "  \xC2\xB7  ";

std::string join(const std::vector<std::string>& parts, const char* sep) {
    std::string out;
    for (auto& p : parts) {
        if (p.empty()) continue;
        if (!out.empty()) out += sep;
        out += p;
    }
    return out;
}

std::string rating_text(std::optional<double> r) {
    if (!r || *r <= 0) return {};
    char buf[16];
    std::snprintf(buf, sizeof buf, "\xE2\x98\x85 %.1f", *r);
    return buf;
}

float paragraph(Ui& ui, const std::string& text, float x, float y, float w, Font font = Font::Body,
                Color color = theme::text) {
    if (text.empty()) return 0;
    auto size = ui.measure(text, font, w, true);
    ui.text(text, rect(x, y, w, size.height + 4), {font, color, Align::Leading, true, false});
    return size.height;
}

float page_header(Ui& ui, const Rect& r, float y, const std::string& title, const std::string& subtitle, bool back = false) {
    float x = r.left + kPad + (back ? 48 : 0);
    ui.text(title, rect(x, y, r.right - kPad - x, 36), {Font::Title});
    if (!subtitle.empty()) ui.text(subtitle, rect(x, y + 38, r.right - kPad - x, 20), {Font::Body, theme::secondary});
    return subtitle.empty() ? 50.0f : 72.0f;
}

class HomePage : public Page {
public:
    using Page::Page;
    std::string title() const override { return "Home"; }

    void load() override {
        load_downloads();
        auto alive = token();
        model().call([](const Client& c) { return c.home(); }, [this, alive](Result<HomeData> r) {
            if (alive.expired()) return;
            if (r.ok()) {
                home_ = std::move(*r.value);
                error_.clear();
            } else if (!home_) {
                error_ = r.error.message;
            }
            loaded_ = true;
        });
        schedule_downloads();
    }

protected:
    float paint_content(Ui& ui, const Rect& r, float y) override {
        last_painted_ = ui.now();
        float start = y;
        y += 28;
        y += page_header(ui, r, y, "Home", "");
        const auto* c = client();
        if (!downloads_.empty()) {
            y += cards::shelf(ui, "dl", r, y, "Downloading", dl_scroll_, downloads_.size(), cards::kWideWidth,
                              cards::wide_height(cards::kWideWidth), [&](size_t i, const Rect& cell) {
                                  const auto& d = downloads_[i];
                                  cards::download(ui, make_id("dl", static_cast<long long>(i)), cell, d, [this, d] {
                                      if (!d.playable) {
                                          model().toast("Not enough has downloaded yet; try again in a minute.");
                                          return;
                                      }
                                      PlaybackRequest req;
                                      req.title = d.title;
                                      req.subtitle = d.subtitle;
                                      req.download_id = d.id;
                                      model().play(req);
                                  });
                              }) + 20;
        }
        if (home_ && !home_->continue_watching.empty()) {
            auto& list = home_->continue_watching;
            y += cards::shelf(ui, "cw", r, y, "Continue Watching", cw_scroll_, list.size(), cards::kWideWidth,
                              cards::wide_height(cards::kWideWidth), [&](size_t i, const Rect& cell) {
                                  const auto& item = list[i];
                                  auto url = c ? c->image_url(item.image_path ? item.image_path : item.poster_path, "w780") : std::nullopt;
                                  cards::wide(ui, make_id("cw", item.file_id), cell, item.title, cards::continue_subtitle(item),
                                              url, item.progress(), [this, item] { cards::play_continue(model(), item); });
                              }) + 20;
        }
        if (home_ && !home_->recent_movies.empty()) {
            auto& list = home_->recent_movies;
            y += cards::shelf(ui, "rm", r, y, "Recently Added Movies", movies_scroll_, list.size(), cards::kPosterWidth,
                              cards::poster_height(cards::kPosterWidth), [&](size_t i, const Rect& cell) {
                                  int id = list[i].id;
                                  cards::poster(ui, make_id("rm", id), cell, cards::movie(c, list[i]),
                                                [this, id] { cards::open_movie(nav_, id); });
                              }) + 20;
        }
        if (home_ && !home_->recent_shows.empty()) {
            auto& list = home_->recent_shows;
            y += cards::shelf(ui, "rs", r, y, "Recently Added Shows", shows_scroll_, list.size(), cards::kPosterWidth,
                              cards::poster_height(cards::kPosterWidth), [&](size_t i, const Rect& cell) {
                                  int id = list[i].id;
                                  cards::poster(ui, make_id("rs", id), cell, cards::show(c, list[i]),
                                                [this, id] { cards::open_show(nav_, id); });
                              }) + 20;
        }
        if (home_ && home_->recent_movies.empty() && home_->recent_shows.empty() && home_->continue_watching.empty()) {
            ui.text("Your library is empty", rect(r.left, y + 80, width(r), 28), {Font::Heading, theme::text, Align::Center});
            ui.text("Add files to the media folder and rescan the library.", rect(r.left, y + 112, width(r), 20),
                    {Font::Body, theme::secondary, Align::Center});
            y += 160;
        }
        return y + 12 - start + start;
    }

private:
    void load_downloads() {
        auto alive = token();
        model().call([](const Client& c) { return c.downloads(); }, [this, alive](Result<std::vector<DownloadItem>> r) {
            if (alive.expired()) return;
            if (r.ok()) downloads_ = std::move(*r.value);
        });
    }

    void schedule_downloads() {
        if (polling_) return;
        polling_ = true;
        auto alive = token();
        dispatch::background([this, alive] {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            dispatch::post([this, alive] {
                if (alive.expired()) return;
                polling_ = false;
                if (timing::now_seconds() - last_painted_ < 15) load_downloads();
                schedule_downloads();
            });
        });
    }

    std::optional<HomeData> home_;
    std::vector<DownloadItem> downloads_;
    Scroll dl_scroll_, cw_scroll_, movies_scroll_, shows_scroll_;
    double last_painted_ = 0;
    bool polling_ = false;
};

class LibraryPage : public Page {
public:
    LibraryPage(Nav nav, LibraryKind kind) : Page(nav), kind_(kind) {}
    std::string title() const override { return kind_ == LibraryKind::Movies ? "Movies" : "Shows"; }

    void load() override {
        auto alive = token();
        std::string sort = sort_key();
        bool movies = kind_ == LibraryKind::Movies;
        struct Data {
            Result<std::vector<MovieCard>> movies;
            Result<std::vector<ShowCard>> shows;
            Result<HomeData> home;
            bool ok() const { return movies.ok() || shows.ok(); }
            ApiError error;
        };
        model().call(
            [sort, movies](const Client& c) {
                Data d;
                if (movies) d.movies = c.movies(sort);
                else d.shows = c.shows(sort);
                d.home = c.home();
                d.error = movies ? d.movies.error : d.shows.error;
                return d;
            },
            [this, alive, sort](Data d) {
                if (alive.expired() || sort != sort_key()) return;
                if (d.ok()) {
                    if (d.movies.ok()) movies_ = std::move(*d.movies.value);
                    if (d.shows.ok()) shows_ = std::move(*d.shows.value);
                    resume_.clear();
                    if (d.home.ok())
                        for (auto& item : d.home.value->continue_watching)
                            if (kind_ == LibraryKind::Movies ? item.movie_id.has_value() : item.show_id.has_value())
                                resume_.push_back(item);
                    error_.clear();
                } else {
                    error_ = d.error.message;
                }
                loaded_ = true;
            });
    }

protected:
    std::string sort_key() const {
        auto& s = Settings::get();
        return kind_ == LibraryKind::Movies ? s.sort_movies : s.sort_shows;
    }

    float paint_content(Ui& ui, const Rect& r, float y) override {
        float start = y;
        y += 28;
        size_t n = kind_ == LibraryKind::Movies ? movies_.size() : shows_.size();
        std::string count = std::to_string(n) + (kind_ == LibraryKind::Movies ? (n == 1 ? " movie" : " movies") : (n == 1 ? " show" : " shows"));
        page_header(ui, r, y, title(), count);

        static const std::pair<const char*, const char*> sorts[] = {
            {"title", "Title"}, {"added", "Recently Added"}, {"year", "Year"}, {"rating", "Rating"}};
        float x = r.right - kPad;
        for (int i = 3; i >= 0; --i) {
            float w = ui.measure(sorts[i].second, Font::SmallBold).width + 24;
            x -= w;
            bool on = sort_key() == sorts[i].first;
            Rect pill = rect(x, y + 6, w, 30);
            auto st = ui.region(make_id("sort", i), pill, [this, key = std::string(sorts[i].first)] {
                auto& s = Settings::get();
                (kind_ == LibraryKind::Movies ? s.sort_movies : s.sort_shows) = key;
                s.save();
                load();
            });
            ui.fill_round(pill, 15, on ? theme::accent : st.hover ? theme::surface_hover : theme::surface);
            ui.text(sorts[i].second, pill, {Font::SmallBold, on ? theme::accent_text : theme::text, Align::Center});
            if (st.focused) ui.focus_ring(pill, 15);
            x -= 8;
        }
        y += 84;

        const auto* c = client();
        if (!resume_.empty()) {
            y += cards::shelf(ui, "lcw", r, y, "Continue Watching", resume_scroll_, resume_.size(), cards::kWideWidth,
                              cards::wide_height(cards::kWideWidth), [&](size_t i, const Rect& cell) {
                                  const auto& item = resume_[i];
                                  auto url = c ? c->image_url(item.image_path ? item.image_path : item.poster_path, "w780") : std::nullopt;
                                  cards::wide(ui, make_id("lcw", item.file_id), cell, item.title, cards::continue_subtitle(item),
                                              url, item.progress(), [this, item] { cards::play_continue(model(), item); });
                              }) + 24;
        }
        if (n == 0) {
            ui.text("Nothing here yet", rect(r.left, y + 60, width(r), 28), {Font::Heading, theme::secondary, Align::Center});
            return y + 120 - start + start;
        }
        y += cards::poster_grid(ui, r, y, n, [&](size_t i, const Rect& cell) {
            if (kind_ == LibraryKind::Movies) {
                int id = movies_[i].id;
                cards::poster(ui, make_id("lm", id), cell, cards::movie(c, movies_[i]), [this, id] { cards::open_movie(nav_, id); });
            } else {
                int id = shows_[i].id;
                cards::poster(ui, make_id("ls", id), cell, cards::show(c, shows_[i]), [this, id] { cards::open_show(nav_, id); });
            }
        });
        return y + 40;
    }

private:
    LibraryKind kind_;
    std::vector<MovieCard> movies_;
    std::vector<ShowCard> shows_;
    std::vector<ContinueItem> resume_;
    Scroll resume_scroll_;
};

class SearchPage : public Page {
public:
    SearchPage(Nav nav, std::string query) : Page(nav), query_(std::move(query)) {}
    std::string title() const override { return "search results"; }

    void load() override {
        auto alive = token();
        std::string q = trim(query_);
        model().call([q](const Client& c) { return c.search(q); }, [this, alive](Result<SearchResults> r) {
            if (alive.expired()) return;
            if (r.ok()) {
                results_ = std::move(*r.value);
                error_.clear();
            } else {
                error_ = r.error.message;
            }
            loaded_ = true;
        });
    }

protected:
    float paint_content(Ui& ui, const Rect& r, float y) override {
        float start = y;
        y += 28;
        y += page_header(ui, r, y, "Results for \xE2\x80\x9C" + query_ + "\xE2\x80\x9D", "", show_back);
        const auto* c = client();
        if (results_.movies.empty() && results_.shows.empty()) {
            ui.text("No matches", rect(r.left, y + 60, width(r), 28), {Font::Heading, theme::secondary, Align::Center});
            return y + 120;
        }
        if (!results_.movies.empty()) {
            ui.text("Movies", rect(r.left + kPad, y, 400, 28), {Font::Heading});
            y += 40;
            y += cards::poster_grid(ui, r, y, results_.movies.size(), [&](size_t i, const Rect& cell) {
                int id = results_.movies[i].id;
                cards::poster(ui, make_id("sm", id), cell, cards::movie(c, results_.movies[i]), [this, id] { cards::open_movie(nav_, id); });
            }) + 32;
        }
        if (!results_.shows.empty()) {
            ui.text("Shows", rect(r.left + kPad, y, 400, 28), {Font::Heading});
            y += 40;
            y += cards::poster_grid(ui, r, y, results_.shows.size(), [&](size_t i, const Rect& cell) {
                int id = results_.shows[i].id;
                cards::poster(ui, make_id("ss", id), cell, cards::show(c, results_.shows[i]), [this, id] { cards::open_show(nav_, id); });
            }) + 32;
        }
        (void)start;
        return y + 8;
    }

private:
    std::string query_;
    SearchResults results_;
};

// Shared layout for movie and show pages: hero, poster, title block. Returns the y below the header.
float detail_header(Ui& ui, const Rect& r, float y, const std::optional<std::string>& backdrop,
                    const std::optional<std::string>& poster, float info_height_hint,
                    const std::function<float(float x, float y, float w)>& info) {
    float hero_h = std::clamp(width(r) * 0.36f, 300.0f, 460.0f);
    cards::hero(ui, rect(r.left, y, width(r), hero_h), backdrop);
    float top = y + hero_h - 150;
    float pw = 200, ph = 300;
    ui.fill_round(rect(r.left + kPad - 1, top - 1, pw + 2, ph + 2), 11, {0, 0, 0, 0.4f});
    ui.image(poster, rect(r.left + kPad, top, pw, ph), 10, static_cast<int>(pw * ui.renderer().scale()));
    float ix = r.left + kPad + pw + 32;
    float h = info(ix, top + 40, std::min(820.0f, r.right - kPad - ix));
    (void)info_height_hint;
    return std::max(top + ph, top + 40 + h) + 28;
}

class MovieDetailPage : public Page {
public:
    MovieDetailPage(Nav nav, int id) : Page(nav), id_(id) {}
    std::string title() const override { return movie_ ? movie_->title : "movie"; }
    bool has_hero() const override { return true; }

    void load() override {
        auto alive = token();
        model().call([id = id_](const Client& c) { return c.movie(id); }, [this, alive](Result<MovieDetail> r) {
            if (alive.expired()) return;
            if (r.ok()) {
                movie_ = std::move(*r.value);
                error_.clear();
            } else if (!movie_) {
                error_ = r.error.message;
            }
            loaded_ = true;
        });
    }

protected:
    const MediaFile* file() const {
        if (!movie_ || movie_->files.empty()) return nullptr;
        for (auto& f : movie_->files)
            if (f.id == file_id_) return &f;
        return &movie_->files.front();
    }

    void play(double start) {
        auto* f = file();
        if (!f) return;
        PlaybackRequest req;
        req.file_id = f->id;
        req.title = movie_->title;
        if (movie_->year) req.subtitle = std::to_string(*movie_->year);
        if (start >= 0) req.start_at = start;
        model().play(req);
    }

    float paint_content(Ui& ui, const Rect& r, float y) override {
        const auto& m = *movie_;
        auto* f = file();
        y = detail_header(ui, r, y, image(m.backdrop_path, "w1280"), image(m.poster_path, "w500"), 300,
                          [&](float x, float iy, float w) {
                              float y0 = iy;
                              auto ts = ui.measure(m.title, Font::Huge, w, true, 2);
                              ui.text(m.title, rect(x, iy, w, ts.height + 4), {Font::Huge, theme::text, Align::Leading, true, false, 2});
                              iy += ts.height + 8;
                              std::string genres = join(m.genres, ", ");
                              ui.text(join({m.year ? std::to_string(*m.year) : "", m.runtime ? format_runtime(*m.runtime) : "",
                                            rating_text(m.rating), genres}, kDot),
                                      rect(x, iy, w, 22), {Font::Body, theme::secondary});
                              iy += 30;
                              if (m.tagline && !m.tagline->empty()) {
                                  iy += paragraph(ui, *m.tagline, x, iy, w, Font::Body, theme::secondary) + 10;
                              }
                              iy += actions(ui, x, iy, w) + 16;
                              if (m.files.size() > 1) iy += versions(ui, x, iy, w) + 12;
                              if (f) {
                                  if (auto p = f->progress()) {
                                      widgets::progress_bar(ui, rect(x, iy + 7, 180, 5), *p);
                                      std::string left = format_clock(static_cast<double>(f->duration_ms.value_or(0) - f->position_ms.value_or(0)) / 1000);
                                      ui.text(left + " left \xC2\xB7 stopped at " + format_clock(static_cast<double>(f->position_ms.value_or(0)) / 1000),
                                              rect(x + 192, iy, w - 192, 20), {Font::Small, theme::secondary});
                                      iy += 30;
                                  }
                                  iy += file_details(ui, *f, x, iy, w) + 14;
                              } else {
                                  ui.text("No playable file.", rect(x, iy, w, 20), {Font::Body, theme::secondary});
                                  iy += 30;
                              }
                              if (m.overview) iy += paragraph(ui, *m.overview, x, iy, w) + 8;
                              return iy - y0;
                          });
        y += cards::cast_row(ui, client(), r, y, m.cast, cast_scroll_);
        return y + 32;
    }

    float actions(Ui& ui, float x, float y, float) {
        auto* f = file();
        bool watched = std::any_of(movie_->files.begin(), movie_->files.end(), [](const MediaFile& mf) { return mf.is_watched(); });
        float cx = x;
        if (f) {
            std::string label = f->progress() ? "Resume" : "Play";
            float w = std::max(120.0f, widgets::button_width(ui, label, true));
            widgets::button(ui, make_id("m.play"), rect(cx, y, w, 40), label, widgets::Style::Primary, [this] { play(-1); }, true, icon::play);
            cx += w + 10;
            if (f->progress()) {
                float w2 = widgets::button_width(ui, "Start Over", true);
                widgets::button(ui, make_id("m.restart"), rect(cx, y, w2, 40), "Start Over", widgets::Style::Secondary,
                                [this] { play(0); }, true, icon::refresh);
                cx += w2 + 10;
            }
        }
        std::string wl = watched ? "Mark Unwatched" : "Mark Watched";
        float w3 = widgets::button_width(ui, wl, true);
        widgets::button(ui, make_id("m.watched"), rect(cx, y, w3, 40), wl, widgets::Style::Secondary, [this, watched] {
            std::vector<int> ids;
            for (auto& mf : movie_->files) ids.push_back(mf.id);
            model().set_watched(ids, !watched);
        }, true, watched ? icon::eye : icon::check);
        return 40;
    }

    float versions(Ui& ui, float x, float y, float w) {
        ui.text("Version", rect(x, y, w, 18), {Font::SmallBold, theme::secondary});
        y += 24;
        float h = 24;
        auto* cur = file();
        for (auto& f : movie_->files) {
            std::vector<std::string> b = f.probe ? f.probe->tech_badges() : std::vector<std::string>{};
            if (b.size() > 3) b.resize(3);
            std::string label = join({join(b, " "), format_bytes(f.size), f.file_name()}, " \xC2\xB7 ");
            Rect row = rect(x, y, w, 30);
            bool on = cur && cur->id == f.id;
            auto st = ui.region(make_id("m.ver", f.id), row, [this, id = f.id] { file_id_ = id; });
            ui.fill_round(row, 8, on ? with_alpha(theme::accent, 0.18f) : st.hover ? theme::surface_hover : theme::surface);
            ui.text(label, inset(row, 12, 0), {Font::Small, on ? theme::accent : theme::text});
            if (st.focused) ui.focus_ring(row, 8);
            y += 34;
            h += 34;
        }
        return h;
    }

    static std::string credits_label(const MediaFile& f) {
        if (f.credits_start && *f.credits_start > 0) {
            std::string src = f.credits_source.value_or("");
            std::string how = src == "chapters" ? "from chapters" : src == "audio" ? "matched across the season"
                            : src == "picture"  ? "detected in the picture" : src == "manual" ? "set by you" : "detected";
            return "Credits at " + format_clock(*f.credits_start) + " (" + how + ")";
        }
        return f.credits_source.value_or("") == "manual" ? "No credits (set by you)" : "Credits not detected";
    }

public:
    static float file_details(Ui& ui, const MediaFile& f, float x, float y, float w) {
        float y0 = y;
        if (f.probe) y += cards::badges(ui, x, y, w, f.probe->tech_badges()) + 10;
        ui.text(f.file_name() + " \xC2\xB7 " + format_bytes(f.size), rect(x, y, w, 20), {Font::Small, theme::secondary});
        y += 22;
        ui.text(credits_label(f), rect(x, y, w, 20), {Font::Small, theme::secondary});
        y += 22;
        if (f.probe && (f.probe->audio.size() > 1 || !f.probe->subtitles.empty())) {
            std::vector<std::string> parts;
            if (f.probe->audio.size() > 1) parts.push_back(std::to_string(f.probe->audio.size()) + " audio tracks");
            if (!f.probe->subtitles.empty())
                parts.push_back(std::to_string(f.probe->subtitles.size()) + (f.probe->subtitles.size() == 1 ? " subtitle track" : " subtitle tracks"));
            ui.text(join(parts, " \xC2\xB7 "), rect(x, y, w, 20), {Font::Small, theme::secondary});
            y += 22;
        }
        return y - y0;
    }

private:
    int id_;
    int file_id_ = 0;
    std::optional<MovieDetail> movie_;
    Scroll cast_scroll_;
};

class ShowDetailPage : public Page {
public:
    ShowDetailPage(Nav nav, int id) : Page(nav), id_(id) {}
    std::string title() const override { return show_ ? show_->title : "show"; }
    bool has_hero() const override { return true; }

    void load() override {
        auto alive = token();
        model().call([id = id_](const Client& c) { return c.show(id); }, [this, alive](Result<ShowDetail> r) {
            if (alive.expired()) return;
            if (!r.ok()) {
                if (!show_) error_ = r.error.message;
                loaded_ = true;
                return;
            }
            show_ = std::move(*r.value);
            show_->seasons = ordered_seasons(show_->seasons);
            error_.clear();
            auto& seasons = show_->seasons;
            bool valid = season_ && std::any_of(seasons.begin(), seasons.end(), [&](const SeasonCard& s) { return s.season_number == *season_; });
            if (!valid) {
                auto it = std::find_if(seasons.begin(), seasons.end(), [](const SeasonCard& s) { return s.season_number > 0 && s.watched < s.available; });
                if (it == seasons.end()) it = seasons.begin();
                season_ = it == seasons.end() ? std::nullopt : std::optional<int>(it->season_number);
            }
            loaded_ = true;
            load_season();
        });
    }

protected:
    void load_season() {
        if (!season_) return;
        auto alive = token();
        int n = *season_;
        model().call([id = id_, n](const Client& c) { return c.season(id, n); }, [this, alive, n](Result<std::vector<Episode>> r) {
            if (alive.expired() || !season_ || *season_ != n) return;
            if (r.ok()) episodes_ = std::move(*r.value);
        });
    }

    const SeasonCard* current() const {
        if (!show_ || !season_) return nullptr;
        for (auto& s : show_->seasons)
            if (s.season_number == *season_) return &s;
        return nullptr;
    }

    const Episode* up_next() const {
        for (auto& e : episodes_)
            if (e.file() && !e.file()->is_watched()) return &e;
        return nullptr;
    }

    void play_episode(const Episode& e, std::optional<double> start) {
        auto* f = e.file();
        if (!f) return;
        PlaybackRequest req;
        req.file_id = f->id;
        req.title = show_->title;
        req.subtitle = e.label();
        req.show_id = show_->id;
        req.start_at = start;
        model().play(req);
    }

    float paint_content(Ui& ui, const Rect& r, float y) override {
        const auto& s = *show_;
        y = detail_header(ui, r, y, image(s.backdrop_path, "w1280"), image(s.poster_path, "w500"), 300,
                          [&](float x, float iy, float w) {
                              float y0 = iy;
                              auto ts = ui.measure(s.title, Font::Huge, w, true, 2);
                              ui.text(s.title, rect(x, iy, w, ts.height + 4), {Font::Huge, theme::text, Align::Leading, true, false, 2});
                              iy += ts.height + 8;
                              ui.text(join({s.year ? std::to_string(*s.year) : "", s.status.value_or(""), rating_text(s.rating),
                                            join(s.genres, ", ")}, kDot),
                                      rect(x, iy, w, 22), {Font::Body, theme::secondary});
                              iy += 34;
                              iy += actions(ui, x, iy) + 18;
                              if (s.overview) iy += paragraph(ui, *s.overview, x, iy, w) + 8;
                              return iy - y0;
                          });
        if (s.seasons.empty()) {
            ui.text("No episodes in the library.", rect(r.left + kPad, y, 400, 22), {Font::Body, theme::secondary});
            y += 40;
        } else {
            y += season_pills(ui, r, y) + 20;
            for (auto& e : episodes_) y += episode_row(ui, r, y, e) + 14;
            y += 12;
        }
        y += cards::cast_row(ui, client(), r, y, s.cast, cast_scroll_);
        return y + 32;
    }

    float actions(Ui& ui, float x, float y) {
        float cx = x;
        if (auto* e = up_next()) {
            std::string label = std::string(e->file()->progress() ? "Resume" : "Play") + " S" + std::to_string(e->season_number) +
                                " \xC2\xB7 E" + std::to_string(e->episode_number);
            float w = widgets::button_width(ui, label, true);
            Episode copy = *e;
            widgets::button(ui, make_id("s.play"), rect(cx, y, w, 40), label, widgets::Style::Primary,
                            [this, copy] { play_episode(copy, std::nullopt); }, true, icon::play);
            cx += w + 10;
        }
        if (auto* c = current()) {
            bool unwatched = c->watched < c->available;
            std::string label = "Mark " + c->label() + (unwatched ? " Watched" : " Unwatched");
            float w = widgets::button_width(ui, label, true);
            widgets::button(ui, make_id("s.watched"), rect(cx, y, w, 40), label, widgets::Style::Secondary, [this, unwatched] {
                std::vector<int> ids;
                for (auto& e : episodes_)
                    for (auto& f : e.files) ids.push_back(f.id);
                model().set_watched(ids, unwatched);
            }, true, unwatched ? icon::check : icon::eye);
        }
        return 40;
    }

    float season_pills(Ui& ui, const Rect& r, float y) {
        Rect row = {r.left, y, r.right, y + 36};
        ui.scroll_area(row, pills_scroll_, true);
        float x = r.left + kPad - pills_scroll_.offset;
        for (auto& se : show_->seasons) {
            std::string count = std::to_string(se.available) + (se.episode_count ? "/" + std::to_string(*se.episode_count) : "");
            float lw = ui.measure(se.label(), Font::SmallBold).width, cw = ui.measure(count, Font::Small).width;
            float w = lw + cw + 36;
            Rect pill = rect(x, y, w, 34);
            bool on = season_ && *season_ == se.season_number;
            auto st = ui.region(make_id("season", se.season_number), pill, [this, n = se.season_number] {
                if (season_ && *season_ == n) return;
                season_ = n;
                episodes_.clear();
                load_season();
            });
            ui.fill_round(pill, 17, on ? with_alpha(theme::accent, 0.9f) : st.hover ? theme::surface_hover : theme::surface);
            ui.text(se.label(), rect(x + 14, y, lw + 2, 34), {Font::SmallBold, on ? theme::accent_text : theme::text});
            ui.text(count, rect(x + 22 + lw, y, cw + 2, 34), {Font::Small, on ? with_alpha(theme::accent_text, 0.7f) : theme::secondary});
            if (st.focused) ui.focus_ring(pill, 17);
            x += w + 8;
        }
        pills_scroll_.content = x + pills_scroll_.offset - r.left + kPad;
        ui.end_scroll_area();
        return 36;
    }

    float episode_row(Ui& ui, const Rect& r, float y, const Episode& e) {
        const MediaFile* f = e.file();
        float x = r.left + kPad;
        float iw = 240, ih = 135;
        Rect img = rect(x, y, iw, ih);
        float alpha = f ? 1.0f : 0.5f;
        Episode copy = e;
        auto st = ui.region(make_id("ep", e.id), img, f ? std::function<void()>([this, copy] { play_episode(copy, std::nullopt); }) : nullptr);
        if (img.bottom >= ui.clip().top && img.top <= ui.clip().bottom) {
            ui.image(image(e.still_path, "w300"), img, 8, static_cast<int>(iw * ui.renderer().scale()), theme::surface, alpha);
            if (f) {
                D2D1_POINT_2F c = {(img.left + img.right) / 2, (img.top + img.bottom) / 2};
                ui.circle(c, st.hover ? 24.0f : 21.0f, {0, 0, 0, 0.45f});
                widgets::glyph(ui, icon::play, rect(c.x - 18, c.y - 18, 38, 38), {1, 1, 1, 0.95f});
                if (auto p = f->progress()) {
                    ui.fill(rect(img.left, img.bottom - 4, iw, 4), {0, 0, 0, 0.6f});
                    ui.fill(rect(img.left, img.bottom - 4, iw * static_cast<float>(*p), 4), theme::accent);
                }
                if (f->is_watched()) {
                    ui.circle({img.right - 16, img.top + 16}, 11, theme::accent);
                    widgets::glyph(ui, icon::check, rect(img.right - 27, img.top + 5, 22, 22), theme::accent_text, Font::IconSmall);
                }
            }
            if (st.focused || st.hover) ui.stroke_round(img, 8, st.focused ? theme::accent : Color{1, 1, 1, 0.25f}, st.focused ? 2.0f : 1.0f);
        }

        float tx = img.right + 18;
        float tw = r.right - kPad - tx - 48;
        ui.text(e.display_title(), rect(tx, y + 2, tw, 22), {Font::Subheading, with_alpha(theme::text, alpha)});
        std::vector<std::string> parts = {e.air_date.value_or(""), e.runtime ? std::to_string(*e.runtime) + "m" : ""};
        if (f) {
            std::vector<std::string> b = f->probe ? f->probe->tech_badges() : std::vector<std::string>{};
            if (b.size() > 2) b.resize(2);
            parts.push_back(join(b, " "));
            parts.push_back(f->time_left_label().value_or(""));
        } else {
            parts.push_back("Not in library");
        }
        ui.text(join(parts, " \xC2\xB7 "), rect(tx, y + 28, tw, 18), {Font::Small, with_alpha(theme::secondary, alpha)});
        if (e.overview && !e.overview->empty())
            ui.text(*e.overview, rect(tx, y + 52, tw, 60), {Font::Small, with_alpha(theme::secondary, alpha), Align::Leading, true, false, 3});

        if (f) {
            bool w = f->is_watched();
            int fid = f->id;
            widgets::icon_button(ui, make_id("ep.w", e.id), rect(r.right - kPad - 36, y + 2, 36, 36), w ? icon::check : icon::check,
                                 [this, fid, w] { model().set_watched({fid}, !w); }, w);
        }
        return ih;
    }

private:
    int id_;
    std::optional<ShowDetail> show_;
    std::optional<int> season_;
    std::vector<Episode> episodes_;
    Scroll pills_scroll_, cast_scroll_;
};

}

std::unique_ptr<Page> make_home_page(Nav nav) { return std::make_unique<HomePage>(nav); }
std::unique_ptr<Page> make_library_page(Nav nav, LibraryKind kind) { return std::make_unique<LibraryPage>(nav, kind); }
std::unique_ptr<Page> make_search_page(Nav nav, const std::string& query) { return std::make_unique<SearchPage>(nav, query); }
std::unique_ptr<Page> make_movie_page(Nav nav, int id) { return std::make_unique<MovieDetailPage>(nav, id); }
std::unique_ptr<Page> make_show_page(Nav nav, int id) { return std::make_unique<ShowDetailPage>(nav, id); }

}

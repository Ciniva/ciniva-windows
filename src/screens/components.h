#pragma once

#include "app/app_model.h"
#include "ui/ui.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ciniva {

class Page;

struct Nav {
    AppModel& model;
    std::function<void(std::unique_ptr<Page>)> push;
    std::function<void()> back;
};

class Page {
public:
    explicit Page(Nav nav) : nav_(nav) {}
    virtual ~Page() = default;

    void paint_page(ui::Ui& ui, const ui::Rect& r);
    virtual std::string title() const = 0;
    virtual void load() = 0;
    virtual bool on_key(WPARAM) { return false; }
    virtual bool has_hero() const { return false; }
    bool show_back = false;

protected:
    virtual float paint_content(ui::Ui& ui, const ui::Rect& r, float y) = 0;
    void paint_state(ui::Ui& ui, const ui::Rect& r);

    std::weak_ptr<bool> token() const { return alive_; }
    AppModel& model() { return nav_.model; }
    const Client* client() const { return nav_.model.client ? &*nav_.model.client : nullptr; }
    std::optional<std::string> image(const std::optional<std::string>& path, const char* size) const;

    Nav nav_;
    ui::Scroll scroll_;
    bool loaded_ = false;
    std::string error_;
    int loaded_version_ = -1;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

namespace cards {

constexpr float kPosterWidth = 158;
constexpr float kWideWidth = 290;

struct Poster {
    std::string title;
    std::string subtitle;
    std::optional<std::string> image;
    std::optional<double> progress;
    bool watched = false;
    int unwatched = 0;
};

float poster_height(float width);
float wide_height(float width);
void poster(ui::Ui& ui, ui::Id id, const ui::Rect& r, const Poster& p, std::function<void()> on_click);
void wide(ui::Ui& ui, ui::Id id, const ui::Rect& r, const std::string& title, const std::string& subtitle,
          const std::optional<std::string>& image, std::optional<double> progress, std::function<void()> on_click);
void download(ui::Ui& ui, ui::Id id, const ui::Rect& r, const DownloadItem& d, std::function<void()> on_click);

Poster movie(const Client* c, const MovieCard& m);
Poster show(const Client* c, const ShowCard& s);
void open_movie(Nav& nav, int id);
void open_show(Nav& nav, int id);
void play_continue(AppModel& model, const ContinueItem& item);
std::string continue_subtitle(const ContinueItem& item);

// A titled horizontal row; returns its height.
float shelf(ui::Ui& ui, std::string_view key, const ui::Rect& r, float y, const std::string& title, ui::Scroll& scroll,
            size_t count, float item_width, float item_height,
            const std::function<void(size_t, const ui::Rect&)>& draw);

// A wrapping grid of posters; returns its height.
float poster_grid(ui::Ui& ui, const ui::Rect& r, float y, size_t count,
                  const std::function<void(size_t, const ui::Rect&)>& draw);

void hero(ui::Ui& ui, const ui::Rect& r, const std::optional<std::string>& url);
float cast_row(ui::Ui& ui, const Client* c, const ui::Rect& r, float y, const std::vector<CastMember>& cast,
               ui::Scroll& scroll);
float badges(ui::Ui& ui, float x, float y, float max_width, const std::vector<std::string>& list);

}

}

#include "player/player_screen.h"

#include "app/dispatch.h"
#include "app/timing.h"
#include "core/settings.h"
#include "core/text.h"
#include "ui/widgets.h"

#include <algorithm>
#include <cmath>

namespace ciniva {

using namespace ui;

namespace {

constexpr double kControlsTimeout = 3.0;

std::string join(const std::vector<std::string>& parts, const char* sep) {
    std::string out;
    for (auto& p : parts) {
        if (p.empty()) continue;
        if (!out.empty()) out += sep;
        out += p;
    }
    return out;
}

EpisodeTarget make_target(const ShowDetail& show, const Episode& e, int file_id) {
    return {file_id, show.id, show.title, e.label(), e.season_number, e.episode_number};
}

struct Neighbours {
    std::optional<ShowDetail> show;
    std::map<int, std::vector<Episode>> seasons;
    std::optional<EpisodeTarget> previous;
    std::optional<EpisodeTarget> next;
    ApiError error;
    bool ok() const { return show.has_value(); }
};

Neighbours find_neighbours(const Client& c, int show_id, int file_id) {
    Neighbours n;
    auto s = c.show(show_id);
    if (!s.ok()) {
        n.error = s.error;
        return n;
    }
    n.show = std::move(*s.value);
    std::vector<Episode> all;
    for (auto& season : ordered_seasons(n.show->seasons)) {
        auto eps = c.season(show_id, season.season_number);
        if (!eps.ok()) continue;
        n.seasons[season.season_number] = *eps.value;
        for (auto& e : *eps.value)
            if (!e.files.empty()) all.push_back(e);
    }
    if (auto* p = neighbour_episode(all, file_id, -1)) n.previous = make_target(*n.show, *p, p->files.front().id);
    if (auto* x = neighbour_episode(all, file_id, 1)) n.next = make_target(*n.show, *x, x->files.front().id);
    return n;
}

}

PlayerScreen::PlayerScreen(AppModel& model, mpv::Player& player, Host host)
    : model_(model), player_(player), host_(std::move(host)) {
    volume_ = Settings::get().volume;
}

PlayerScreen::~PlayerScreen() { *alive_ = false; }

void PlayerScreen::start(const PlaybackRequest& request) {
    if (active_) stop();
    ++session_;
    active_ = true;
    stopped_ = false;
    request_ = request;
    file_.reset();
    url_.clear();
    duration_ = 0;
    position_ = 0;
    paused_ = false;
    buffering_ = true;
    loaded_ = false;
    ended_ = false;
    start_offset_ = 0;
    resume_offer_.reset();
    error_.clear();
    up_next_.reset();
    previous_.reset();
    next_.reset();
    neighbours_loaded_ = false;
    pending_up_next_ = false;
    trickplay_.reset();
    available_secs_.reset();
    source_badges_.clear();
    tracks_.clear();
    credits_.reset();
    credits_shown_ = false;
    last_report_ = 0;
    menu_ = Menu::None;
    show_episodes_ = false;
    scrubbing_.reset();
    if (!show_ || !request.show_id || show_->id != *request.show_id) {
        show_.reset();
        seasons_.clear();
        panel_season_.reset();
    }
    bump();
    host_.show_video(true);

    int session = session_;
    std::weak_ptr<bool> alive = alive_;
    if (request.url) {
        url_ = *request.url;
        begin(request.start_at.value_or(0));
        return;
    }
    if (request.download_id) {
        std::string id = *request.download_id;
        model_.call([id](const Client& c) { return c.download(id); }, [this, alive, session, id](Result<DownloadItem> r) {
            if (alive.expired() || session != session_ || stopped_) return;
            if (!r.ok()) {
                error_ = r.error.message;
                buffering_ = false;
                return;
            }
            url_ = model_.client->download_stream_url(id);
            source_badges_ = r.value->probe ? r.value->probe->tech_badges() : std::vector<std::string>{};
            duration_ = r.value->duration.value_or(0);
            available_secs_ = r.value->available_secs;
            last_download_poll_ = timing::now_seconds();
            begin(request_.start_at.value_or(0));
        });
        return;
    }
    if (!request.file_id || !model_.client) return;
    int file_id = *request.file_id;
    model_.call([file_id](const Client& c) { return c.file(file_id); }, [this, alive, session, file_id](Result<MediaFile> r) {
        if (alive.expired() || session != session_ || stopped_) return;
        if (!r.ok()) {
            error_ = r.error.message;
            buffering_ = false;
            return;
        }
        file_ = std::move(*r.value);
        url_ = model_.client->stream_url(file_id);
        source_badges_ = file_->probe ? file_->probe->tech_badges() : std::vector<std::string>{};
        duration_ = file_->total_seconds();
        if (file_->credits_start && *file_->credits_start > 0 && (duration_ <= 0 || *file_->credits_start < duration_ - 5))
            credits_ = file_->credits_start;
        load_neighbours();
        model_.call([file_id](const Client& c) { return Result<std::optional<TrickplayManifest>>{c.trickplay(file_id), {}}; },
                    [this, alive, session](Result<std::optional<TrickplayManifest>> t) {
                        if (alive.expired() || session != session_) return;
                        if (t.ok()) trickplay_ = *t.value;
                    });
        if (request_.start_at) {
            begin(*request_.start_at);
            return;
        }
        double saved = file_->resume_seconds();
        if (saved >= 30 && (duration_ <= 0 || saved < duration_ - 15)) {
            resume_offer_ = saved;
            buffering_ = false;
        } else {
            begin(0);
        }
    });
}

void PlayerScreen::begin(double at) {
    at = std::max(0.0, at);
    if (duration_ > 0 && at > duration_ - 15) at = 0;
    start_offset_ = at;
    buffering_ = true;
    loaded_ = false;
    player_.set_volume(volume_);
    player_.load(url_, at);
    last_report_ = timing::now_seconds();
}

void PlayerScreen::load_neighbours() {
    if (!request_.show_id || !request_.file_id) {
        neighbours_loaded_ = true;
        return;
    }
    int show_id = *request_.show_id, file_id = *request_.file_id;
    int session = session_;
    std::weak_ptr<bool> alive = alive_;
    model_.call([show_id, file_id](const Client& c) {
        Neighbours n = find_neighbours(c, show_id, file_id);
        return Result<Neighbours>{n.ok() ? std::optional<Neighbours>(n) : std::nullopt, n.error};
    }, [this, alive, session](Result<Neighbours> r) {
        if (alive.expired() || session != session_) return;
        neighbours_loaded_ = true;
        if (r.ok()) {
            previous_ = r.value->previous;
            next_ = r.value->next;
            show_ = r.value->show;
            seasons_ = r.value->seasons;
        }
        if (pending_up_next_) {
            pending_up_next_ = false;
            show_up_next(pending_up_next_credits_);
        }
    });
}

void PlayerScreen::stop() {
    if (!active_) return;
    report(true);
    stopped_ = true;
    active_ = false;
    ++session_;
    player_.stop();
    host_.show_video(false);
}

void PlayerScreen::close() {
    stop();
    host_.close();
}

bool PlayerScreen::controls_visible() const {
    return timing::now_seconds() < controls_until_ || paused_ || show_episodes_ || menu_ != Menu::None ||
           scrubbing_.has_value() || dragging_volume_;
}

bool PlayerScreen::cursor_hidden() const { return active_ && !controls_visible() && !resume_offer_ && error_.empty(); }

void PlayerScreen::bump() {
    controls_until_ = timing::now_seconds() + kControlsTimeout;
    host_.redraw();
}

void PlayerScreen::on_activity() { bump(); }

void PlayerScreen::tick() {
    if (!active_) return;
    double now = timing::now_seconds();
    static bool was_visible = true;
    bool visible = controls_visible();
    if (visible != was_visible) host_.redraw();
    was_visible = visible;

    if (!paused_ && loaded_ && now - last_report_ >= 10) report(true);

    if (up_next_ && up_next_->countdown && now >= next_countdown_) {
        next_countdown_ = now + 1;
        if (*up_next_->countdown <= 1) {
            play_up_next();
            return;
        }
        up_next_->countdown = *up_next_->countdown - 1;
        host_.redraw();
    }

    if (request_.download_id && available_secs_ && now - last_download_poll_ >= 5) {
        last_download_poll_ = now;
        std::string id = *request_.download_id;
        int session = session_;
        std::weak_ptr<bool> alive = alive_;
        model_.call([id](const Client& c) { return c.download(id); }, [this, alive, session](Result<DownloadItem> r) {
            if (alive.expired() || session != session_) return;
            // The item disappears once the download finishes; from then on the whole file is seekable.
            if (r.ok()) available_secs_ = r.value->available_secs;
            else if (r.error.status == 404) available_secs_.reset();
        });
    }
}

void PlayerScreen::report(bool force) {
    if (!loaded_ || stopped_ || ended_ || !model_.client || !request_.file_id || duration_ <= 0 || position_ < 1) return;
    double now = timing::now_seconds();
    if (!force && now - last_report_ < 10) return;
    last_report_ = now;
    int id = *request_.file_id;
    long long pos = static_cast<long long>(position_ * 1000), dur = static_cast<long long>(duration_ * 1000);
    Client c = *model_.client;
    dispatch::background([c, id, pos, dur] { c.save_progress(id, pos, dur); });
}

void PlayerScreen::report_finished() {
    if (!model_.client || !request_.file_id || duration_ <= 0) return;
    int id = *request_.file_id;
    long long dur = static_cast<long long>(duration_ * 1000);
    Client c = *model_.client;
    dispatch::background([c, id, dur] { c.save_progress(id, dur, dur); });
}

void PlayerScreen::seek_to(double t) {
    if (!loaded_) return;
    double upper = std::max(0.0, duration_ - 1);
    if (available_secs_) upper = std::min(upper, std::max(0.0, *available_secs_ - 10));
    double target = std::clamp(t, 0.0, upper);
    position_ = target;
    player_.seek_absolute(target);
    check_credits(target);
    host_.redraw();
}

void PlayerScreen::seek_by(double delta) { seek_to(position_ + delta); }

void PlayerScreen::toggle_pause() {
    if (!loaded_) return;
    paused_ = !paused_;
    player_.set_paused(paused_);
    if (paused_) report(true);
    bump();
}

void PlayerScreen::set_volume(double v) {
    volume_ = std::clamp(v, 0.0, 150.0);
    player_.set_volume(volume_);
    auto& s = Settings::get();
    s.volume = volume_;
    s.save();
    host_.redraw();
}

void PlayerScreen::check_credits(double t) {
    if (!credits_ || !loaded_ || ended_) return;
    if (t < *credits_ - 1) {
        if (credits_shown_) hide_up_next();
        credits_shown_ = false;
    } else if (!credits_shown_ && (duration_ <= 0 || t < duration_ - 3)) {
        credits_shown_ = true;
        show_up_next(true);
    }
}

void PlayerScreen::show_up_next(bool at_credits) {
    if (stopped_) return;
    if (!neighbours_loaded_) {
        pending_up_next_ = true;
        pending_up_next_credits_ = at_credits;
        return;
    }
    if (!next_) {
        if (at_credits) up_next_ = UpNext{std::nullopt, std::nullopt, true};
        else close();
        host_.redraw();
        return;
    }
    bool autoplay = Settings::get().autoplay_next;
    up_next_ = UpNext{next_, autoplay ? std::optional<int>(10) : std::nullopt, at_credits};
    next_countdown_ = timing::now_seconds() + 1;
    host_.redraw();
}

void PlayerScreen::play_up_next() {
    auto target = up_next_ && up_next_->target ? up_next_->target : next_;
    up_next_.reset();
    if (target) play_target(*target, 0.0);
}

void PlayerScreen::hide_up_next() {
    up_next_.reset();
    host_.redraw();
}

void PlayerScreen::skip_credits() {
    hide_up_next();
    report_finished();
    ended_ = true;
    close();
}

void PlayerScreen::play_target(const EpisodeTarget& t, std::optional<double> at) {
    PlaybackRequest r;
    r.file_id = t.file_id;
    r.title = t.show_title;
    r.subtitle = t.label;
    r.show_id = t.show_id;
    r.start_at = at;
    start(r);
}

void PlayerScreen::on_file_loaded() {
    if (!active_) return;
    loaded_ = true;
    error_.clear();
    on_tracks_changed();
}

void PlayerScreen::on_changed() {
    if (!active_ || stopped_) return;
    const auto& st = player_.state();
    bool redraw = false;
    if (loaded_ && std::isfinite(st.position) && !scrubbing_) {
        if (std::floor(st.position * 4) != std::floor(position_ * 4)) redraw = true;
        position_ = st.position;
        check_credits(position_);
    }
    if (std::isfinite(st.duration) && st.duration > 0 && (duration_ <= 0 || std::fabs(duration_ - st.duration) > 2)) {
        duration_ = st.duration;
        redraw = true;
    }
    if (st.paused != paused_ && loaded_) {
        paused_ = st.paused;
        if (paused_) report(true);
        redraw = true;
    }
    bool buffering = st.buffering || st.seeking || !loaded_;
    if (buffering != buffering_) {
        buffering_ = buffering;
        redraw = true;
    }
    if (redraw && (controls_visible() || buffering_ || up_next_)) host_.redraw();
}

void PlayerScreen::on_tracks_changed() {
    if (!active_) return;
    tracks_ = player_.tracks();
    host_.redraw();
}

void PlayerScreen::on_ended(bool eof, const std::string& error) {
    if (!active_ || stopped_) return;
    if (!eof) {
        error_ = "Playback failed: " + error;
        buffering_ = false;
        host_.redraw();
        return;
    }
    if (ended_) return;
    ended_ = true;
    position_ = duration_;
    buffering_ = false;
    report_finished();
    if (up_next_ && up_next_->countdown) return;
    if (up_next_ && !up_next_->target) {
        close();
        return;
    }
    show_up_next(false);
}

double PlayerScreen::seek_step(bool shift, bool ctrl) const {
    const auto& s = Settings::get();
    return ctrl ? s.seek_long : shift ? s.seek_medium : s.seek_short;
}

void PlayerScreen::cycle_subtitles() {
    std::vector<const mpv::Track*> subs;
    for (auto& t : tracks_)
        if (t.type == "sub") subs.push_back(&t);
    if (subs.empty()) return;
    auto it = std::find_if(subs.begin(), subs.end(), [](const mpv::Track* t) { return t->selected; });
    if (it == subs.end()) player_.select_subtitle(subs.front()->id);
    else if (it + 1 == subs.end()) player_.select_subtitle("");
    else player_.select_subtitle((*(it + 1))->id);
}

bool PlayerScreen::on_key(WPARAM key) {
    if (!active_) return false;
    bool shift = GetKeyState(VK_SHIFT) < 0, ctrl = GetKeyState(VK_CONTROL) < 0;
    switch (key) {
    case VK_ESCAPE:
    case VK_BROWSER_BACK:
        if (resume_offer_) close();
        else if (menu_ != Menu::None) menu_ = Menu::None;
        else if (show_episodes_) show_episodes_ = false;
        else if (host_.is_fullscreen() && key == VK_ESCAPE) host_.toggle_fullscreen();
        else close();
        break;
    case VK_RETURN:
        if (resume_offer_) {
            double at = *resume_offer_;
            resume_offer_.reset();
            begin(at);
        } else if (up_next_ && up_next_->target) {
            play_up_next();
        }
        break;
    case VK_SPACE:
    case VK_MEDIA_PLAY_PAUSE:
        if (!resume_offer_) toggle_pause();
        break;
    case VK_LEFT:
    case VK_RIGHT:
        if (!resume_offer_) seek_by(key == VK_LEFT ? -seek_step(shift, ctrl) : seek_step(shift, ctrl));
        break;
    case VK_UP:
        set_volume(volume_ + 5);
        break;
    case VK_DOWN:
        set_volume(volume_ - 5);
        break;
    case 'M':
    case VK_VOLUME_MUTE:
        if (volume_ > 0) {
            volume_before_mute_ = volume_;
            set_volume(0);
        } else {
            set_volume(volume_before_mute_ > 0 ? volume_before_mute_ : 100);
        }
        break;
    case 'F':
    case VK_F11:
        host_.toggle_fullscreen();
        break;
    case 'I':
        if (shift) player_.toggle_stats();
        else show_info_ = !show_info_;
        break;
    case 'S':
        cycle_subtitles();
        break;
    case 'N':
    case VK_MEDIA_NEXT_TRACK:
        if ((shift || key == VK_MEDIA_NEXT_TRACK) && next_) play_target(*next_, 0.0);
        break;
    case 'P':
    case VK_MEDIA_PREV_TRACK:
        if ((shift || key == VK_MEDIA_PREV_TRACK) && previous_) play_target(*previous_, 0.0);
        break;
    case 'E':
        if (request_.show_id) show_episodes_ = !show_episodes_;
        break;
    default:
        return false;
    }
    bump();
    return true;
}

void PlayerScreen::on_click(D2D1_POINT_2F) {
    if (menu_ != Menu::None) {
        menu_ = Menu::None;
    } else if (show_episodes_) {
        show_episodes_ = false;
    } else if (!resume_offer_ && error_.empty()) {
        toggle_pause();
    }
    host_.redraw();
}

void PlayerScreen::on_double_click() {
    host_.toggle_fullscreen();
    toggle_pause();
}

void PlayerScreen::on_wheel(float delta) { set_volume(volume_ + (delta > 0 ? 5 : -5)); }

void PlayerScreen::load_season(int season) {
    panel_season_ = season;
    if (seasons_.count(season) || !request_.show_id) return;
    int show_id = *request_.show_id;
    std::weak_ptr<bool> alive = alive_;
    model_.call([show_id, season](const Client& c) { return c.season(show_id, season); },
                [this, alive, season](Result<std::vector<Episode>> r) {
                    if (alive.expired()) return;
                    if (r.ok()) seasons_[season] = std::move(*r.value);
                    host_.redraw();
                });
}

// MARK: Painting

void PlayerScreen::paint(Ui& ui, const Rect& b) {
    if (!active_) return;
    if (scrubbing_ && GetKeyState(VK_LBUTTON) >= 0) {
        double t = *scrubbing_;
        scrubbing_.reset();
        seek_to(t);
    }
    if (dragging_volume_ && GetKeyState(VK_LBUTTON) >= 0) dragging_volume_ = false;
    if (scrubbing_ && width(bar_rect_) > 0 && duration_ > 0) {
        float f = std::clamp((ui.mouse().x - bar_rect_.left) / width(bar_rect_), 0.0f, 1.0f);
        scrubbing_ = f * duration_;
    }
    if (dragging_volume_ && width(volume_rect_) > 0) {
        float f = std::clamp((ui.mouse().x - volume_rect_.left) / width(volume_rect_), 0.0f, 1.0f);
        if (std::fabs(f * 150 - volume_) >= 1) set_volume(std::round(f * 150));
    }

    if (buffering_ && error_.empty() && !resume_offer_) widgets::spinner(ui, {(b.left + b.right) / 2, (b.top + b.bottom) / 2}, 18, {1, 1, 1, 0.9f});
    if (!error_.empty()) {
        paint_error(ui, b);
        return;
    }
    Rect content = b;
    if (show_episodes_) content.right -= 400;
    if (controls_visible()) paint_controls(ui, content);
    if (up_next_) paint_up_next(ui, content);
    if (show_episodes_) paint_episodes(ui, {b.right - 400, b.top, b.right, b.bottom});
    if (menu_ != Menu::None) paint_menu(ui, b);
    if (resume_offer_) paint_resume(ui, b);
}

void PlayerScreen::paint_controls(Ui& ui, const Rect& b) {
    ui.fill_gradient(rect(b.left, b.top, width(b), 150), {0, 0, 0, 0.7f}, {0, 0, 0, 0});
    widgets::icon_button(ui, make_id("p.back"), rect(b.left + 24, b.top + 28, 40, 40), icon::back, [this] { close(); });
    ui.text(request_.title, rect(b.left + 80, b.top + 26, width(b) - 400, 30), {Font::Heading, {1, 1, 1, 1}});
    if (request_.subtitle)
        ui.text(*request_.subtitle, rect(b.left + 80, b.top + 56, width(b) - 400, 20), {Font::Body, {1, 1, 1, 0.7f}});
    if (show_info_) paint_info(ui, b);

    float bottom_h = 140;
    ui.fill_gradient(rect(b.left, b.bottom - bottom_h - 40, width(b), bottom_h + 40), {0, 0, 0, 0}, {0, 0, 0, 0.82f});
    float pad = 28;
    paint_scrubber(ui, rect(b.left + pad, b.bottom - 96, width(b) - pad * 2, 28));

    float y = b.bottom - 58, h = 40;
    float x = b.left + pad - 8;
    auto button = [&](std::string_view key, const wchar_t* glyph, std::function<void()> fn, bool enabled = true,
                      Font font = Font::Icon, float w = 40) {
        Rect r = rect(x, y, w, h);
        if (enabled) widgets::icon_button(ui, make_id(key), r, glyph, std::move(fn), false, font);
        else widgets::glyph(ui, glyph, r, {1, 1, 1, 0.3f}, font);
        x += w + 6;
        return r;
    };
    bool episode = request_.show_id.has_value();
    if (episode) button("p.prev", icon::previous, [this] { if (previous_) play_target(*previous_, 0.0); }, previous_.has_value());
    double step = Settings::get().seek_short;
    button("p.rew", icon::skip_back, [this, step] { seek_by(-step); });
    button("p.play", paused_ ? icon::play : icon::pause, [this] { toggle_pause(); }, true, Font::Icon, 48);
    button("p.fwd", icon::skip_forward, [this, step] { seek_by(step); });
    if (episode) button("p.next", icon::next, [this] { if (next_) play_target(*next_, 0.0); }, next_.has_value());

    x += 6;
    button("p.mute", volume_ <= 0 ? icon::mute : icon::volume, [this] {
        if (volume_ > 0) {
            volume_before_mute_ = volume_;
            set_volume(0);
        } else {
            set_volume(volume_before_mute_ > 0 ? volume_before_mute_ : 100);
        }
    });
    volume_rect_ = rect(x, y + h / 2 - 2, 100, 4);
    Rect vhit = rect(x - 4, y, 108, h);
    ui.region(make_id("p.vol"), vhit, [] {}, false);
    if (auto* reg = ui.last_region()) reg->on_press = [this](D2D1_POINT_2F) { dragging_volume_ = true; };
    widgets::progress_bar(ui, volume_rect_, volume_ / 150, {1, 1, 1, 0.95f}, {1, 1, 1, 0.25f});
    ui.circle({volume_rect_.left + static_cast<float>(volume_ / 150) * 100, volume_rect_.top + 2}, 6, {1, 1, 1, 1});
    x += 116;

    float rx = b.right - pad + 8;
    auto rbutton = [&](std::string_view key, const wchar_t* glyph, std::function<void()> fn, bool active = false) {
        rx -= 46;
        Rect r = rect(rx, y, 40, h);
        widgets::icon_button(ui, make_id(key), r, glyph, std::move(fn), active);
        return r;
    };
    rbutton("p.full", host_.is_fullscreen() ? icon::exit_fullscreen : icon::fullscreen, [this] { host_.toggle_fullscreen(); });
    rbutton("p.info", L"", [this] { show_info_ = !show_info_; }, show_info_);
    bool has_subs = std::any_of(tracks_.begin(), tracks_.end(), [](const mpv::Track& t) { return t.type == "sub"; });
    Rect subs = rbutton("p.subs", icon::subtitles, [this, has_subs] {
        if (has_subs) menu_ = menu_ == Menu::Subtitles ? Menu::None : Menu::Subtitles;
    }, menu_ == Menu::Subtitles);
    if (menu_ == Menu::Subtitles) menu_anchor_ = subs;
    Rect audio = rbutton("p.audio", icon::audio, [this] { menu_ = menu_ == Menu::Audio ? Menu::None : Menu::Audio; }, menu_ == Menu::Audio);
    if (menu_ == Menu::Audio) menu_anchor_ = audio;
    if (episode) rbutton("p.eps", icon::list, [this] { show_episodes_ = !show_episodes_; }, show_episodes_);

    std::vector<std::string> badges;
    for (auto& s : source_badges_)
        for (const char* p : {"4K", "Dolby Vision", "HDR", "HLG", "Atmos"})
            if (s.rfind(p, 0) == 0) {
                badges.push_back(s);
                break;
            }
    float bx = rx - 10;
    for (auto it = badges.rbegin(); it != badges.rend(); ++it) {
        float w = widgets::chip_width(ui, *it);
        if (bx - w < x) break;
        bx -= w;
        widgets::chip(ui, rect(bx, y + 9, w, 22), *it, {1, 1, 1, 0.15f});
        bx -= 6;
    }
}

void PlayerScreen::paint_scrubber(Ui& ui, const Rect& area) {
    double shown = scrubbing_.value_or(position_);
    float tw = 72;
    ui.text(format_clock(shown), rect(area.left, area.top, tw, height(area)), {Font::Small, {1, 1, 1, 0.9f}});
    ui.text("-" + format_clock(std::max(0.0, duration_ - shown)), rect(area.right - tw, area.top, tw, height(area)),
            {Font::Small, {1, 1, 1, 0.9f}, Align::Trailing});
    Rect track = rect(area.left + tw + 8, area.top + height(area) / 2 - 2, width(area) - tw * 2 - 16, 4);
    bar_rect_ = track;
    Rect hit = {track.left - 6, area.top, track.right + 6, area.bottom};
    auto st = ui.region(make_id("p.bar"), hit, [] {}, false);
    if (auto* reg = ui.last_region())
        reg->on_press = [this](D2D1_POINT_2F p) {
            if (duration_ <= 0 || !loaded_) return;
            float f = std::clamp((p.x - bar_rect_.left) / std::max(1.0f, width(bar_rect_)), 0.0f, 1.0f);
            scrubbing_ = f * duration_;
        };
    bool active = st.hover || scrubbing_;
    float th = active ? 6.0f : 4.0f;
    Rect bar = rect(track.left, track.top + 2 - th / 2, width(track), th);
    ui.fill_round(bar, th / 2, {1, 1, 1, 0.22f});
    if (available_secs_ && duration_ > 0)
        ui.fill_round(rect(bar.left, bar.top, width(bar) * static_cast<float>(std::min(1.0, *available_secs_ / duration_)), th),
                      th / 2, {1, 1, 1, 0.4f});
    float f = duration_ > 0 ? static_cast<float>(std::clamp(shown / duration_, 0.0, 1.0)) : 0;
    if (f > 0) ui.fill_round(rect(bar.left, bar.top, std::max(th, width(bar) * f), th), th / 2, theme::accent);
    if (credits_ && duration_ > 0) {
        float cx = bar.left + width(bar) * static_cast<float>(*credits_ / duration_);
        ui.fill(rect(cx - 1, bar.top - 2, 2, th + 4), {1, 1, 1, 0.6f});
    }
    if (active) ui.circle({bar.left + width(bar) * f, bar.top + th / 2}, 8, {1, 1, 1, 1});

    std::optional<float> hover_f;
    if (scrubbing_) hover_f = f;
    else if (st.hover) hover_f = std::clamp((ui.mouse().x - bar.left) / width(bar), 0.0f, 1.0f);
    if (hover_f && duration_ > 0) {
        double t = *hover_f * duration_;
        float cx = bar.left + width(bar) * *hover_f;
        float pw = 0, ph = 0;
        if (trickplay_ && trickplay_->count > 0 && trickplay_->width > 0 && file_) {
            ph = 112;
            pw = ph * trickplay_->width / std::max(1, trickplay_->height);
        }
        float px = std::clamp(cx - std::max(pw, 70.0f) / 2, track.left, track.right - std::max(pw, 70.0f));
        float py = bar.top - 18 - ph - 26;
        if (pw > 0) {
            auto tile = trickplay_->tile_at(t);
            Rect dest = rect(px, py, pw, ph);
            ui.fill_round(inset(dest, -2, -2), 6, {0, 0, 0, 0.85f});
            ui.push_clip(dest);
            if (!ui.image_tile(model_.client->trickplay_sheet_url(file_->id, tile.sheet),
                               rect(static_cast<float>(tile.x), static_cast<float>(tile.y), static_cast<float>(trickplay_->width),
                                    static_cast<float>(trickplay_->height)),
                               dest))
                ui.fill(dest, {0.1f, 0.1f, 0.1f, 1});
            ui.pop_clip();
            ui.stroke_round(inset(dest, -2, -2), 6, {1, 1, 1, 0.5f});
        }
        float lw = std::max(pw, 70.0f);
        Rect label = rect(px + (lw - 70) / 2, bar.top - 18 - 24, 70, 22);
        ui.fill_round(label, 5, {0, 0, 0, 0.8f});
        ui.text(format_clock(t), label, {Font::SmallBold, {1, 1, 1, 1}, Align::Center});
    }
}

void PlayerScreen::paint_menu(Ui& ui, const Rect& b) {
    std::vector<const mpv::Track*> items;
    for (auto& t : tracks_)
        if (t.type == (menu_ == Menu::Audio ? "audio" : "sub")) items.push_back(&t);
    float w = 340, row = 44;
    size_t n = items.size() + (menu_ == Menu::Subtitles ? 1 : 0);
    float h = std::min(static_cast<float>(n) * row + 16, height(b) - 200);
    float x = std::clamp(menu_anchor_.right - w, b.left + 16, b.right - w - 16);
    Rect r = rect(x, menu_anchor_.top - h - 10, w, h);
    ui.fill_round(inset(r, -1, -1), 13, {0, 0, 0, 0.5f});
    ui.fill_round(r, 12, {0.13f, 0.13f, 0.15f, 0.97f});
    ui.region(make_id("p.menu.bg"), r, [] {}, false);
    menu_scroll_.content = n * row + 16;
    ui.scroll_area(r, menu_scroll_, false);
    float y = r.top + 8 - menu_scroll_.offset;
    auto item = [&](const std::string& id, const std::string& title, const std::string& detail, bool selected) {
        Rect ir = rect(r.left + 6, y, w - 12, row - 2);
        auto st = ui.region(make_id("p.menu", static_cast<long long>(std::hash<std::string>{}(id))), ir, [this, id] {
            if (menu_ == Menu::Audio) player_.select_audio(id);
            else player_.select_subtitle(id);
            menu_ = Menu::None;
        });
        if (st.hover || st.focused) ui.fill_round(ir, 8, {1, 1, 1, 0.08f});
        if (selected) widgets::glyph(ui, icon::check, rect(ir.left + 4, ir.top, 24, row - 2), theme::accent, Font::IconSmall);
        if (detail.empty()) {
            ui.text(title, rect(ir.left + 34, ir.top, width(ir) - 40, row - 2), {Font::Body, {1, 1, 1, 1}});
        } else {
            ui.text(title, rect(ir.left + 34, ir.top + 4, width(ir) - 40, 20), {Font::Body, {1, 1, 1, 1}});
            ui.text(detail, rect(ir.left + 34, ir.top + 22, width(ir) - 40, 16), {Font::Caption, {1, 1, 1, 0.6f}});
        }
        y += row;
    };
    if (menu_ == Menu::Subtitles) {
        bool any = std::any_of(items.begin(), items.end(), [](const mpv::Track* t) { return t->selected; });
        item("", "Off", "", !any);
    }
    for (auto* t : items) item(t->id, t->title, t->detail, t->selected);
    ui.end_scroll_area();
}

void PlayerScreen::paint_up_next(Ui& ui, const Rect& b) {
    float w = 340, h = up_next_->target ? 132.0f : 72.0f;
    float bottom = controls_visible() ? 160.0f : 40.0f;
    Rect r = rect(b.right - w - 28, b.bottom - bottom - h, w, h);
    ui.fill_round(inset(r, -1, -1), 15, {0, 0, 0, 0.5f});
    ui.fill_round(r, 14, {0.13f, 0.13f, 0.15f, 0.96f});
    float x = r.left + 16, y = r.top + 16;
    if (up_next_->target) {
        ui.text("UP NEXT", rect(x, y, w - 32, 16), {Font::SmallBold, theme::accent});
        ui.text(up_next_->target->label, rect(x, y + 20, w - 32, 22), {Font::Subheading, {1, 1, 1, 1}});
        y += 58;
        std::string label = up_next_->countdown ? "Play in " + std::to_string(*up_next_->countdown) : "Play Now";
        float bw = widgets::button_width(ui, label, true);
        widgets::button(ui, make_id("p.upnext.play"), rect(x, y, bw, 40), label, widgets::Style::Primary, [this] { play_up_next(); },
                        true, icon::play);
        std::string other = up_next_->at_credits ? "Watch Credits" : "Cancel";
        widgets::button(ui, make_id("p.upnext.cancel"), rect(x + bw + 10, y, widgets::button_width(ui, other), 40), other,
                        widgets::Style::Secondary, [this] { hide_up_next(); });
    } else {
        float bw = widgets::button_width(ui, "Skip Credits");
        widgets::button(ui, make_id("p.skip"), rect(x, y, bw, 40), "Skip Credits", widgets::Style::Primary, [this] { skip_credits(); });
        widgets::button(ui, make_id("p.watchcredits"), rect(x + bw + 10, y, widgets::button_width(ui, "Watch Credits"), 40),
                        "Watch Credits", widgets::Style::Secondary, [this] { hide_up_next(); });
    }
}

void PlayerScreen::paint_resume(Ui& ui, const Rect& b) {
    ui.fill(b, {0, 0, 0, 0.55f});
    double saved = *resume_offer_;
    float w = 460, h = 216;
    Rect r = rect((b.left + b.right - w) / 2, (b.top + b.bottom - h) / 2, w, h);
    ui.region(make_id("p.resume.bg"), r, [] {}, false);
    ui.fill_round(r, 16, {0.13f, 0.13f, 0.15f, 0.98f});
    float x = r.left + 24, y = r.top + 22, iw = w - 48;
    ui.text("Continue watching?", rect(x, y, iw, 30), {Font::Heading, {1, 1, 1, 1}});
    y += 34;
    ui.text(join({request_.title, request_.subtitle.value_or("")}, " \xC2\xB7 "), rect(x, y, iw, 20), {Font::Body, theme::secondary});
    y += 30;
    if (duration_ > 0) widgets::progress_bar(ui, rect(x, y, iw, 5), std::min(1.0, saved / duration_));
    y += 14;
    ui.text("You stopped at " + format_clock(saved) + (duration_ > 0 ? " of " + format_clock(duration_) : "") + ".",
            rect(x, y, iw, 20), {Font::Small, theme::secondary});
    y += 36;
    std::string label = "Resume from " + format_clock(saved);
    float bw = widgets::button_width(ui, label, true);
    widgets::button(ui, make_id("p.resume"), rect(x, y, bw, 40), label, widgets::Style::Primary, [this, saved] {
        resume_offer_.reset();
        begin(saved);
    }, true, icon::play);
    float sw = widgets::button_width(ui, "Start Over");
    widgets::button(ui, make_id("p.startover"), rect(x + bw + 10, y, sw, 40), "Start Over", widgets::Style::Secondary, [this] {
        resume_offer_.reset();
        begin(0);
    });
    widgets::button(ui, make_id("p.cancel"), rect(r.right - 24 - 80, y, 80, 40), "Cancel", widgets::Style::Ghost, [this] { close(); });
}

void PlayerScreen::paint_error(Ui& ui, const Rect& b) {
    float w = 440, h = 150;
    Rect r = rect((b.left + b.right - w) / 2, (b.top + b.bottom - h) / 2, w, h);
    ui.fill_round(r, 14, {0.13f, 0.13f, 0.15f, 0.96f});
    ui.text(error_, rect(r.left + 24, r.top + 20, w - 48, 64), {Font::Body, {1, 1, 1, 1}, Align::Center, true, false, 3});
    widgets::button(ui, make_id("p.err.close"), rect((r.left + r.right) / 2 - 50, r.bottom - 58, 100, 38), "Close",
                    widgets::Style::Secondary, [this] { close(); });
}

void PlayerScreen::paint_info(Ui& ui, const Rect& b) {
    std::vector<std::string> lines = player_.video_info();
    const auto& st = player_.state();
    if (!st.target_gamma.empty())
        lines.push_back("Output: " + st.target_primaries + " / " + st.target_gamma +
                        (st.target_gamma == "pq" ? " (HDR10)" : st.target_gamma == "hlg" ? " (HLG)" : " (SDR)"));
    if (!source_badges_.empty()) lines.push_back("Source: " + join(source_badges_, " \xC2\xB7 "));
    if (file_) lines.push_back(file_->file_name());
    lines.push_back("Shift+I for mpv statistics");
    float w = 380, h = 16 + lines.size() * 22.0f + 8;
    Rect r = rect(b.right - w - 24, b.top + 24, w, h);
    ui.fill_round(r, 12, {0, 0, 0, 0.7f});
    float y = r.top + 12;
    for (auto& l : lines) {
        ui.text(l, rect(r.left + 14, y, w - 28, 20), {Font::Small, {1, 1, 1, 0.9f}});
        y += 22;
    }
}

void PlayerScreen::paint_episodes(Ui& ui, const Rect& r) {
    ui.fill(r, {0.07f, 0.07f, 0.08f, 0.96f});
    ui.region(make_id("p.eps.bg"), r, [] {}, false);
    float x = r.left + 18, w = width(r) - 36, y = r.top + 24;
    ui.text(show_ ? show_->title : request_.title, rect(x, y, w - 40, 26), {Font::Subheading, {1, 1, 1, 1}});
    widgets::icon_button(ui, make_id("p.eps.close"), rect(r.right - 52, y - 6, 36, 36), icon::close, [this] { show_episodes_ = false; });
    y += 40;
    if (!show_) {
        widgets::spinner(ui, {(r.left + r.right) / 2, y + 60}, 10);
        return;
    }
    auto seasons = ordered_seasons(show_->seasons);
    int current_season = 0;
    for (auto& [n, eps] : seasons_)
        for (auto& e : eps)
            for (auto& f : e.files)
                if (request_.file_id && f.id == *request_.file_id) current_season = n;
    if (!panel_season_) panel_season_ = current_season;
    Rect pills = {r.left, y, r.right, y + 32};
    ui.scroll_area(pills, season_scroll_, true);
    float px = x - season_scroll_.offset;
    for (auto& s : seasons) {
        std::string label = s.season_number == 0 ? "Specials" : "S" + std::to_string(s.season_number);
        float pw = ui.measure(label, Font::SmallBold).width + 24;
        Rect pr = rect(px, y, pw, 30);
        bool on = *panel_season_ == s.season_number;
        auto st = ui.region(make_id("p.eps.s", s.season_number), pr, [this, n = s.season_number] { load_season(n); });
        ui.fill_round(pr, 15, on ? theme::accent : st.hover ? Color{1, 1, 1, 0.16f} : Color{1, 1, 1, 0.08f});
        ui.text(label, pr, {Font::SmallBold, on ? theme::accent_text : Color{1, 1, 1, 1}, Align::Center});
        px += pw + 6;
    }
    season_scroll_.content = px + season_scroll_.offset - r.left + 18;
    ui.end_scroll_area();
    y += 44;

    Rect list = {r.left, y, r.right, r.bottom};
    auto it = seasons_.find(*panel_season_);
    if (it == seasons_.end()) {
        load_season(*panel_season_);
        widgets::spinner(ui, {(r.left + r.right) / 2, y + 60}, 10);
        return;
    }
    ui.scroll_area(list, episodes_scroll_, false);
    float ey = y - episodes_scroll_.offset;
    for (auto& e : it->second) {
        const MediaFile* f = e.file();
        Rect row = rect(x - 8, ey, w + 16, 86);
        bool current = f && request_.file_id && f->id == *request_.file_id;
        Episode copy = e;
        auto st = ui.region(make_id("p.eps.e", e.id), row, f ? std::function<void()>([this, copy] {
            if (show_) play_target(make_target(*show_, copy, copy.files.front().id), std::nullopt);
        }) : nullptr);
        if (current) ui.fill_round(row, 10, with_alpha(theme::accent, 0.16f));
        else if (st.hover || st.focused) ui.fill_round(row, 10, {1, 1, 1, 0.06f});
        Rect img = rect(x, ey + 8, 124, 70);
        ui.image(model_.client ? model_.client->image_url(e.still_path, "w300") : std::nullopt, img, 6,
                 static_cast<int>(124 * ui.renderer().scale()), {1, 1, 1, 0.06f}, f ? 1.0f : 0.4f);
        if (f && f->progress()) {
            ui.fill(rect(img.left, img.bottom - 3, width(img), 3), {0, 0, 0, 0.6f});
            ui.fill(rect(img.left, img.bottom - 3, width(img) * static_cast<float>(*f->progress()), 3), theme::accent);
        }
        if (f && f->is_watched()) {
            ui.circle({img.right - 12, img.top + 12}, 9, theme::accent);
            widgets::glyph(ui, icon::check, rect(img.right - 21, img.top + 3, 18, 18), theme::accent_text, Font::IconSmall);
        }
        float tx = img.right + 12, tw = row.right - tx - 8;
        ui.text(e.display_title(), rect(tx, ey + 10, tw, 20), {Font::BodyBold, {1, 1, 1, f ? 1.0f : 0.5f}});
        std::string meta = join({e.runtime ? std::to_string(*e.runtime) + "m" : "", f ? f->time_left_label().value_or("") : "Not in library"},
                                " \xC2\xB7 ");
        ui.text(meta, rect(tx, ey + 30, tw, 18), {Font::Caption, {1, 1, 1, 0.55f}});
        if (e.overview) ui.text(*e.overview, rect(tx, ey + 48, tw, 32), {Font::Caption, {1, 1, 1, 0.65f}, Align::Leading, true, false, 2});
        ey += 92;
    }
    episodes_scroll_.content = ey + episodes_scroll_.offset - y + 20;
    ui.end_scroll_area();
}

}

#pragma once

#include "app/app_model.h"
#include "mpv/player.h"
#include "ui/ui.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ciniva {

struct EpisodeTarget {
    int file_id = 0;
    int show_id = 0;
    std::string show_title;
    std::string label;
    int season = 0;
    int episode = 0;
};

class PlayerScreen {
public:
    struct Host {
        std::function<void()> close;
        std::function<void()> toggle_fullscreen;
        std::function<bool()> is_fullscreen;
        std::function<void()> redraw;
        std::function<void(bool)> show_video;
    };

    PlayerScreen(AppModel& model, mpv::Player& player, Host host);
    ~PlayerScreen();

    void start(const PlaybackRequest& request);
    void stop();
    bool active() const { return active_; }

    void paint(ui::Ui& ui, const ui::Rect& bounds);
    bool on_key(WPARAM key);
    void on_activity();
    void on_click(D2D1_POINT_2F p);
    void on_double_click();
    void on_wheel(float delta);
    void tick();
    bool cursor_hidden() const;

    void on_file_loaded();
    void on_changed();
    void on_tracks_changed();
    void on_ended(bool eof, const std::string& error);

private:
    struct UpNext {
        std::optional<EpisodeTarget> target;
        std::optional<int> countdown;
        bool at_credits = false;
    };
    enum class Menu { None, Audio, Subtitles };

    void begin(double at);
    void load_neighbours();
    void report(bool force);
    void report_finished();
    void seek_to(double t);
    void seek_by(double delta);
    void toggle_pause();
    void set_volume(double v);
    void check_credits(double t);
    void show_up_next(bool at_credits);
    void play_up_next();
    void hide_up_next();
    void skip_credits();
    void play_target(const EpisodeTarget& t, std::optional<double> at);
    void close();
    bool controls_visible() const;
    void bump();
    void cycle_subtitles();
    double seek_step(bool shift, bool ctrl) const;
    void load_season(int season);
    void start_live();
    void live_dropped();
    void retry_live();
    void zap(int step);

    void paint_controls(ui::Ui& ui, const ui::Rect& b);
    void paint_live_controls(ui::Ui& ui, const ui::Rect& b);
    float paint_volume(ui::Ui& ui, float x, float y, float h);
    void paint_scrubber(ui::Ui& ui, const ui::Rect& bar_area);
    void paint_menu(ui::Ui& ui, const ui::Rect& b);
    void paint_up_next(ui::Ui& ui, const ui::Rect& b);
    void paint_resume(ui::Ui& ui, const ui::Rect& b);
    void paint_error(ui::Ui& ui, const ui::Rect& b);
    void paint_info(ui::Ui& ui, const ui::Rect& b);
    void paint_episodes(ui::Ui& ui, const ui::Rect& b);

    AppModel& model_;
    mpv::Player& player_;
    Host host_;

    bool active_ = false;
    int session_ = 0;
    PlaybackRequest request_;
    std::optional<MediaFile> file_;
    std::string url_;
    double duration_ = 0;
    double position_ = 0;
    bool paused_ = false;
    bool buffering_ = true;
    bool loaded_ = false;
    bool ended_ = false;
    bool stopped_ = true;
    double start_offset_ = 0;
    std::optional<double> resume_offer_;
    std::string error_;
    std::optional<UpNext> up_next_;
    std::optional<EpisodeTarget> previous_;
    std::optional<EpisodeTarget> next_;
    bool neighbours_loaded_ = false;
    bool pending_up_next_ = false;
    bool pending_up_next_credits_ = false;
    std::optional<TrickplayManifest> trickplay_;
    std::optional<double> available_secs_;
    std::vector<std::string> source_badges_;
    std::vector<mpv::Track> tracks_;
    std::optional<double> credits_;
    bool credits_shown_ = false;
    double last_report_ = 0;
    double last_download_poll_ = 0;
    double next_countdown_ = 0;
    double volume_ = 100;
    double volume_before_mute_ = 100;
    std::optional<LiveRequest> live_;
    std::string live_title_;
    std::string live_image_;
    int live_attempt_ = 0;
    double live_retry_at_ = 0;
    double live_stall_since_ = 0;
    double paused_at_ = 0;

    double controls_until_ = 0;
    bool show_info_ = false;
    bool show_episodes_ = false;
    Menu menu_ = Menu::None;
    ui::Rect menu_anchor_ = {};
    std::optional<double> scrubbing_;
    bool dragging_volume_ = false;
    ui::Rect bar_rect_ = {};
    ui::Rect volume_rect_ = {};
    ui::Scroll episodes_scroll_;
    ui::Scroll menu_scroll_;
    ui::Scroll season_scroll_;

    std::optional<ShowDetail> show_;
    std::map<int, std::vector<Episode>> seasons_;
    std::optional<int> panel_season_;

    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}

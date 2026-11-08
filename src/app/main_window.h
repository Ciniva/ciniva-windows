#pragma once

#include "app/app_model.h"
#include "app/overlay_window.h"
#include "app/renderer.h"
#include "app/updater.h"
#include "app/video_host.h"
#include "mpv/player.h"
#include "player/player_screen.h"
#include "screens/screen.h"
#include "ui/images.h"
#include "ui/ui.h"

#include <memory>
#include <optional>
#include <string>

namespace ciniva {

struct LaunchOptions {
    std::string url;
    double start = 0;
    bool exit_after_first_frame = false;
};

class MainWindow {
public:
    bool create(const LaunchOptions& options, int show);
    HWND hwnd() const { return hwnd_; }

private:
    static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);

    void paint();
    void layout();
    void sync_screen();
    void create_player();
    void play(const PlaybackRequest& req);
    void on_key(WPARAM key);
    void toggle_fullscreen();
    void invalidate();
    D2D1_POINT_2F to_dip(LPARAM lp) const;

    HWND hwnd_ = nullptr;
    LaunchOptions options_;
    Renderer renderer_;
    VideoHost video_;
    OverlayWindow overlay_;
    AppModel model_;
    std::unique_ptr<ui::ImageCache> images_;
    std::unique_ptr<ui::Ui> ui_;
    std::unique_ptr<Screen> login_;
    std::unique_ptr<Screen> shell_;
    Screen* current_ = nullptr;
    std::unique_ptr<mpv::Player> player_;
    std::unique_ptr<PlayerScreen> screen_;
    std::unique_ptr<Updater> updater_;
    std::optional<PlaybackRequest> pending_play_;
    bool first_paint_done_ = false;
    bool fullscreen_ = false;
    bool tracking_mouse_ = false;
    bool toast_timer_ = false;
    WINDOWPLACEMENT saved_placement_ = {sizeof(WINDOWPLACEMENT)};
};

}

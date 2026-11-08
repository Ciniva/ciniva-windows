#pragma once

#include "app/renderer.h"
#include "app/video_host.h"
#include "mpv/player.h"

#include <memory>
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
    void start_player();
    void on_key(WPARAM key);
    void toggle_fullscreen();
    void set_status(std::wstring text);

    HWND hwnd_ = nullptr;
    LaunchOptions options_;
    Renderer renderer_;
    VideoHost video_;
    std::unique_ptr<mpv::Player> player_;
    ComPtr<IDWriteTextFormat> title_format_;
    ComPtr<IDWriteTextFormat> body_format_;
    std::wstring status_;
    bool first_paint_done_ = false;
    bool fullscreen_ = false;
    WINDOWPLACEMENT saved_placement_ = {sizeof(WINDOWPLACEMENT)};
};

}

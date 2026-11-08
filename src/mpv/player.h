#pragma once

#include <windows.h>

#include <atomic>
#include <functional>
#include <string>
#include <thread>

struct mpv_handle;
struct mpv_event;

namespace ciniva::mpv {

class Player {
public:
    struct Callbacks {
        std::function<void(bool ok, const std::string& error)> ready;
        std::function<void()> first_frame;
        std::function<void(const std::string& reason)> ended;
        std::function<void()> state_changed;
    };

    struct State {
        double position = 0;
        double duration = 0;
        bool paused = false;
        bool buffering = false;
        std::string gamma;
        std::string primaries;
        std::string hwdec;
        std::string video_format;
    };

    Player(HWND notify, UINT wakeup_message, UINT ready_message, Callbacks callbacks);
    ~Player();

    void start(HWND wid);
    void on_ready_message(WPARAM ok);
    void drain_events();

    bool ready() const { return handle_ != nullptr; }
    const State& state() const { return state_; }

    void load(const std::string& url, double start_secs);
    void toggle_pause();
    void seek_relative(double secs);
    void toggle_stats();
    void command(std::initializer_list<const char*> args);

private:
    void initialize(HWND wid);
    void handle_event(const mpv_event& event);

    HWND notify_;
    UINT wakeup_message_;
    UINT ready_message_;
    Callbacks callbacks_;
    std::thread init_thread_;
    mpv_handle* pending_ = nullptr;
    mpv_handle* handle_ = nullptr;
    std::string init_error_;
    State state_;
    bool first_frame_seen_ = false;
};

}

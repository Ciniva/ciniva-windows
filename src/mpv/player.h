#pragma once

#include <windows.h>

#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <vector>

struct mpv_handle;
struct mpv_event;

namespace ciniva::mpv {

struct Track {
    std::string id;
    std::string type;
    std::string title;
    std::string detail;
    bool selected = false;
};

class Player {
public:
    struct Callbacks {
        std::function<void(bool ok, const std::string& error)> ready;
        std::function<void()> file_loaded;
        std::function<void()> first_frame;
        std::function<void(bool eof, const std::string& error)> ended;
        std::function<void()> changed;
        std::function<void()> tracks_changed;
    };

    struct State {
        double position = 0;
        double duration = 0;
        bool paused = false;
        bool buffering = false;
        bool seeking = false;
        double volume = 100;
        std::string gamma;
        std::string primaries;
        std::string target_gamma;
        std::string target_primaries;
        std::string hwdec;
        std::string video_format;
        long long width = 0;
        long long height = 0;
        std::optional<long long> dolby_vision_profile;
    };

    Player(HWND notify, UINT wakeup_message, UINT ready_message);
    ~Player();

    void set_callbacks(Callbacks callbacks) { callbacks_ = std::move(callbacks); }
    void start(HWND wid, double volume);
    void on_ready_message(WPARAM ok);
    void drain_events();

    bool ready() const { return handle_ != nullptr; }
    const State& state() const { return state_; }

    void load(const std::string& url, double start_secs);
    void stop();
    void set_paused(bool paused);
    void seek_absolute(double secs);
    void set_volume(double volume);
    void select_audio(const std::string& id);
    void select_subtitle(const std::string& id);
    void toggle_stats();
    void apply_settings();
    void command(std::initializer_list<const char*> args);
    void set(const char* name, const std::string& value);

    std::vector<Track> tracks() const;
    std::vector<std::string> video_info() const;

private:
    void initialize(HWND wid, double volume);
    void handle_event(const mpv_event& event);
    std::optional<std::string> get_string(const std::string& name) const;
    std::optional<long long> get_int(const std::string& name) const;
    bool get_flag(const std::string& name) const;

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
    bool loading_ = false;
};

}

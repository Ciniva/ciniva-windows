#include "mpv/player.h"

#include "app/timing.h"
#include "mpv/mpv_api.h"

#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace ciniva::mpv {

static std::wstring exe_dir() {
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring s(path, n);
    return s.substr(0, s.find_last_of(L"\\/"));
}

static std::string local_appdata_utf8(const char* file) {
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    if (!n || n >= MAX_PATH) return {};
    std::wstring w = std::wstring(buf, n) + L"\\Ciniva\\";
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), len, nullptr, nullptr);
    return s + file;
}

Player::Player(HWND notify, UINT wakeup_message, UINT ready_message, Callbacks callbacks)
    : notify_(notify), wakeup_message_(wakeup_message), ready_message_(ready_message), callbacks_(std::move(callbacks)) {}

Player::~Player() {
    if (init_thread_.joinable()) init_thread_.join();
    mpv_handle* h = handle_ ? handle_ : pending_;
    if (h) {
        api().mpv_set_wakeup_callback(h, nullptr, nullptr);
        api().mpv_terminate_destroy(h);
    }
}

void Player::start(HWND wid) {
    init_thread_ = std::thread([this, wid] { initialize(wid); });
}

void Player::initialize(HWND wid) {
    timing::mark("mpv: loading libmpv-2.dll");
    const Api* a = mpv::load((exe_dir() + L"\\libmpv-2.dll").c_str());
    if (!a) {
        init_error_ = "libmpv-2.dll is missing or incompatible";
        PostMessageW(notify_, ready_message_, 0, 0);
        return;
    }
    timing::mark("mpv: library loaded");

    mpv_handle* h = a->mpv_create();
    if (!h) {
        init_error_ = "mpv_create failed";
        PostMessageW(notify_, ready_message_, 0, 0);
        return;
    }

    std::vector<std::pair<std::string, std::string>> opts = {
        {"wid", std::to_string(reinterpret_cast<intptr_t>(wid))},
        {"vo", "gpu-next"},
        {"gpu-api", "d3d11"},
        {"target-colorspace-hint", "yes"},
        {"hwdec", "auto-safe"},
        {"osc", "no"},
        {"osd-level", "1"},
        {"input-default-bindings", "no"},
        {"input-vo-keyboard", "no"},
        {"input-cursor", "no"},
        {"cursor-autohide", "no"},
        {"idle", "yes"},
        {"keep-open", "no"},
        {"terminal", "no"},
        {"load-scripts", "no"},
        {"load-auto-profiles", "no"},
        {"ytdl", "no"},
        {"user-agent", "Ciniva-Windows/1.0"},
        {"cache", "yes"},
        {"demuxer-max-bytes", "1024MiB"},
        {"demuxer-max-back-bytes", "256MiB"},
        {"demuxer-readahead-secs", "60"},
        {"network-timeout", "30"},
        {"audio-channels", "auto"},
        {"tone-mapping", "auto"},
        {"hdr-compute-peak", "auto"},
        {"sub-font-size", "44"},
        {"sub-border-size", "2.5"},
        {"sub-ass-override", "scale"},
        {"volume-max", "150"},
    };
    if (GetEnvironmentVariableW(L"CINIVA_DEBUG", nullptr, 0) > 0) {
        opts.push_back({"log-file", local_appdata_utf8("mpv.log")});
        opts.push_back({"msg-level", "all=v"});
    }
    for (auto& [k, v] : opts) {
        if (int err = a->mpv_set_option_string(h, k.c_str(), v.c_str()); err < 0)
            timing::log("mpv: option %s=%s failed: %s", k.c_str(), v.c_str(), a->mpv_error_string(err));
    }

    if (int err = a->mpv_initialize(h); err < 0) {
        init_error_ = std::string("mpv_initialize failed: ") + a->mpv_error_string(err);
        a->mpv_terminate_destroy(h);
        PostMessageW(notify_, ready_message_, 0, 0);
        return;
    }

    a->mpv_observe_property(h, 0, "time-pos", MPV_FORMAT_DOUBLE);
    a->mpv_observe_property(h, 0, "duration", MPV_FORMAT_DOUBLE);
    a->mpv_observe_property(h, 0, "pause", MPV_FORMAT_FLAG);
    a->mpv_observe_property(h, 0, "paused-for-cache", MPV_FORMAT_FLAG);
    a->mpv_observe_property(h, 0, "video-params/gamma", MPV_FORMAT_STRING);
    a->mpv_observe_property(h, 0, "video-params/primaries", MPV_FORMAT_STRING);
    a->mpv_observe_property(h, 0, "video-target-params/primaries", MPV_FORMAT_STRING);
    a->mpv_observe_property(h, 0, "video-target-params/gamma", MPV_FORMAT_STRING);
    a->mpv_observe_property(h, 0, "hwdec-current", MPV_FORMAT_STRING);
    a->mpv_observe_property(h, 0, "video-format", MPV_FORMAT_STRING);

    pending_ = h;
    a->mpv_set_wakeup_callback(h, [](void* ctx) {
        auto* self = static_cast<Player*>(ctx);
        PostMessageW(self->notify_, self->wakeup_message_, 0, 0);
    }, this);
    timing::mark("mpv: initialized");
    PostMessageW(notify_, ready_message_, 1, 0);
}

void Player::on_ready_message(WPARAM ok) {
    if (init_thread_.joinable()) init_thread_.join();
    if (ok) {
        handle_ = pending_;
        pending_ = nullptr;
    }
    if (callbacks_.ready) callbacks_.ready(ok != 0, init_error_);
    if (ok) drain_events();
}

void Player::drain_events() {
    if (!handle_) return;
    for (;;) {
        mpv_event* e = api().mpv_wait_event(handle_, 0);
        if (e->event_id == MPV_EVENT_NONE) break;
        handle_event(*e);
    }
}

void Player::handle_event(const mpv_event& event) {
    switch (event.event_id) {
    case MPV_EVENT_PROPERTY_CHANGE: {
        auto* p = static_cast<mpv_event_property*>(event.data);
        std::string name = p->name;
        if (p->format == MPV_FORMAT_DOUBLE) {
            double v = *static_cast<double*>(p->data);
            if (name == "time-pos") state_.position = v;
            else if (name == "duration") state_.duration = v;
        } else if (p->format == MPV_FORMAT_FLAG) {
            bool v = *static_cast<int*>(p->data) != 0;
            if (name == "pause") state_.paused = v;
            else if (name == "paused-for-cache") state_.buffering = v;
        } else if (p->format == MPV_FORMAT_STRING) {
            std::string v = *static_cast<char**>(p->data);
            if (name == "video-params/gamma") state_.gamma = v;
            else if (name == "video-params/primaries") state_.primaries = v;
            else if (name == "hwdec-current") state_.hwdec = v;
            else if (name == "video-format") state_.video_format = v;
            timing::log("mpv: %s = %s", name.c_str(), v.c_str());
        } else if (p->format == MPV_FORMAT_NONE) {
            if (name == "time-pos") state_.position = 0;
            else if (name == "duration") state_.duration = 0;
        }
        if (callbacks_.state_changed) callbacks_.state_changed();
        break;
    }
    case MPV_EVENT_FILE_LOADED:
        timing::mark("mpv: file loaded");
        break;
    case MPV_EVENT_PLAYBACK_RESTART:
        if (!first_frame_seen_) {
            first_frame_seen_ = true;
            timing::mark("mpv: first video frame");
            if (callbacks_.first_frame) callbacks_.first_frame();
        }
        break;
    case MPV_EVENT_END_FILE: {
        auto* end = static_cast<mpv_event_end_file*>(event.data);
        std::string reason;
        switch (end->reason) {
        case MPV_END_FILE_REASON_EOF: reason = "eof"; break;
        case MPV_END_FILE_REASON_STOP: reason = "stop"; break;
        case MPV_END_FILE_REASON_QUIT: reason = "quit"; break;
        case MPV_END_FILE_REASON_ERROR: reason = std::string("error: ") + api().mpv_error_string(end->error); break;
        default: reason = "other"; break;
        }
        timing::log("mpv: end-file %s", reason.c_str());
        if (end->reason != MPV_END_FILE_REASON_REDIRECT && callbacks_.ended) callbacks_.ended(reason);
        break;
    }
    default:
        break;
    }
}

void Player::command(std::initializer_list<const char*> args) {
    if (!handle_) return;
    std::vector<const char*> argv(args);
    argv.push_back(nullptr);
    api().mpv_command_async(handle_, 0, argv.data());
}

void Player::load(const std::string& url, double start_secs) {
    if (!handle_) return;
    first_frame_seen_ = false;
    api().mpv_set_property_string(handle_, "start", start_secs > 1 ? std::to_string(start_secs).c_str() : "none");
    command({"loadfile", url.c_str()});
}

void Player::toggle_pause() { command({"cycle", "pause"}); }

void Player::seek_relative(double secs) {
    std::string s = std::to_string(secs);
    command({"seek", s.c_str(), "relative"});
}

void Player::toggle_stats() { command({"script-binding", "stats/display-stats-toggle"}); }

}

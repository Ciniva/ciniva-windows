#include "mpv/player.h"

#include "app/timing.h"
#include "core/models.h"
#include "core/settings.h"
#include "core/text.h"
#include "mpv/mpv_api.h"

#include <cstdio>
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

Player::Player(HWND notify, UINT wakeup_message, UINT ready_message)
    : notify_(notify), wakeup_message_(wakeup_message), ready_message_(ready_message) {}

Player::~Player() {
    if (init_thread_.joinable()) init_thread_.join();
    mpv_handle* h = handle_ ? handle_ : pending_;
    if (h) {
        api().mpv_set_wakeup_callback(h, nullptr, nullptr);
        api().mpv_terminate_destroy(h);
    }
}

void Player::start(HWND wid, double volume) {
    init_thread_ = std::thread([this, wid, volume] { initialize(wid, volume); });
}

void Player::initialize(HWND wid, double volume) {
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

    const auto& s = Settings::get();
    std::vector<std::pair<std::string, std::string>> opts = {
        {"wid", std::to_string(reinterpret_cast<intptr_t>(wid))},
        {"vo", "gpu-next"},
        {"gpu-api", "d3d11"},
        {"target-colorspace-hint", s.hdr ? "yes" : "no"},
        {"hwdec", "auto-safe"},
        {"osc", "no"},
        {"osd-level", "1"},
        {"input-default-bindings", "no"},
        {"input-vo-keyboard", "no"},
        {"input-cursor", "no"},
        {"cursor-autohide", "no"},
        {"idle", "yes"},
        {"keep-open", "no"},
        {"force-window", "no"},
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
        {"volume", std::to_string(static_cast<int>(volume))},
        {"volume-max", "150"},
    };
    if (s.peak_nits > 0) opts.push_back({"target-peak", std::to_string(s.peak_nits)});
    if (!s.audio_lang.empty()) opts.push_back({"alang", s.audio_lang});
    if (!s.sub_lang.empty()) opts.push_back({"slang", s.sub_lang});
    if (s.passthrough) opts.push_back({"audio-spdif", "ac3,eac3,dts,dts-hd,truehd"});
    if (GetEnvironmentVariableW(L"CINIVA_DEBUG", nullptr, 0) > 0) {
        opts.push_back({"log-file", narrow(Settings::data_dir()) + "\\mpv.log"});
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

    const std::pair<const char*, mpv_format> observed[] = {
        {"time-pos", MPV_FORMAT_DOUBLE},
        {"duration", MPV_FORMAT_DOUBLE},
        {"pause", MPV_FORMAT_FLAG},
        {"paused-for-cache", MPV_FORMAT_FLAG},
        {"seeking", MPV_FORMAT_FLAG},
        {"volume", MPV_FORMAT_DOUBLE},
        {"track-list/count", MPV_FORMAT_INT64},
        {"aid", MPV_FORMAT_STRING},
        {"sid", MPV_FORMAT_STRING},
        {"video-params/gamma", MPV_FORMAT_STRING},
        {"video-params/primaries", MPV_FORMAT_STRING},
        {"video-params/w", MPV_FORMAT_INT64},
        {"video-params/h", MPV_FORMAT_INT64},
        {"video-target-params/primaries", MPV_FORMAT_STRING},
        {"video-target-params/gamma", MPV_FORMAT_STRING},
        {"current-tracks/video/dolby-vision-profile", MPV_FORMAT_INT64},
        {"hwdec-current", MPV_FORMAT_STRING},
        {"video-format", MPV_FORMAT_STRING},
    };
    for (auto& [name, format] : observed) a->mpv_observe_property(h, 0, name, format);

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
    bool changed = false;
    for (;;) {
        mpv_event* e = api().mpv_wait_event(handle_, 0);
        if (e->event_id == MPV_EVENT_NONE) break;
        if (e->event_id == MPV_EVENT_PROPERTY_CHANGE) changed = true;
        handle_event(*e);
    }
    if (changed && callbacks_.changed) callbacks_.changed();
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
            else if (name == "volume") state_.volume = v;
        } else if (p->format == MPV_FORMAT_FLAG) {
            bool v = *static_cast<int*>(p->data) != 0;
            if (name == "pause") state_.paused = v;
            else if (name == "paused-for-cache") state_.buffering = v;
            else if (name == "seeking") state_.seeking = v;
        } else if (p->format == MPV_FORMAT_INT64) {
            long long v = *static_cast<int64_t*>(p->data);
            if (name == "video-params/w") state_.width = v;
            else if (name == "video-params/h") state_.height = v;
            else if (name == "current-tracks/video/dolby-vision-profile") state_.dolby_vision_profile = v;
            else if (name == "track-list/count" && callbacks_.tracks_changed) callbacks_.tracks_changed();
        } else if (p->format == MPV_FORMAT_STRING) {
            std::string v = *static_cast<char**>(p->data);
            if (name == "video-params/gamma") state_.gamma = v;
            else if (name == "video-params/primaries") state_.primaries = v;
            else if (name == "video-target-params/gamma") state_.target_gamma = v;
            else if (name == "video-target-params/primaries") state_.target_primaries = v;
            else if (name == "hwdec-current") state_.hwdec = v;
            else if (name == "video-format") state_.video_format = v;
            else if ((name == "aid" || name == "sid") && callbacks_.tracks_changed) callbacks_.tracks_changed();
            if (name != "aid" && name != "sid") timing::log("mpv: %s = %s", name.c_str(), v.c_str());
        } else if (p->format == MPV_FORMAT_NONE) {
            if (name == "time-pos") state_.position = 0;
            else if (name == "duration") state_.duration = 0;
            else if (name == "current-tracks/video/dolby-vision-profile") state_.dolby_vision_profile.reset();
        }
        break;
    }
    case MPV_EVENT_FILE_LOADED:
        loading_ = false;
        timing::mark("mpv: file loaded");
        if (callbacks_.file_loaded) callbacks_.file_loaded();
        if (callbacks_.tracks_changed) callbacks_.tracks_changed();
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
        if (end->reason == MPV_END_FILE_REASON_EOF) {
            timing::log("mpv: end-file eof");
            if (callbacks_.ended) callbacks_.ended(true, "");
        } else if (end->reason == MPV_END_FILE_REASON_ERROR) {
            std::string msg = api().mpv_error_string(end->error);
            timing::log("mpv: end-file error %s", msg.c_str());
            if (callbacks_.ended) callbacks_.ended(false, msg);
        }
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

void Player::set(const char* name, const std::string& value) { command({"set", name, value.c_str()}); }

void Player::load(const std::string& url, double start_secs) {
    if (!handle_) return;
    first_frame_seen_ = false;
    loading_ = true;
    state_.position = 0;
    state_.duration = 0;
    state_.dolby_vision_profile.reset();
    char start[32];
    std::snprintf(start, sizeof start, "%.3f", start_secs);
    std::string opts = start_secs > 1 ? std::string("start=") + start : "start=none";
    command({"loadfile", url.c_str(), "replace", "0", opts.c_str()});
    set("pause", "no");
}

void Player::stop() { command({"stop"}); }

void Player::set_paused(bool paused) {
    state_.paused = paused;
    set("pause", paused ? "yes" : "no");
}

void Player::seek_absolute(double secs) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.3f", secs < 0 ? 0 : secs);
    command({"seek", buf, "absolute+keyframes"});
}

void Player::set_volume(double volume) {
    state_.volume = volume;
    set("volume", std::to_string(static_cast<int>(volume)));
}

void Player::select_audio(const std::string& id) { set("aid", id); }
void Player::select_subtitle(const std::string& id) { set("sid", id.empty() ? "no" : id); }

void Player::apply_settings() {
    const auto& s = Settings::get();
    set("target-colorspace-hint", s.hdr ? "yes" : "no");
    set("target-peak", s.peak_nits > 0 ? std::to_string(s.peak_nits) : "auto");
    set("alang", s.audio_lang);
    set("slang", s.sub_lang);
    set("audio-spdif", s.passthrough ? "ac3,eac3,dts,dts-hd,truehd" : "");
}

void Player::toggle_stats() { command({"script-binding", "stats/display-stats-toggle"}); }

std::optional<std::string> Player::get_string(const std::string& name) const {
    if (!handle_) return std::nullopt;
    char* v = api().mpv_get_property_string(handle_, name.c_str());
    if (!v) return std::nullopt;
    std::string s = v;
    api().mpv_free(v);
    return s;
}

std::optional<long long> Player::get_int(const std::string& name) const {
    if (!handle_) return std::nullopt;
    int64_t v = 0;
    if (api().mpv_get_property(handle_, name.c_str(), MPV_FORMAT_INT64, &v) < 0) return std::nullopt;
    return v;
}

bool Player::get_flag(const std::string& name) const {
    if (!handle_) return false;
    int v = 0;
    return api().mpv_get_property(handle_, name.c_str(), MPV_FORMAT_FLAG, &v) >= 0 && v != 0;
}

static std::string language_name(const std::string& code) {
    if (code.empty() || code == "und") return {};
    wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
    std::wstring w = widen(code);
    wchar_t locale[LOCALE_NAME_MAX_LENGTH] = {};
    // ISO 639-2 codes (eng, ger) need mapping to a locale name before Windows can name the language.
    if (ResolveLocaleName(w.c_str(), locale, LOCALE_NAME_MAX_LENGTH) > 0 &&
        GetLocaleInfoEx(locale, LOCALE_SENGLISHLANGUAGENAME, name, LOCALE_NAME_MAX_LENGTH) > 0)
        return narrow(name);
    static const std::pair<const char*, const char*> iso3[] = {
        {"eng", "English"}, {"ger", "German"}, {"deu", "German"}, {"fre", "French"}, {"fra", "French"},
        {"spa", "Spanish"}, {"ita", "Italian"}, {"jpn", "Japanese"}, {"kor", "Korean"}, {"chi", "Chinese"},
        {"zho", "Chinese"}, {"rus", "Russian"}, {"por", "Portuguese"}, {"dut", "Dutch"}, {"nld", "Dutch"},
        {"swe", "Swedish"}, {"nor", "Norwegian"}, {"dan", "Danish"}, {"fin", "Finnish"}, {"pol", "Polish"},
        {"geo", "Georgian"}, {"kat", "Georgian"}, {"ukr", "Ukrainian"}, {"tur", "Turkish"}, {"ara", "Arabic"},
        {"heb", "Hebrew"}, {"hin", "Hindi"}, {"tha", "Thai"}, {"cze", "Czech"}, {"ces", "Czech"},
        {"hun", "Hungarian"}, {"gre", "Greek"}, {"ell", "Greek"}, {"rum", "Romanian"}, {"ron", "Romanian"}};
    for (auto& [k, v] : iso3)
        if (code == k) return v;
    return code;
}

static std::string subtitle_codec_name(const std::string& codec) {
    if (codec == "hdmv_pgs_subtitle") return "PGS";
    if (codec == "dvd_subtitle") return "VobSub";
    if (codec == "subrip") return "SRT";
    if (codec == "ass" || codec == "ssa") return "ASS";
    if (codec == "mov_text") return "TX3G";
    if (codec == "webvtt") return "WebVTT";
    std::string s = codec;
    for (auto& c : s) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    return s;
}

std::vector<Track> Player::tracks() const {
    std::vector<Track> out;
    long long count = get_int("track-list/count").value_or(0);
    for (long long i = 0; i < count; ++i) {
        std::string p = "track-list/" + std::to_string(i) + "/";
        auto type = get_string(p + "type");
        auto id = get_int(p + "id");
        if (!type || !id || (*type != "audio" && *type != "sub")) continue;
        Track t;
        t.id = std::to_string(*id);
        t.type = *type;
        t.selected = get_flag(p + "selected");
        t.detail = get_string(p + "title").value_or("");
        std::string lang = language_name(get_string(p + "lang").value_or(""));
        std::string codec = get_string(p + "codec").value_or("");
        if (*type == "audio") {
            AudioInfo a;
            a.codec = codec;
            a.profile = get_string(p + "codec-profile");
            if (auto ch = get_int(p + "demux-channel-count")) a.channels = static_cast<int>(*ch);
            std::string tech = codec.empty() ? "" : a.label();
            t.title = lang.empty() ? tech : tech.empty() ? lang : lang + " \xC2\xB7 " + tech;
            if (t.title.empty()) t.title = "Track " + t.id;
        } else {
            std::string s = lang.empty() ? "Unknown" : lang;
            if (get_flag(p + "forced")) s += " \xC2\xB7 Forced";
            if (!codec.empty()) s += " \xC2\xB7 " + subtitle_codec_name(codec);
            if (get_flag(p + "external")) s += " \xC2\xB7 External";
            t.title = s;
        }
        out.push_back(std::move(t));
    }
    return out;
}

std::vector<std::string> Player::video_info() const {
    std::vector<std::string> bits;
    if (!state_.video_format.empty()) {
        std::string f = state_.video_format;
        for (auto& c : f) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
        bits.push_back(f);
    }
    if (state_.width > 0 && state_.height > 0)
        bits.push_back(std::to_string(state_.width) + "\xC3\x97" + std::to_string(state_.height));
    if (state_.dolby_vision_profile) bits.push_back("Dolby Vision P" + std::to_string(*state_.dolby_vision_profile));
    if (state_.gamma == "pq" && !state_.dolby_vision_profile) bits.push_back("HDR10 (PQ)");
    if (state_.gamma == "hlg") bits.push_back("HLG");
    if (!state_.hwdec.empty() && state_.hwdec != "no") bits.push_back("HW decode (" + state_.hwdec + ")");
    else if (!state_.video_format.empty()) bits.push_back("SW decode");
    return bits;
}

}

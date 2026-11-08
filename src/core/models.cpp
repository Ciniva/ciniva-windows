#include "core/models.h"

#include "core/text.h"

#include <algorithm>
#include <cctype>
#include <map>

namespace ciniva {

namespace {

template <class T>
void get(const json& j, const char* key, T& out) {
    auto it = j.find(key);
    if (it != j.end() && !it->is_null()) it->get_to(out);
}

template <class T>
void get(const json& j, const char* key, std::optional<T>& out) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return;
    try {
        out = it->get<T>();
    } catch (const json::exception&) {
        out.reset();
    }
}

template <class T>
void get_lenient(const json& j, const char* key, std::vector<T>& out) {
    try {
        get(j, key, out);
    } catch (const json::exception&) {
        out.clear();
    }
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}

std::optional<double> MovieCard::progress() const {
    if (watched.value_or(false) || !position_ms || !duration_ms || *duration_ms <= 0 || *position_ms <= 0)
        return std::nullopt;
    return static_cast<double>(*position_ms) / static_cast<double>(*duration_ms);
}

std::string AudioInfo::label() const {
    std::string c = lower(codec), name;
    std::string p = profile ? lower(*profile) : "";
    if (c == "truehd") name = "TrueHD";
    else if (c == "eac3") name = "Dolby Digital+";
    else if (c == "ac3") name = "Dolby Digital";
    else if (c == "dca" || c == "dts") name = p.find("ma") != std::string::npos ? "DTS-HD MA" : p.find('x') != std::string::npos ? "DTS:X" : "DTS";
    else if (c == "aac") name = "AAC";
    else if (c == "flac") name = "FLAC";
    else if (c == "opus") name = "Opus";
    else if (c == "mp3") name = "MP3";
    else if (c.rfind("pcm", 0) == 0) name = "PCM";
    else {
        name = codec;
        for (auto& ch : name) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    if (!channels || *channels <= 0) return name;
    switch (*channels) {
    case 1: return name + " 1.0";
    case 2: return name + " 2.0";
    case 6: return name + " 5.1";
    case 8: return name + " 7.1";
    default: return name + " " + std::to_string(*channels) + "ch";
    }
}

const AudioInfo* MediaInfo::default_audio() const {
    for (auto& a : audio)
        if (a.is_default) return &a;
    return audio.empty() ? nullptr : &audio.front();
}

std::vector<std::string> MediaInfo::tech_badges() const {
    std::vector<std::string> out;
    if (video) {
        const auto& v = *video;
        if (v.height >= 1600 || v.width >= 3200) out.push_back("4K");
        else if (v.height >= 1000 || v.width >= 1800) out.push_back("1080p");
        else if (v.height >= 700) out.push_back("720p");
        else out.push_back(std::to_string(v.height) + "p");
        if (v.dolby_vision_profile) out.push_back("Dolby Vision P" + std::to_string(*v.dolby_vision_profile));
        if (v.hdr && *v.hdr != "dolby_vision") {
            std::string h = *v.hdr;
            if (h == "hdr10") out.push_back("HDR10");
            else if (h == "hdr10+") out.push_back("HDR10+");
            else {
                for (auto& ch : h) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                out.push_back(h);
            }
        }
        static const std::map<std::string, std::string> names = {
            {"hevc", "HEVC"}, {"h264", "H.264"}, {"av1", "AV1"}, {"vp9", "VP9"}, {"mpeg2video", "MPEG-2"}, {"mpeg4", "MPEG-4"}};
        auto it = names.find(v.codec);
        if (it != names.end()) out.push_back(it->second);
        else {
            std::string c = v.codec;
            for (auto& ch : c) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            out.push_back(c);
        }
    }
    if (auto* a = default_audio()) {
        if (a->atmos) out.push_back("Atmos");
        out.push_back(a->label());
    }
    return out;
}

std::string MediaFile::file_name() const {
    auto pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

double MediaFile::total_seconds() const {
    if (duration) return *duration;
    if (probe && probe->duration) return *probe->duration;
    return static_cast<double>(duration_ms.value_or(0)) / 1000.0;
}

std::optional<double> MediaFile::progress() const {
    if (is_watched() || !position_ms || !duration_ms || *duration_ms <= 0 || *position_ms <= 0) return std::nullopt;
    return static_cast<double>(*position_ms) / static_cast<double>(*duration_ms);
}

double MediaFile::resume_seconds() const {
    if (is_watched() || !position_ms) return 0;
    return static_cast<double>(*position_ms) / 1000.0;
}

std::optional<std::string> MediaFile::time_left_label() const {
    if (!progress()) return std::nullopt;
    return format_clock(static_cast<double>(duration_ms.value_or(0) - position_ms.value_or(0)) / 1000.0) + " left";
}

std::string DownloadItem::label() const { return subtitle ? title + " \xC2\xB7 " + *subtitle : title; }

std::string SeasonCard::label() const {
    if (name) return *name;
    return season_number == 0 ? "Specials" : "Season " + std::to_string(season_number);
}

std::string Episode::display_title() const {
    return std::to_string(episode_number) + ". " + (title ? *title : "Episode " + std::to_string(episode_number));
}

std::string Episode::label() const {
    std::string s = "S" + std::to_string(season_number) + " \xC2\xB7 E" + std::to_string(episode_number);
    if (title) s += " \xE2\x80\x94 " + *title;
    return s;
}

TrickplayManifest::Tile TrickplayManifest::tile_at(double t) const {
    int per_sheet = std::max(1, cols * rows);
    int i = static_cast<int>(t / std::max(interval, 0.001));
    i = std::max(0, std::min(i, count - 1));
    int within = i % per_sheet;
    return {i / per_sheet + 1, (within % std::max(cols, 1)) * width, (within / std::max(cols, 1)) * height};
}

void from_json(const json& j, User& v) {
    get(j, "id", v.id);
    get(j, "username", v.username);
    get(j, "admin", v.admin);
}

void from_json(const json& j, SetupState& v) {
    get(j, "needs_admin", v.needs_admin);
    get(j, "tmdb_configured", v.tmdb_configured);
    get(j, "version", v.version);
}

void from_json(const json& j, MovieCard& v) {
    get(j, "id", v.id);
    get(j, "title", v.title);
    get(j, "year", v.year);
    get(j, "poster_path", v.poster_path);
    get(j, "backdrop_path", v.backdrop_path);
    get(j, "rating", v.rating);
    get(j, "added_at", v.added_at);
    get(j, "position_ms", v.position_ms);
    get(j, "duration_ms", v.duration_ms);
    get(j, "watched", v.watched);
}

void from_json(const json& j, ShowCard& v) {
    get(j, "id", v.id);
    get(j, "title", v.title);
    get(j, "year", v.year);
    get(j, "poster_path", v.poster_path);
    get(j, "backdrop_path", v.backdrop_path);
    get(j, "rating", v.rating);
    get(j, "episodes", v.episodes);
    get(j, "watched_episodes", v.watched_episodes);
    get(j, "last_added", v.last_added);
}

void from_json(const json& j, ContinueItem& v) {
    get(j, "file_id", v.file_id);
    get(j, "position_ms", v.position_ms);
    get(j, "duration_ms", v.duration_ms);
    get(j, "updated_at", v.updated_at);
    get(j, "movie_id", v.movie_id);
    get(j, "show_id", v.show_id);
    get(j, "title", v.title);
    get(j, "subtitle", v.subtitle);
    get(j, "image_path", v.image_path);
    get(j, "poster_path", v.poster_path);
    get(j, "next_up", v.next_up);
}

void from_json(const json& j, VideoInfo& v) {
    get(j, "codec", v.codec);
    get(j, "profile", v.profile);
    get(j, "width", v.width);
    get(j, "height", v.height);
    get(j, "bit_depth", v.bit_depth);
    get(j, "hdr", v.hdr);
    get(j, "dolby_vision_profile", v.dolby_vision_profile);
    get(j, "fps", v.fps);
}

void from_json(const json& j, AudioInfo& v) {
    get(j, "index", v.index);
    get(j, "codec", v.codec);
    get(j, "profile", v.profile);
    get(j, "channels", v.channels);
    get(j, "language", v.language);
    get(j, "title", v.title);
    get(j, "atmos", v.atmos);
    get(j, "default", v.is_default);
}

void from_json(const json& j, SubtitleInfo& v) {
    get(j, "index", v.index);
    get(j, "codec", v.codec);
    get(j, "language", v.language);
    get(j, "title", v.title);
    get(j, "forced", v.forced);
    get(j, "default", v.is_default);
}

void from_json(const json& j, MediaInfo& v) {
    get(j, "container", v.container);
    get(j, "duration", v.duration);
    get(j, "bitrate", v.bitrate);
    get(j, "video", v.video);
    get(j, "audio", v.audio);
    get(j, "subtitles", v.subtitles);
}

void from_json(const json& j, MediaFile& v) {
    get(j, "id", v.id);
    get(j, "path", v.path);
    get(j, "size", v.size);
    get(j, "duration", v.duration);
    get(j, "probe", v.probe);
    get(j, "added_at", v.added_at);
    get(j, "credits_start", v.credits_start);
    get(j, "credits_source", v.credits_source);
    get(j, "position_ms", v.position_ms);
    get(j, "duration_ms", v.duration_ms);
    get(j, "watched", v.watched);
}

void from_json(const json& j, DownloadItem& v) {
    get(j, "id", v.id);
    get(j, "title", v.title);
    get(j, "subtitle", v.subtitle);
    get(j, "name", v.name);
    get(j, "size", v.size);
    get(j, "progress", v.progress);
    get(j, "available_fraction", v.available_fraction);
    get(j, "available_secs", v.available_secs);
    get(j, "duration", v.duration);
    get(j, "playable", v.playable);
    get(j, "probe", v.probe);
}

void from_json(const json& j, HomeData& v) {
    get(j, "continue_watching", v.continue_watching);
    get(j, "recent_movies", v.recent_movies);
    get(j, "recent_shows", v.recent_shows);
}

void from_json(const json& j, SearchResults& v) {
    get(j, "movies", v.movies);
    get(j, "shows", v.shows);
}

void from_json(const json& j, CastMember& v) {
    get(j, "name", v.name);
    get(j, "character", v.character);
    get(j, "profile_path", v.profile_path);
}

void from_json(const json& j, MovieDetail& v) {
    get(j, "id", v.id);
    get(j, "tmdb_id", v.tmdb_id);
    get(j, "title", v.title);
    get(j, "original_title", v.original_title);
    get(j, "year", v.year);
    get(j, "overview", v.overview);
    get(j, "tagline", v.tagline);
    get(j, "poster_path", v.poster_path);
    get(j, "backdrop_path", v.backdrop_path);
    get(j, "runtime", v.runtime);
    get(j, "rating", v.rating);
    get_lenient(j, "genres", v.genres);
    get(j, "release_date", v.release_date);
    get_lenient(j, "cast", v.cast);
    get(j, "files", v.files);
}

void from_json(const json& j, SeasonCard& v) {
    get(j, "season_number", v.season_number);
    get(j, "name", v.name);
    get(j, "poster_path", v.poster_path);
    get(j, "air_date", v.air_date);
    get(j, "episode_count", v.episode_count);
    get(j, "available", v.available);
    get(j, "watched", v.watched);
}

void from_json(const json& j, ShowDetail& v) {
    get(j, "id", v.id);
    get(j, "tmdb_id", v.tmdb_id);
    get(j, "title", v.title);
    get(j, "original_title", v.original_title);
    get(j, "year", v.year);
    get(j, "overview", v.overview);
    get(j, "poster_path", v.poster_path);
    get(j, "backdrop_path", v.backdrop_path);
    get(j, "rating", v.rating);
    get_lenient(j, "genres", v.genres);
    get(j, "first_air_date", v.first_air_date);
    get(j, "status", v.status);
    get_lenient(j, "cast", v.cast);
    get(j, "seasons", v.seasons);
}

void from_json(const json& j, Episode& v) {
    get(j, "id", v.id);
    get(j, "season_number", v.season_number);
    get(j, "episode_number", v.episode_number);
    get(j, "title", v.title);
    get(j, "overview", v.overview);
    get(j, "still_path", v.still_path);
    get(j, "air_date", v.air_date);
    get(j, "runtime", v.runtime);
    get(j, "rating", v.rating);
    get(j, "files", v.files);
}

void from_json(const json& j, TrickplayManifest& v) {
    get(j, "interval", v.interval);
    get(j, "width", v.width);
    get(j, "height", v.height);
    get(j, "cols", v.cols);
    get(j, "rows", v.rows);
    get(j, "count", v.count);
    get(j, "sheets", v.sheets);
}

std::vector<SeasonCard> ordered_seasons(std::vector<SeasonCard> seasons) {
    std::stable_sort(seasons.begin(), seasons.end(), [](const SeasonCard& a, const SeasonCard& b) {
        if ((a.season_number == 0) != (b.season_number == 0)) return b.season_number == 0;
        return a.season_number < b.season_number;
    });
    return seasons;
}

const Episode* neighbour_episode(const std::vector<Episode>& episodes, int file_id, int step) {
    std::vector<const Episode*> playable;
    for (auto& e : episodes)
        if (!e.files.empty()) playable.push_back(&e);
    auto at = std::find_if(playable.begin(), playable.end(), [&](const Episode* e) {
        return std::any_of(e->files.begin(), e->files.end(), [&](const MediaFile& f) { return f.id == file_id; });
    });
    if (at == playable.end()) return nullptr;
    long long i = (at - playable.begin()) + step;
    if (i < 0 || i >= static_cast<long long>(playable.size())) return nullptr;
    const Episode* here = *at;
    const Episode* target = playable[static_cast<size_t>(i)];
    if ((here->season_number == 0) != (target->season_number == 0)) return nullptr;
    return target;
}

}

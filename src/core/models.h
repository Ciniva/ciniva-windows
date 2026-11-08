#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace ciniva {

using json = nlohmann::json;

struct User {
    int id = 0;
    std::string username;
    bool admin = false;
};

struct SetupState {
    bool needs_admin = false;
    std::optional<bool> tmdb_configured;
    std::optional<std::string> version;
};

struct MovieCard {
    int id = 0;
    std::string title;
    std::optional<int> year;
    std::optional<std::string> poster_path;
    std::optional<std::string> backdrop_path;
    std::optional<double> rating;
    std::optional<std::string> added_at;
    std::optional<long long> position_ms;
    std::optional<long long> duration_ms;
    std::optional<bool> watched;

    std::optional<double> progress() const;
};

struct ShowCard {
    int id = 0;
    std::string title;
    std::optional<int> year;
    std::optional<std::string> poster_path;
    std::optional<std::string> backdrop_path;
    std::optional<double> rating;
    int episodes = 0;
    int watched_episodes = 0;
    std::optional<std::string> last_added;

    int unwatched() const { return episodes > watched_episodes ? episodes - watched_episodes : 0; }
};

struct ContinueItem {
    int file_id = 0;
    long long position_ms = 0;
    long long duration_ms = 0;
    std::optional<std::string> updated_at;
    std::optional<int> movie_id;
    std::optional<int> show_id;
    std::string title;
    std::optional<std::string> subtitle;
    std::optional<std::string> image_path;
    std::optional<std::string> poster_path;
    std::optional<bool> next_up;

    double progress() const { return duration_ms > 0 ? static_cast<double>(position_ms) / duration_ms : 0; }
};

struct VideoInfo {
    std::string codec;
    std::optional<std::string> profile;
    int width = 0;
    int height = 0;
    std::optional<int> bit_depth;
    std::optional<std::string> hdr;
    std::optional<int> dolby_vision_profile;
    std::optional<double> fps;
};

struct AudioInfo {
    int index = 0;
    std::string codec;
    std::optional<std::string> profile;
    std::optional<int> channels;
    std::optional<std::string> language;
    std::optional<std::string> title;
    bool atmos = false;
    bool is_default = false;

    std::string label() const;
};

struct SubtitleInfo {
    int index = 0;
    std::string codec;
    std::optional<std::string> language;
    std::optional<std::string> title;
    bool forced = false;
    bool is_default = false;
};

struct MediaInfo {
    std::optional<std::string> container;
    std::optional<double> duration;
    std::optional<long long> bitrate;
    std::optional<VideoInfo> video;
    std::vector<AudioInfo> audio;
    std::vector<SubtitleInfo> subtitles;

    const AudioInfo* default_audio() const;
    std::vector<std::string> tech_badges() const;
};

struct MediaFile {
    int id = 0;
    std::string path;
    long long size = 0;
    std::optional<double> duration;
    std::optional<MediaInfo> probe;
    std::optional<std::string> added_at;
    std::optional<double> credits_start;
    std::optional<std::string> credits_source;
    std::optional<long long> position_ms;
    std::optional<long long> duration_ms;
    std::optional<bool> watched;

    bool is_watched() const { return watched.value_or(false); }
    std::string file_name() const;
    double total_seconds() const;
    std::optional<double> progress() const;
    double resume_seconds() const;
    std::optional<std::string> time_left_label() const;
};

struct DownloadItem {
    std::string id;
    std::string title;
    std::optional<std::string> subtitle;
    std::string name;
    long long size = 0;
    double progress = 0;
    double available_fraction = 0;
    std::optional<double> available_secs;
    std::optional<double> duration;
    bool playable = false;
    std::optional<MediaInfo> probe;

    std::string label() const;
};

struct HomeData {
    std::vector<ContinueItem> continue_watching;
    std::vector<MovieCard> recent_movies;
    std::vector<ShowCard> recent_shows;
};

struct SearchResults {
    std::vector<MovieCard> movies;
    std::vector<ShowCard> shows;
};

struct CastMember {
    std::optional<std::string> name;
    std::optional<std::string> character;
    std::optional<std::string> profile_path;
};

struct MovieDetail {
    int id = 0;
    std::optional<int> tmdb_id;
    std::string title;
    std::optional<std::string> original_title;
    std::optional<int> year;
    std::optional<std::string> overview;
    std::optional<std::string> tagline;
    std::optional<std::string> poster_path;
    std::optional<std::string> backdrop_path;
    std::optional<int> runtime;
    std::optional<double> rating;
    std::vector<std::string> genres;
    std::optional<std::string> release_date;
    std::vector<CastMember> cast;
    std::vector<MediaFile> files;
};

struct SeasonCard {
    int season_number = 0;
    std::optional<std::string> name;
    std::optional<std::string> poster_path;
    std::optional<std::string> air_date;
    std::optional<int> episode_count;
    int available = 0;
    int watched = 0;

    std::string label() const;
};

struct ShowDetail {
    int id = 0;
    std::optional<int> tmdb_id;
    std::string title;
    std::optional<std::string> original_title;
    std::optional<int> year;
    std::optional<std::string> overview;
    std::optional<std::string> poster_path;
    std::optional<std::string> backdrop_path;
    std::optional<double> rating;
    std::vector<std::string> genres;
    std::optional<std::string> first_air_date;
    std::optional<std::string> status;
    std::vector<CastMember> cast;
    std::vector<SeasonCard> seasons;
};

struct Episode {
    int id = 0;
    int season_number = 0;
    int episode_number = 0;
    std::optional<std::string> title;
    std::optional<std::string> overview;
    std::optional<std::string> still_path;
    std::optional<std::string> air_date;
    std::optional<int> runtime;
    std::optional<double> rating;
    std::vector<MediaFile> files;

    const MediaFile* file() const { return files.empty() ? nullptr : &files.front(); }
    std::string display_title() const;
    std::string label() const;
};

struct TrickplayManifest {
    double interval = 10;
    int width = 0;
    int height = 0;
    int cols = 1;
    int rows = 1;
    int count = 0;
    std::optional<int> sheets;

    struct Tile {
        int sheet;
        int x;
        int y;
    };
    Tile tile_at(double t) const;
};

void from_json(const json& j, User& v);
void from_json(const json& j, SetupState& v);
void from_json(const json& j, MovieCard& v);
void from_json(const json& j, ShowCard& v);
void from_json(const json& j, ContinueItem& v);
void from_json(const json& j, VideoInfo& v);
void from_json(const json& j, AudioInfo& v);
void from_json(const json& j, SubtitleInfo& v);
void from_json(const json& j, MediaInfo& v);
void from_json(const json& j, MediaFile& v);
void from_json(const json& j, DownloadItem& v);
void from_json(const json& j, HomeData& v);
void from_json(const json& j, SearchResults& v);
void from_json(const json& j, CastMember& v);
void from_json(const json& j, MovieDetail& v);
void from_json(const json& j, SeasonCard& v);
void from_json(const json& j, ShowDetail& v);
void from_json(const json& j, Episode& v);
void from_json(const json& j, TrickplayManifest& v);

std::vector<SeasonCard> ordered_seasons(std::vector<SeasonCard> seasons);
const Episode* neighbour_episode(const std::vector<Episode>& episodes, int file_id, int step);

}

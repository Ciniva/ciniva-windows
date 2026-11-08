#pragma once

#include "core/http.h"
#include "core/models.h"
#include "core/plugins.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ciniva {

using Query = std::vector<std::pair<std::string, std::string>>;

struct ApiError {
    int status = 0;
    std::string message;

    bool unauthorized() const { return status == 401; }
};

template <class T>
struct Result {
    std::optional<T> value;
    ApiError error;

    bool ok() const { return value.has_value(); }
    static Result fail(ApiError e) { return {std::nullopt, std::move(e)}; }
};

class Client {
public:
    static constexpr const char* kClientName = "Ciniva for Windows";
    static constexpr const char* kUserAgent = "Ciniva-Windows/1.0";

    Client() = default;
    Client(std::string base_url, std::string token) : base_(std::move(base_url)), token_(std::move(token)) {}

    const std::string& base_url() const { return base_; }
    const std::string& token() const { return token_; }

    std::string api_url(std::string_view path, const Query& query = {}, bool with_token = false) const;
    std::string stream_url(int file_id) const;
    std::string subtitle_url(int file_id, int index) const;
    std::string trickplay_sheet_url(int file_id, int sheet) const;
    std::string download_stream_url(const std::string& id) const;
    std::optional<std::string> image_url(const std::optional<std::string>& path, const char* size = "w342") const;

    Result<json> send(const char* method, std::string_view path, const Query& query = {}, const json* body = nullptr) const;

    template <class T>
    Result<T> get(std::string_view path, const Query& query = {}) const {
        auto r = send("GET", path, query);
        if (!r.ok()) return Result<T>::fail(r.error);
        try {
            return {r.value->template get<T>(), {}};
        } catch (const json::exception&) {
            return Result<T>::fail({0, "The server sent a response Ciniva couldn't read."});
        }
    }

    static Result<SetupState> setup_state(const std::string& base_url);
    static Result<std::pair<Client, User>> login(const std::string& base_url, const std::string& username,
                                                 const std::string& password);

    Result<std::optional<User>> me() const;
    void logout() const;

    Result<HomeData> home() const { return get<HomeData>("/home"); }
    Result<std::vector<MovieCard>> movies(const std::string& sort) const {
        return get<std::vector<MovieCard>>("/movies", {{"sort", sort}});
    }
    Result<std::vector<ShowCard>> shows(const std::string& sort) const {
        return get<std::vector<ShowCard>>("/shows", {{"sort", sort}});
    }
    Result<MovieDetail> movie(int id) const { return get<MovieDetail>("/movies/" + std::to_string(id)); }
    Result<ShowDetail> show(int id) const { return get<ShowDetail>("/shows/" + std::to_string(id)); }
    Result<std::vector<Episode>> season(int show, int number) const {
        return get<std::vector<Episode>>("/shows/" + std::to_string(show) + "/seasons/" + std::to_string(number));
    }
    Result<SearchResults> search(const std::string& q) const { return get<SearchResults>("/search", {{"q", q}}); }
    Result<MediaFile> file(int id) const { return get<MediaFile>("/files/" + std::to_string(id)); }
    Result<std::vector<DownloadItem>> downloads() const { return get<std::vector<DownloadItem>>("/downloads"); }
    Result<DownloadItem> download(const std::string& id) const { return get<DownloadItem>("/downloads/" + id); }
    std::optional<TrickplayManifest> trickplay(int file_id) const {
        return get<TrickplayManifest>("/files/" + std::to_string(file_id) + "/trickplay").value;
    }
    Result<bool> save_progress(int file_id, long long position_ms, long long duration_ms) const;
    Result<bool> scan() const;
    Result<bool> set_watched(int file_id, bool watched) const;
    Result<json> plugins() const;
    Result<json> plugin_page(const std::string& plugin, const std::string& page, const json& params) const;
    Result<json> plugin_call(const std::string& plugin, const ciniva::plugins::Call& call, const json* body = nullptr) const;
    Result<json> plugin_action(const std::string& plugin, const std::string& action, const json& context) const;
    Result<json> plugin_settings(const std::string& plugin) const;
    Result<json> save_plugin_settings(const std::string& plugin, const json& values) const;

    HttpRequest events_request() const;

private:
    std::string base_;
    std::string token_;
};

}

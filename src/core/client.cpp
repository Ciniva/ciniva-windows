#include "core/client.h"

#include "core/url.h"

namespace ciniva {

namespace {

std::string encode_path(std::string_view path) {
    std::string out;
    size_t start = 0;
    while (start <= path.size()) {
        size_t slash = path.find('/', start);
        auto segment = path.substr(start, slash == std::string_view::npos ? std::string_view::npos : slash - start);
        out += url_encode(segment);
        if (slash == std::string_view::npos) break;
        out += '/';
        start = slash + 1;
    }
    return out;
}

std::string error_message(const HttpResponse& r) {
    try {
        auto j = json::parse(r.body);
        if (j.is_object() && j.contains("error") && j["error"].is_string()) return j["error"].get<std::string>();
    } catch (const json::exception&) {
    }
    if (r.status == 401) return "Your session has ended. Sign in again.";
    return "The server returned HTTP " + std::to_string(r.status) + ".";
}

}

std::string Client::api_url(std::string_view path, const Query& query, bool with_token) const {
    std::string raw(path);
    std::string extra;
    if (auto q = raw.find('?'); q != std::string::npos) {
        extra = raw.substr(q + 1);
        raw.resize(q);
    }
    if (raw.empty() || raw.front() != '/') raw.insert(raw.begin(), '/');
    std::string url = base_ + "/api" + encode_path(raw);
    std::string qs = extra;
    for (auto& [k, v] : query) {
        if (!qs.empty()) qs += '&';
        qs += url_encode(k) + '=' + url_encode(v);
    }
    if (with_token && !token_.empty()) {
        if (!qs.empty()) qs += '&';
        qs += "token=" + url_encode(token_);
    }
    if (!qs.empty()) url += '?' + qs;
    return url;
}

std::string Client::stream_url(int file_id) const {
    return api_url("/files/" + std::to_string(file_id) + "/stream", {}, true);
}

std::string Client::subtitle_url(int file_id, int index) const {
    return api_url("/files/" + std::to_string(file_id) + "/subtitles/" + std::to_string(index) + ".vtt", {}, true);
}

std::string Client::trickplay_sheet_url(int file_id, int sheet) const {
    return api_url("/files/" + std::to_string(file_id) + "/trickplay/" + std::to_string(sheet) + ".jpg", {}, true);
}

std::string Client::download_stream_url(const std::string& id) const {
    return api_url("/downloads/" + id + "/stream", {}, true);
}

std::string Client::live_url(const std::string& playlist) const {
    std::string_view p = playlist;
    if (p.rfind("/api/", 0) == 0) p.remove_prefix(4);
    return api_url(p, {}, true);
}

std::optional<std::string> Client::image_url(const std::optional<std::string>& path, const char* size) const {
    if (!path || path->empty()) return std::nullopt;
    const std::string& p = *path;
    if (p.rfind("http://", 0) == 0 || p.rfind("https://", 0) == 0) return p;
    if (p.rfind("/api/", 0) == 0) return api_url(std::string_view(p).substr(4), {}, true);
    std::string_view rest = p;
    while (!rest.empty() && rest.front() == '/') rest.remove_prefix(1);
    return api_url(std::string("/images/") + size + "/" + std::string(rest), {}, true);
}

Result<json> Client::send(const char* method, std::string_view path, const Query& query, const json* body) const {
    HttpRequest req;
    req.method = method;
    req.url = api_url(path, query);
    req.headers.push_back({"Accept", "application/json"});
    req.headers.push_back({"User-Agent", kUserAgent});
    if (!token_.empty()) req.headers.push_back({"Authorization", "Bearer " + token_});
    if (body) {
        req.body = body->dump();
        req.headers.push_back({"Content-Type", "application/json"});
    }
    HttpResponse r = http_send(req);
    if (!r.status) return Result<json>::fail({0, r.error});
    if (!r.ok()) return Result<json>::fail({r.status, error_message(r)});
    if (r.body.empty()) return {json(), {}};
    try {
        return {json::parse(r.body), {}};
    } catch (const json::exception&) {
        return Result<json>::fail({0, "The server sent a response Ciniva couldn't read."});
    }
}

Result<SetupState> Client::setup_state(const std::string& base_url) {
    return Client(base_url, "").get<SetupState>("/setup");
}

Result<std::pair<Client, User>> Client::login(const std::string& base_url, const std::string& username,
                                              const std::string& password) {
    Client anon(base_url, "");
    json body = {{"username", username}, {"password", password}, {"client", kClientName}};
    auto r = anon.send("POST", "/auth/login", {}, &body);
    if (!r.ok()) return Result<std::pair<Client, User>>::fail(r.error);
    try {
        auto token = r.value->at("token").get<std::string>();
        auto user = r.value->at("user").get<User>();
        return {std::pair{Client(base_url, token), user}, {}};
    } catch (const json::exception&) {
        return Result<std::pair<Client, User>>::fail({0, "The server sent a response Ciniva couldn't read."});
    }
}

Result<std::optional<User>> Client::me() const {
    auto r = send("GET", "/me");
    if (!r.ok()) return Result<std::optional<User>>::fail(r.error);
    if (r.value->is_object() && r.value->contains("user") && (*r.value)["user"].is_object())
        return {std::optional<User>((*r.value)["user"].get<User>()), {}};
    return {std::optional<User>(), {}};
}

void Client::logout() const { send("POST", "/auth/logout"); }

Result<bool> Client::save_progress(int file_id, long long position_ms, long long duration_ms) const {
    json body = {{"position_ms", position_ms}, {"duration_ms", duration_ms}};
    auto r = send("PUT", "/files/" + std::to_string(file_id) + "/progress", {}, &body);
    return r.ok() ? Result<bool>{true, {}} : Result<bool>::fail(r.error);
}

Result<bool> Client::scan() const {
    auto r = send("POST", "/scan");
    return r.ok() ? Result<bool>{true, {}} : Result<bool>::fail(r.error);
}

Result<bool> Client::set_watched(int file_id, bool watched) const {
    json body = {{"watched", watched}};
    auto r = send("POST", "/files/" + std::to_string(file_id) + "/watched", {}, &body);
    return r.ok() ? Result<bool>{true, {}} : Result<bool>::fail(r.error);
}

Result<json> Client::plugins() const {
    auto r = send("GET", "/plugins");
    if (!r.ok() && r.error.status == 404) return {json::array(), {}};
    return r;
}

static Query to_query(const json& params) {
    Query q;
    if (!params.is_object()) return q;
    for (auto& [k, v] : params.items())
        if (!v.is_null() && !v.is_object() && !v.is_array()) q.push_back({k, ciniva::plugins::to_text(v)});
    return q;
}

static std::string strip_slashes(std::string p) {
    while (!p.empty() && p.front() == '/') p.erase(p.begin());
    return p;
}

Result<json> Client::plugin_page(const std::string& plugin, const std::string& page, const json& params) const {
    return send("GET", "/plugins/" + plugin + "/ui/pages/" + page, to_query(params));
}

Result<json> Client::plugin_call(const std::string& plugin, const ciniva::plugins::Call& call, const json* body) const {
    const json& payload = body ? *body : call.body;
    std::string path = "/plugins/" + plugin + "/ui/" + strip_slashes(call.path);
    if (call.method == "GET") return send("GET", path, to_query(payload));
    json b = payload.is_object() ? payload : json::object();
    return send(call.method.c_str(), path, {}, &b);
}

Result<json> Client::plugin_action(const std::string& plugin, const std::string& action, const json& context) const {
    json b = {{"context", context}};
    return send("POST", "/plugins/" + plugin + "/ui/actions/" + action, {}, &b);
}

Result<json> Client::plugin_settings(const std::string& plugin) const { return send("GET", "/plugins/" + plugin + "/settings"); }

Result<json> Client::save_plugin_settings(const std::string& plugin, const json& values) const {
    json b = {{"values", values}};
    return send("PUT", "/plugins/" + plugin + "/settings", {}, &b);
}

HttpRequest Client::events_request() const {
    HttpRequest req;
    req.url = api_url("/events");
    req.headers.push_back({"Accept", "text/event-stream"});
    req.headers.push_back({"User-Agent", kUserAgent});
    if (!token_.empty()) req.headers.push_back({"Authorization", "Bearer " + token_});
    req.timeout_ms = 60000;
    return req;
}

Result<json> Client::start_live(const std::string& plugin, const std::string& stream) const {
    json b = ciniva::plugins::live_request();
    return send("POST", "/plugins/" + plugin + "/live/" + stream, {}, &b);
}

}

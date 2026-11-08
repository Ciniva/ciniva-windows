#include "app/app_model.h"

#include "app/timing.h"
#include "core/credentials.h"
#include "core/settings.h"
#include "core/sse.h"
#include "core/text.h"
#include "core/url.h"

namespace ciniva {

using nlohmann::json;

AppModel::AppModel() = default;

AppModel::~AppModel() {
    *alive_ = false;
    alive_.reset();
    stop_events();
}

void AppModel::changed() {
    if (on_changed) on_changed();
}

void AppModel::bootstrap() {
    error.clear();
    auto& s = Settings::get();
    auto base = normalize_server_url(s.server);
    auto token = base ? credentials::read(*base) : std::nullopt;
    if (!base || !token) {
        phase = Phase::SignedOut;
        changed();
        return;
    }
    std::weak_ptr<bool> alive = alive_;
    struct Probe {
        Client client;
        Result<std::optional<User>> me;
    };
    dispatch::async(
        [base = *base, token = *token]() -> Probe {
            // Older sign-ins stored http:// for hosts that redirect to TLS; move them to https so the token never
            // travels in the clear.
            if (base.rfind("http://", 0) == 0 && base.find(':', 7) == std::string::npos) {
                std::string secure = "https://" + base.substr(7);
                if (Client::setup_state(secure).ok()) {
                    Client c(secure, token);
                    return {c, c.me()};
                }
            }
            Client c(base, token);
            return {c, c.me()};
        },
        [this, alive, old_base = *base, username = s.username](Probe p) {
        if (alive.expired()) return;
        const Client& c = p.client;
        auto& r = p.me;
        std::string base = c.base_url();
        if (base != old_base && r.ok() && r.value->has_value()) {
            credentials::remove(old_base);
            credentials::save(base, username, c.token());
            auto& settings = Settings::get();
            settings.server = base;
            settings.save();
            timing::log("moved session from %s to %s", old_base.c_str(), base.c_str());
        }
        if (r.ok() && r.value->has_value()) {
            become_ready(c, **r.value);
        } else if ((r.ok() && !r.value->has_value()) || r.error.unauthorized()) {
            credentials::remove(base);
            phase = Phase::SignedOut;
            error = "Your session has ended. Sign in again.";
        } else {
            phase = Phase::SignedOut;
            error = "Couldn't reach " + base + ": " + r.error.message;
        }
        changed();
    });
}

void AppModel::sign_in(const std::string& server_text, const std::string& username, const std::string& password) {
    error.clear();
    auto base = normalize_server_url(server_text);
    if (!base) {
        error = "Enter a server address like http://srv1.local:8484";
        changed();
        return;
    }
    std::string name = trim(username);
    if (name.empty() || password.empty()) {
        error = "Enter your username and password.";
        changed();
        return;
    }
    signing_in = true;
    changed();

    struct Outcome {
        std::string error;
        std::optional<Client> client;
        std::optional<User> user;
    };
    std::weak_ptr<bool> alive = alive_;
    dispatch::async(
        [base = *base, name, password]() -> Outcome {
            auto setup = Client::setup_state(base);
            if (!setup.ok()) return {setup.error.message};
            if (setup.value->needs_admin)
                return {"This server isn't set up yet. Open " + base +
                        " in a browser to create the administrator account, then sign in here."};
            auto r = Client::login(base, name, password);
            if (!r.ok()) return {r.error.unauthorized() ? (r.error.message.empty() ? "Wrong username or password." : r.error.message)
                                                        : r.error.message};
            return {"", r.value->first, r.value->second};
        },
        [this, alive, base = *base, name](Outcome o) {
            if (alive.expired()) return;
            signing_in = false;
            if (!o.client) {
                error = o.error;
                changed();
                return;
            }
            credentials::save(base, name, o.client->token());
            auto& s = Settings::get();
            s.server = base;
            s.username = name;
            s.save();
            become_ready(*o.client, *o.user);
        });
}

void AppModel::become_ready(Client c, User u) {
    ++session_;
    client = std::move(c);
    user = std::move(u);
    error.clear();
    phase = Phase::Ready;
    timing::log("signed in to %s as %s", client->base_url().c_str(), user->username.c_str());
    load_plugins();
    start_events();
    changed();
}

void AppModel::sign_out() {
    if (client) {
        Client c = *client;
        dispatch::background([c] { c.logout(); });
    }
    clear_session();
    error.clear();
    changed();
}

void AppModel::session_expired() {
    if (!client) return;
    clear_session();
    error = "Your session has ended. Sign in again.";
    changed();
}

void AppModel::clear_session() {
    if (client) credentials::remove(client->base_url());
    stop_events();
    ++session_;
    client.reset();
    user.reset();
    plugins = json::array();
    scanning = false;
    phase = Phase::SignedOut;
}

void AppModel::toast(std::string text, Tone tone) {
    double now = timing::now_seconds();
    toasts.push_back({std::move(text), tone, now, now + (tone == Tone::Error ? 6 : 4)});
    if (toasts.size() > 4) toasts.erase(toasts.begin());
    changed();
}

void AppModel::expire_toasts(double now) {
    std::erase_if(toasts, [now](const Toast& t) { return t.expires <= now; });
}

void AppModel::rescan() {
    if (scanning) return;
    call([](const Client& c) { return c.scan(); }, [this](Result<bool> r) {
        if (!r.ok()) {
            if (!r.error.unauthorized()) toast("Couldn't start a scan: " + r.error.message, Tone::Error);
            return;
        }
        scanning = true;
        scan_started_ = timing::now_seconds();
        toast("Scanning the library\xE2\x80\xA6");
    });
}

void AppModel::play(const PlaybackRequest& request) {
    if (on_play) on_play(request);
}

void AppModel::library_changed() {
    ++library_version;
    changed();
}

void AppModel::set_watched(std::vector<int> file_ids, bool watched) {
    call(
        [file_ids, watched](const Client& c) {
            for (int id : file_ids) {
                auto r = c.set_watched(id, watched);
                if (!r.ok()) return r;
            }
            return Result<bool>{true, {}};
        },
        [this](Result<bool> r) {
            if (!r.ok() && !r.error.unauthorized()) toast(r.error.message, Tone::Error);
            library_changed();
        });
}

void AppModel::load_plugins() {
    call([](const Client& c) { return c.plugins(); }, [this](Result<json> r) {
        plugins = r.ok() && r.value->is_array() ? *r.value : json::array();
        changed();
    });
}

void AppModel::start_events() {
    stop_events();
    {
        std::lock_guard lock(events_mutex_);
        events_stop_ = false;
    }
    auto stream = std::make_shared<HttpStream>();
    events_stream_ = stream;
    HttpRequest req = client->events_request();
    int session = session_;
    std::weak_ptr<bool> alive = alive_;
    events_thread_ = std::thread([this, stream, req, session, alive] {
        for (;;) {
            SseParser parser;
            std::string error;
            int status = stream->run(req, [&](std::string_view chunk) {
                for (auto& e : parser.feed(chunk)) {
                    json data = json::parse(e.data, nullptr, false);
                    if (data.is_discarded()) data = e.data;
                    dispatch::post([this, alive, session, name = e.name, data] {
                        if (!alive.expired() && session == session_) handle_event(name, data);
                    });
                }
                return true;
            }, &error);
            if (status == 401) {
                dispatch::post([this, alive, session] {
                    if (!alive.expired() && session == session_) session_expired();
                });
                return;
            }
            std::unique_lock lock(events_mutex_);
            if (events_cv_.wait_for(lock, std::chrono::seconds(15), [this] { return events_stop_; })) return;
            stream->reset();
        }
    });
}

void AppModel::stop_events() {
    {
        std::lock_guard lock(events_mutex_);
        events_stop_ = true;
    }
    events_cv_.notify_all();
    if (events_stream_) events_stream_->cancel();
    if (events_thread_.joinable()) events_thread_.join();
    events_stream_.reset();
}

void AppModel::handle_event(const std::string& name, const json& data) {
    auto str = [&](const char* k) -> std::string {
        return data.is_object() && data.contains(k) && data[k].is_string() ? data[k].get<std::string>() : "";
    };
    if (name == "notification") {
        std::string title = str("title"), body = str("body");
        std::string text = title.empty() ? body : body.empty() ? title : title + " \xC2\xB7 " + body;
        if (!text.empty()) toast(text, str("tone") == "error" ? Tone::Error : Tone::Info);
    } else if (name == "library") {
        if (str("kind") == "scan.finished") {
            if (scanning) {
                auto num = [&](const char* k) {
                    return data.contains(k) && data[k].is_number() ? data[k].get<long long>() : 0;
                };
                toast("Library scan finished \xC2\xB7 " + std::to_string(num("added")) + " added, " +
                      std::to_string(num("removed")) + " removed");
            }
            scanning = false;
        }
        library_changed();
    }
}

}

#pragma once

#include "core/client.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace ciniva {

enum class Tone { Info, Success, Error };

struct Toast {
    std::string text;
    Tone tone = Tone::Info;
    double created = 0;
    double expires = 0;
};

struct PlaybackRequest {
    std::optional<int> file_id;
    std::string title;
    std::optional<std::string> subtitle;
    std::optional<int> show_id;
    std::optional<double> start_at;
    std::optional<std::string> url;
    std::optional<std::string> download_id;
};

class AppModel {
public:
    enum class Phase { Loading, SignedOut, Ready };

    AppModel();
    ~AppModel();

    Phase phase = Phase::Loading;
    std::optional<Client> client;
    std::optional<User> user;
    std::string error;
    bool signing_in = false;
    int library_version = 0;
    bool scanning = false;
    nlohmann::json plugins = nlohmann::json::array();
    std::vector<Toast> toasts;

    std::function<void()> on_changed;
    std::function<void(const PlaybackRequest&)> on_play;

    void bootstrap();
    void sign_in(const std::string& server_text, const std::string& username, const std::string& password);
    void sign_out();
    void session_expired();

    void toast(std::string text, Tone tone = Tone::Info);
    void expire_toasts(double now);
    void rescan();
    void play(const PlaybackRequest& request);
    void library_changed();

    // Runs a client call on the thread pool; a 401 sends the user back to sign-in before done sees the error.
    template <class Work, class Done>
    void call(Work work, Done done);

    std::shared_ptr<bool> alive() const { return alive_; }

private:
    void become_ready(Client c, User u);
    void clear_session();
    void start_events();
    void stop_events();
    void handle_event(const std::string& name, const nlohmann::json& data);
    void load_plugins();
    void changed();

    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    std::thread events_thread_;
    std::mutex events_mutex_;
    std::condition_variable events_cv_;
    bool events_stop_ = false;
    std::shared_ptr<HttpStream> events_stream_;
    int session_ = 0;
    double scan_started_ = 0;
};

}

#include "app/dispatch.h"

namespace ciniva {

template <class Work, class Done>
void AppModel::call(Work work, Done done) {
    if (!client) return;
    Client c = *client;
    int session = session_;
    std::weak_ptr<bool> alive = alive_;
    dispatch::async([c, work = std::move(work)]() mutable { return work(c); },
                    [this, alive, session, done = std::move(done)](auto result) mutable {
                        if (alive.expired()) return;
                        if (session == session_ && !result.ok() && result.error.unauthorized()) session_expired();
                        done(std::move(result));
                    });
}

}

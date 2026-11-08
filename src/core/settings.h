#pragma once

#include <string>

namespace ciniva {

struct Settings {
    static constexpr const char* kDefaultServer = "http://srv1.local:8484";

    std::string server = kDefaultServer;
    std::string username;
    bool hdr = true;
    int peak_nits = 0;
    bool passthrough = false;
    std::string audio_lang;
    std::string sub_lang;
    double volume = 100;
    bool autoplay_next = true;
    int seek_short = 10;
    int seek_medium = 30;
    int seek_long = 60;
    bool auto_update = true;

    static Settings& get();
    static std::wstring data_dir();
    void load();
    void save() const;
    std::string to_json() const;
    void from_json(const std::string& text);
};

}

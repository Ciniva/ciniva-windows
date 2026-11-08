#include "core/settings.h"

#include <nlohmann/json.hpp>

#include <windows.h>
#include <shlobj.h>

#include <fstream>
#include <sstream>

namespace ciniva {

using json = nlohmann::json;

Settings& Settings::get() {
    static Settings s = [] {
        Settings x;
        x.load();
        return x;
    }();
    return s;
}

std::wstring Settings::data_dir() {
    static std::wstring dir = [] {
        PWSTR base = nullptr;
        std::wstring d;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base))) {
            d = std::wstring(base) + L"\\Ciniva";
            CoTaskMemFree(base);
            CreateDirectoryW(d.c_str(), nullptr);
        }
        return d;
    }();
    return dir;
}

void Settings::load() {
    std::ifstream in(data_dir() + L"\\settings.json", std::ios::binary);
    if (!in) return;
    std::stringstream ss;
    ss << in.rdbuf();
    from_json(ss.str());
}

void Settings::save() const {
    std::wstring path = data_dir() + L"\\settings.json";
    std::wstring tmp = path + L".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return;
        out << to_json();
    }
    MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}

std::string Settings::to_json() const {
    json j = {
        {"server", server},
        {"username", username},
        {"hdr", hdr},
        {"peak_nits", peak_nits},
        {"passthrough", passthrough},
        {"audio_lang", audio_lang},
        {"sub_lang", sub_lang},
        {"volume", volume},
        {"autoplay_next", autoplay_next},
        {"seek_short", seek_short},
        {"seek_medium", seek_medium},
        {"seek_long", seek_long},
        {"auto_update", auto_update},
    };
    return j.dump(2);
}

void Settings::from_json(const std::string& text) {
    json j = json::parse(text, nullptr, false);
    if (!j.is_object()) return;
    auto read = [&](const char* key, auto& out) {
        auto it = j.find(key);
        if (it == j.end()) return;
        try {
            it->get_to(out);
        } catch (const json::exception&) {
        }
    };
    read("server", server);
    read("username", username);
    read("hdr", hdr);
    read("peak_nits", peak_nits);
    read("passthrough", passthrough);
    read("audio_lang", audio_lang);
    read("sub_lang", sub_lang);
    read("volume", volume);
    read("autoplay_next", autoplay_next);
    read("seek_short", seek_short);
    read("seek_medium", seek_medium);
    read("seek_long", seek_long);
    read("auto_update", auto_update);
}

}

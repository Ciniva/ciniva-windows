#include "core/update.h"

#include <monocypher-ed25519.h>
#include <nlohmann/json.hpp>

#include <cctype>
#include <vector>

namespace ciniva::update {

static std::vector<long> parts(std::string_view v) {
    std::vector<long> out;
    long cur = 0;
    bool any = false;
    for (char c : v) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            cur = cur * 10 + (c - '0');
            any = true;
        } else if (c == '.') {
            out.push_back(cur);
            cur = 0;
            any = false;
        } else {
            break;
        }
    }
    if (any || !out.empty()) out.push_back(cur);
    return out;
}

bool is_newer(std::string_view candidate, std::string_view current) {
    if (!candidate.empty() && candidate.front() == 'v') candidate.remove_prefix(1);
    if (!current.empty() && current.front() == 'v') current.remove_prefix(1);
    auto a = parts(candidate), b = parts(current);
    for (size_t i = 0; i < std::max(a.size(), b.size()); ++i) {
        long x = i < a.size() ? a[i] : 0, y = i < b.size() ? b[i] : 0;
        if (x != y) return x > y;
    }
    return false;
}

std::optional<std::string> base64_decode(std::string_view text) {
    auto value = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+' || c == '-') return 62;
        if (c == '/' || c == '_') return 63;
        return -1;
    };
    std::string out;
    unsigned buf = 0;
    int bits = 0;
    for (char c : text) {
        if (c == '=' || std::isspace(static_cast<unsigned char>(c))) continue;
        int v = value(c);
        if (v < 0) return std::nullopt;
        buf = (buf << 6) | static_cast<unsigned>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((buf >> bits) & 0xFF));
        }
    }
    return out;
}

bool verify(std::string_view data, std::string_view signature_base64, std::string_view public_key_base64) {
    auto sig = base64_decode(signature_base64);
    auto key = base64_decode(public_key_base64);
    if (!sig || !key || sig->size() != 64 || key->size() != 32) return false;
    return crypto_ed25519_check(reinterpret_cast<const uint8_t*>(sig->data()), reinterpret_cast<const uint8_t*>(key->data()),
                                reinterpret_cast<const uint8_t*>(data.data()), data.size()) == 0;
}

std::optional<Release> parse_release(const std::string& text) {
    auto j = nlohmann::json::parse(text, nullptr, false);
    if (!j.is_object()) return std::nullopt;
    if (j.value("draft", false) || j.value("prerelease", false)) return std::nullopt;
    std::string zip_name, zip_url, sig_url;
    if (!j.contains("assets") || !j["assets"].is_array()) return std::nullopt;
    for (auto& a : j["assets"]) {
        std::string name = a.value("name", "");
        if (name.size() > 4 && name.compare(name.size() - 4, 4, ".zip") == 0) {
            zip_name = name;
            zip_url = a.value("browser_download_url", "");
            break;
        }
    }
    if (zip_name.empty()) return std::nullopt;
    for (auto& a : j["assets"])
        if (a.value("name", "") == zip_name + ".sig") sig_url = a.value("browser_download_url", "");
    if (zip_url.empty() || sig_url.empty()) return std::nullopt;
    std::string tag = j.value("tag_name", "");
    if (!tag.empty() && tag.front() == 'v') tag.erase(0, 1);
    std::string notes = j.contains("body") && j["body"].is_string() ? j["body"].get<std::string>() : "";
    return Release{tag, notes, zip_url, sig_url};
}

}

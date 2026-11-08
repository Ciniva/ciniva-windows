#include "core/url.h"

#include <cctype>

namespace ciniva {

static std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

std::optional<std::string> normalize_server_url(std::string_view text) {
    std::string s(trim(text));
    if (s.empty()) return std::nullopt;
    if (s.find("://") == std::string::npos) s = "http://" + s;
    while (!s.empty() && s.back() == '/') s.pop_back();

    auto sep = s.find("://");
    std::string scheme = s.substr(0, sep);
    for (auto& c : scheme) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (scheme != "http" && scheme != "https") return std::nullopt;

    auto rest = std::string_view(s).substr(sep + 3);
    auto host_end = rest.find_first_of("/?#");
    auto authority = rest.substr(0, host_end);
    if (auto at = authority.rfind('@'); at != std::string_view::npos) authority.remove_prefix(at + 1);
    std::string_view host = authority;
    if (!host.empty() && host.front() == '[') {
        auto close = host.find(']');
        if (close == std::string_view::npos) return std::nullopt;
        host = host.substr(0, close + 1);
    } else if (auto colon = host.find(':'); colon != std::string_view::npos) {
        auto port = host.substr(colon + 1);
        host = host.substr(0, colon);
        if (port.empty()) return std::nullopt;
        for (char c : port)
            if (!std::isdigit(static_cast<unsigned char>(c))) return std::nullopt;
    }
    if (host.empty()) return std::nullopt;
    return scheme + s.substr(sep);
}

std::string url_encode(std::string_view s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 15]);
        }
    }
    return out;
}

}

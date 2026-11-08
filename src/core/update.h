#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace ciniva::update {

constexpr const char* kRepository = "Ciniva/ciniva-windows";
constexpr const char* kPublicKey = "LOrx2enHLJTnamVfTCugE+Hp6TKjKOv4BobPlUO8lQ4=";

struct Release {
    std::string version;
    std::string notes;
    std::string archive_url;
    std::string signature_url;
};

bool is_newer(std::string_view candidate, std::string_view current);
std::optional<std::string> base64_decode(std::string_view text);
bool verify(std::string_view data, std::string_view signature_base64, std::string_view public_key_base64 = kPublicKey);

// Parses a GitHub "latest release" response; nullopt when it has no zip with a matching .sig.
std::optional<Release> parse_release(const std::string& json);

}

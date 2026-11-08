#pragma once

#include <optional>
#include <string>

namespace ciniva {

std::optional<std::string> normalize_server_url(std::string_view text);

std::string url_encode(std::string_view s);

}

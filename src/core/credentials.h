#pragma once

#include <optional>
#include <string>

namespace ciniva::credentials {

std::optional<std::string> read(const std::string& server);
bool save(const std::string& server, const std::string& username, const std::string& token);
void remove(const std::string& server);

}

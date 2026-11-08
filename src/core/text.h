#pragma once

#include <string>
#include <string_view>

namespace ciniva {

std::wstring widen(std::string_view s);
std::string narrow(std::wstring_view s);

std::string format_clock(double seconds);
std::string format_runtime(int minutes);
std::string format_bytes(long long bytes);

std::string trim(std::string_view s);

}

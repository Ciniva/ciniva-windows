#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace ciniva {

struct SseEvent {
    std::string name;
    std::string data;
};

class SseParser {
public:
    std::vector<SseEvent> feed(std::string_view chunk);

private:
    void line(std::string_view l, std::vector<SseEvent>& out);

    std::string buffer_;
    std::string event_;
    std::string data_;
    bool has_data_ = false;
};

}

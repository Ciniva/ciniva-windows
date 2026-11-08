#include "core/sse.h"

namespace ciniva {

std::vector<SseEvent> SseParser::feed(std::string_view chunk) {
    std::vector<SseEvent> out;
    buffer_.append(chunk);
    size_t start = 0;
    for (;;) {
        size_t nl = buffer_.find_first_of("\r\n", start);
        if (nl == std::string::npos) break;
        // A lone CR at the end of the buffer may be the first half of CRLF.
        if (buffer_[nl] == '\r' && nl + 1 == buffer_.size()) break;
        line(std::string_view(buffer_).substr(start, nl - start), out);
        start = nl + 1;
        if (buffer_[nl] == '\r' && buffer_[start] == '\n') ++start;
    }
    buffer_.erase(0, start);
    return out;
}

void SseParser::line(std::string_view l, std::vector<SseEvent>& out) {
    if (l.empty()) {
        if (has_data_) out.push_back({event_.empty() ? "message" : event_, data_});
        event_.clear();
        data_.clear();
        has_data_ = false;
        return;
    }
    if (l.front() == ':') return;
    auto colon = l.find(':');
    std::string_view field = l.substr(0, colon);
    std::string_view value = colon == std::string_view::npos ? std::string_view() : l.substr(colon + 1);
    if (!value.empty() && value.front() == ' ') value.remove_prefix(1);
    if (field == "event") {
        event_ = value;
    } else if (field == "data") {
        if (has_data_) data_ += '\n';
        data_ += value;
        has_data_ = true;
    }
}

}

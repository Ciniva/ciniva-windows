#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ciniva {

struct HttpRequest {
    std::string method = "GET";
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    int timeout_ms = 20000;
};

struct HttpResponse {
    int status = 0;
    std::string body;
    std::string error;

    bool ok() const { return status >= 200 && status < 300; }
};

HttpResponse http_send(const HttpRequest& request);

// Streams the response body to on_data until it returns false, the connection ends, or cancel() is called.
class HttpStream {
public:
    HttpStream();
    ~HttpStream();
    int run(const HttpRequest& request, const std::function<bool(std::string_view)>& on_data, std::string* error);
    void cancel();
    void reset();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}

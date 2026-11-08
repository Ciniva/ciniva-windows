#include "core/http.h"
#include "core/text.h"

#include <windows.h>
#include <winhttp.h>

#include <mutex>

namespace ciniva {

namespace {

HINTERNET session() {
    static HINTERNET s = [] {
        HINTERNET h = WinHttpOpen(L"Ciniva-Windows/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                  WINHTTP_NO_PROXY_BYPASS, 0);
        if (h) {
            DWORD protocols = WINHTTP_PROTOCOL_FLAG_HTTP2;
            WinHttpSetOption(h, WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL, &protocols, sizeof protocols);
            DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_ALL;
            WinHttpSetOption(h, WINHTTP_OPTION_DECOMPRESSION, &decompression, sizeof decompression);
        }
        return h;
    }();
    return s;
}

std::string last_error_text(DWORD code) {
    switch (code) {
    case ERROR_WINHTTP_TIMEOUT: return "The server took too long to respond.";
    case ERROR_WINHTTP_CANNOT_CONNECT: return "Couldn't connect to the server.";
    case ERROR_WINHTTP_NAME_NOT_RESOLVED: return "The server name couldn't be found.";
    case ERROR_WINHTTP_CONNECTION_ERROR: return "The connection to the server was lost.";
    case ERROR_WINHTTP_SECURE_FAILURE: return "The server's certificate isn't trusted.";
    case ERROR_WINHTTP_OPERATION_CANCELLED: return "Cancelled.";
    case ERROR_WINHTTP_INVALID_URL:
    case ERROR_WINHTTP_UNRECOGNIZED_SCHEME: return "The server address isn't valid.";
    default: return "Network error " + std::to_string(code) + ".";
    }
}

struct Handles {
    HINTERNET connect = nullptr;
    HINTERNET request = nullptr;

    ~Handles() {
        if (request) WinHttpCloseHandle(request);
        if (connect) WinHttpCloseHandle(connect);
    }
};

// Opens and sends the request and receives the response headers; returns the HTTP status or 0 on failure.
int open(const HttpRequest& req, Handles& h, std::string* error, const std::function<void(HINTERNET)>& on_request) {
    HINTERNET s = session();
    if (!s) {
        *error = last_error_text(GetLastError());
        return 0;
    }
    std::wstring url = widen(req.url);
    URL_COMPONENTS uc = {sizeof uc};
    wchar_t host[256] = {}, path[4096] = {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = ARRAYSIZE(host);
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = ARRAYSIZE(path);
    wchar_t extra[8192] = {};
    uc.lpszExtraInfo = extra;
    uc.dwExtraInfoLength = ARRAYSIZE(extra);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) {
        *error = last_error_text(GetLastError());
        return 0;
    }

    h.connect = WinHttpConnect(s, host, uc.nPort, 0);
    if (!h.connect) {
        *error = last_error_text(GetLastError());
        return 0;
    }
    std::wstring target = std::wstring(path) + extra;
    std::wstring method = widen(req.method);
    DWORD flags = uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    h.request = WinHttpOpenRequest(h.connect, method.c_str(), target.c_str(), nullptr, WINHTTP_NO_REFERER,
                                   WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!h.request) {
        *error = last_error_text(GetLastError());
        return 0;
    }
    if (on_request) on_request(h.request);
    WinHttpSetTimeouts(h.request, req.timeout_ms, req.timeout_ms, req.timeout_ms, req.timeout_ms);

    std::wstring headers;
    for (auto& [k, v] : req.headers) headers += widen(k) + L": " + widen(v) + L"\r\n";
    BOOL sent = WinHttpSendRequest(h.request, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                                   static_cast<DWORD>(-1L),
                                   req.body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(req.body.data()),
                                   static_cast<DWORD>(req.body.size()), static_cast<DWORD>(req.body.size()), 0);
    if (!sent || !WinHttpReceiveResponse(h.request, nullptr)) {
        *error = last_error_text(GetLastError());
        return 0;
    }
    DWORD status = 0, size = sizeof status;
    WinHttpQueryHeaders(h.request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &size, WINHTTP_NO_HEADER_INDEX);
    return static_cast<int>(status);
}

}

HttpResponse http_send(const HttpRequest& req) {
    HttpResponse resp;
    Handles h;
    resp.status = open(req, h, &resp.error, nullptr);
    if (!resp.status) return resp;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(h.request, &avail)) {
            resp.error = last_error_text(GetLastError());
            resp.status = 0;
            return resp;
        }
        if (!avail) break;
        size_t at = resp.body.size();
        resp.body.resize(at + avail);
        DWORD read = 0;
        if (!WinHttpReadData(h.request, resp.body.data() + at, avail, &read)) {
            resp.error = last_error_text(GetLastError());
            resp.status = 0;
            return resp;
        }
        resp.body.resize(at + read);
    }
    return resp;
}

struct HttpStream::Impl {
    std::mutex mutex;
    HINTERNET request = nullptr;
    bool cancelled = false;
    bool closed = false;
};

HttpStream::HttpStream() : impl_(std::make_unique<Impl>()) {}
HttpStream::~HttpStream() = default;

int HttpStream::run(const HttpRequest& req, const std::function<bool(std::string_view)>& on_data, std::string* error) {
    Handles h;
    bool cancelled_early = false;
    int status = open(req, h, error, [&](HINTERNET r) {
        std::lock_guard lock(impl_->mutex);
        cancelled_early = impl_->cancelled;
        impl_->request = r;
    });
    auto detach = [&] {
        std::lock_guard lock(impl_->mutex);
        if (impl_->closed) h.request = nullptr;
        impl_->request = nullptr;
    };
    if (cancelled_early || !status || status < 200 || status >= 300) {
        detach();
        return status;
    }
    std::string buf(16384, '\0');
    for (;;) {
        DWORD read = 0;
        if (!WinHttpReadData(h.request, buf.data(), static_cast<DWORD>(buf.size()), &read)) {
            *error = last_error_text(GetLastError());
            break;
        }
        if (!read || !on_data(std::string_view(buf.data(), read))) break;
    }
    detach();
    return status;
}

void HttpStream::reset() {
    std::lock_guard lock(impl_->mutex);
    impl_->cancelled = false;
    impl_->closed = false;
}

void HttpStream::cancel() {
    std::lock_guard lock(impl_->mutex);
    if (impl_->cancelled) return;
    impl_->cancelled = true;
    // Closing the request handle from another thread aborts a blocking WinHttpReadData; run() then skips its own close.
    if (impl_->request) {
        WinHttpCloseHandle(impl_->request);
        impl_->closed = true;
    }
}

}

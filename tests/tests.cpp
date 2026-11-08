#include "core/url.h"

#include <cstdio>
#include <string>

static int failures = 0;

#define CHECK(expr)                                                         \
    do {                                                                    \
        if (!(expr)) {                                                      \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #expr); \
            ++failures;                                                     \
        }                                                                   \
    } while (0)

static void test_normalize() {
    using ciniva::normalize_server_url;
    CHECK(normalize_server_url("srv1.local:8484") == "http://srv1.local:8484");
    CHECK(normalize_server_url("  https://example.com/// ") == "https://example.com");
    CHECK(normalize_server_url("HTTP://10.0.0.20:8484/") == "http://10.0.0.20:8484");
    CHECK(normalize_server_url("http://[fd00::1]:8484") == "http://[fd00::1]:8484");
    CHECK(normalize_server_url("") == std::nullopt);
    CHECK(normalize_server_url("ftp://srv1") == std::nullopt);
    CHECK(normalize_server_url("http://") == std::nullopt);
    CHECK(normalize_server_url("srv1:abc") == std::nullopt);
}

static void test_encode() {
    CHECK(ciniva::url_encode("a b/c") == "a%20b%2Fc");
    CHECK(ciniva::url_encode("Amélie") == "Am%C3%A9lie");
}

int main() {
    test_normalize();
    test_encode();
    if (failures) std::fprintf(stderr, "%d failure(s)\n", failures);
    else std::puts("all tests passed");
    return failures ? 1 : 0;
}

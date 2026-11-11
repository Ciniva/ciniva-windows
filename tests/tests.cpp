#include "core/client.h"
#include "core/models.h"
#include "core/plugins.h"
#include "core/sse.h"
#include "core/update.h"
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

static void test_sse() {
    ciniva::SseParser p;
    auto a = p.feed("event: library\ndata: {\"kind\":\"scan.fin");
    CHECK(a.empty());
    auto b = p.feed("ished\"}\n\n: keep-alive\n\ndata: one\r\ndata: two\r\n\r");
    CHECK(b.size() == 1);
    CHECK(b[0].name == "library");
    CHECK(b[0].data == "{\"kind\":\"scan.finished\"}");
    auto c = p.feed("\n");
    CHECK(c.size() == 1);
    CHECK(c[0].name == "message");
    CHECK(c[0].data == "one\ntwo");
}

static void test_models() {
    auto j = ciniva::json::parse(R"({
        "id": 7, "title": "Dune", "year": null, "cast": "oops", "genres": ["Sci-Fi"],
        "files": [{"id": 3, "path": "/m/Dune (2021).mkv", "size": 1, "position_ms": 60000, "duration_ms": 120000,
                   "probe": {"audio": [{"index": 1, "codec": "truehd", "channels": 8, "atmos": true, "default": true}],
                             "subtitles": [], "video": {"codec": "hevc", "width": 3840, "height": 2160, "hdr": "hdr10",
                             "dolby_vision_profile": 8}}}]
    })");
    auto m = j.get<ciniva::MovieDetail>();
    CHECK(m.id == 7 && m.title == "Dune" && !m.year);
    CHECK(m.cast.empty());
    CHECK(m.files.size() == 1);
    CHECK(m.files[0].file_name() == "Dune (2021).mkv");
    CHECK(m.files[0].progress() == 0.5);
    CHECK(m.files[0].resume_seconds() == 60);
    auto badges = m.files[0].probe->tech_badges();
    CHECK(badges.size() == 6);
    CHECK(badges[0] == "4K" && badges[1] == "Dolby Vision P8" && badges[2] == "HDR10" && badges[3] == "HEVC");
    CHECK(badges[4] == "Atmos" && badges[5] == "TrueHD 7.1");

    ciniva::TrickplayManifest t;
    t.interval = 10; t.width = 320; t.height = 180; t.cols = 10; t.rows = 10; t.count = 250;
    auto tile = t.tile_at(1015);
    CHECK(tile.sheet == 2 && tile.x == 320 && tile.y == 0);
}

static void test_client_urls() {
    ciniva::Client c("http://srv1.local:8484", "a b");
    CHECK(c.stream_url(5) == "http://srv1.local:8484/api/files/5/stream?token=a%20b");
    CHECK(c.api_url("/search", {{"q", "x&y"}}) == "http://srv1.local:8484/api/search?q=x%26y");
    CHECK(c.image_url(std::string("/abc.jpg")) == "http://srv1.local:8484/api/images/w342/abc.jpg?token=a%20b");
    CHECK(c.image_url(std::string("/api/files/1/thumb?x=1")) == "http://srv1.local:8484/api/files/1/thumb?x=1&token=a%20b");
    CHECK(c.subtitle_url(2, 3) == "http://srv1.local:8484/api/files/2/subtitles/3.vtt?token=a%20b");
}

static void test_update() {
    using namespace ciniva::update;
    CHECK(is_newer("0.2.0", "0.1.9"));
    CHECK(is_newer("v1.10", "1.9.3"));
    CHECK(!is_newer("1.0", "1.0.0"));
    CHECK(!is_newer("0.9.9", "1.0"));

    // RFC 8032 test 2: one-byte message 0x72.
    const char* key = "PUAXw+hDiVqStwqnTRt+vJyYLM8uxJaMwM1V8Sr0Zgw=";
    const char* sig = "kqAJqfDUyrhyDoILX2QlQKKye1QWUD+Ps3YiI+vbadoIWsHkPhWZbkWPNhPQ8R2MOHsurrQwKu6wDSkWErsMAA==";
    CHECK(verify("r", sig, key));
    CHECK(!verify("s", sig, key));
    CHECK(!verify("r", sig));

    auto r = parse_release(R"({"tag_name": "v0.2.0", "draft": false, "prerelease": false, "body": "notes",
        "assets": [{"name": "Ciniva-0.2.0-win64.zip.sig", "browser_download_url": "https://x/s"},
                   {"name": "Ciniva-0.2.0-win64.zip", "browser_download_url": "https://x/z"}]})");
    CHECK(r && r->version == "0.2.0" && r->archive_url == "https://x/z" && r->signature_url == "https://x/s");
    CHECK(!parse_release(R"({"tag_name": "v0.2.0", "assets": [{"name": "a.zip", "browser_download_url": "u"}]})"));
}

static void test_live() {
    using namespace ciniva::plugins;
    auto v = parse_view(json::parse(R"({"title": "TV", "blocks": [
        {"type": "list", "items": [
            {"id": "a", "title": "One", "image": "https://logos/1.png", "play": {"stream": "one"}},
            {"id": "b", "title": "Guide", "link": {"page": "guide"}},
            {"id": "c", "title": "Two", "play": {"stream": "two.hd", "title": "Two HD"}, "link": {"page": "x"},
             "actions": [{"label": "Watch", "play": {"stream": "two.hd"}}, {"label": "Fav", "call": {"path": "fav"}}]},
            {"id": "d", "title": "Bad", "play": {"title": "no stream"}},
            {"id": "e", "title": "Bad2", "play": "three"}
        ]},
        {"type": "actions", "items": [{"label": "Live", "play": {"stream": "news", "title": "News"}}]}
    ]})"));
    CHECK(v.blocks.size() == 2);
    const auto& items = v.blocks[0].items;
    CHECK(items.size() == 5);
    CHECK(items[0].play && items[0].play->stream == "one" && items[0].play->title.empty());
    CHECK(!items[1].play && items[1].link);
    CHECK(items[2].play && items[2].play->title == "Two HD" && items[2].link && items[2].link->page == "x");
    CHECK(items[2].actions.size() == 2 && items[2].actions[0].play && items[2].actions[0].play->stream == "two.hd");
    CHECK(!items[2].actions[1].play && items[2].actions[1].call);
    CHECK(!items[3].play && !items[4].play);
    CHECK(v.blocks[1].actions.size() == 1 && v.blocks[1].actions[0].play && v.blocks[1].actions[0].play->title == "News");

    auto ch = channels(v.blocks[0]);
    CHECK(ch.size() == 2);
    CHECK(ch[0].stream == "one" && ch[0].title == "One" && ch[0].image == "https://logos/1.png");
    CHECK(ch[1].stream == "two.hd" && ch[1].title == "Two HD" && ch[1].image.empty());

    CHECK(step_channel(0, -1, 3) == 2);
    CHECK(step_channel(2, 1, 3) == 0);
    CHECK(step_channel(1, 1, 3) == 2);
    CHECK(step_channel(0, 1, 1) == 0);
    CHECK(step_channel(0, 1, 0) == 0);

    auto body = live_request();
    CHECK(body["video_copy"] == "h264,hevc,av1,vp9");
    CHECK(body["audio_copy"] == "aac,ac3,eac3");
    CHECK(body["max_height"] == 2160 && body["audio_channels"] == 2);

    auto l = parse_live(json::parse(R"({"session": "s1", "playlist": "/api/live/s1/index.m3u8", "plan": {}, "live": true,
        "title": "One", "image": null})"));
    CHECK(l && l->session == "s1" && l->playlist == "/api/live/s1/index.m3u8" && l->title == "One" && l->image.empty());
    CHECK(!parse_live(json::parse(R"({"error": "channel is offline"})")));
    CHECK(!parse_live(json::array()));

    ciniva::Client c("http://srv1.local:8484", "a b");
    CHECK(c.live_url("/api/live/s1/index.m3u8") == "http://srv1.local:8484/api/live/s1/index.m3u8?token=a%20b");
}

int main() {
    test_normalize();
    test_encode();
    test_sse();
    test_models();
    test_client_urls();
    test_update();
    test_live();
    if (failures) std::fprintf(stderr, "%d failure(s)\n", failures);
    else std::puts("all tests passed");
    return failures ? 1 : 0;
}

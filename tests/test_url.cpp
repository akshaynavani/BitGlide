#include "bitglide/url.h"
#include "test_framework.h"

using namespace bitglide;

TEST(url_parses_http_with_port) {
    auto u = parse_url("http://localhost:8080/live/master.m3u8?token=1");
    EXPECT_TRUE(u.has_value());
    EXPECT_EQ(u->scheme, std::string("http"));
    EXPECT_EQ(u->host, std::string("localhost"));
    EXPECT_EQ(u->port, 8080);
    EXPECT_EQ(u->path, std::string("/live/master.m3u8?token=1"));
}

TEST(url_defaults_port_80_and_root_path) {
    auto u = parse_url("http://example.com");
    EXPECT_TRUE(u.has_value());
    EXPECT_EQ(u->port, 80);
    EXPECT_EQ(u->path, std::string("/"));
}

TEST(url_rejects_unsupported_schemes) {
    EXPECT_FALSE(parse_url("https://example.com/a").has_value());
    EXPECT_FALSE(parse_url("not a url").has_value());
    EXPECT_FALSE(parse_url("http://host:99999/").has_value());
}

TEST(url_resolves_relative_references) {
    const std::string base = "http://h:8080/a/b/master.m3u8";
    EXPECT_EQ(resolve_url(base, "720p/index.m3u8"), std::string("http://h:8080/a/b/720p/index.m3u8"));
    EXPECT_EQ(resolve_url(base, "../x.ts"), std::string("http://h:8080/a/x.ts"));
    EXPECT_EQ(resolve_url(base, "/abs.ts"), std::string("http://h:8080/abs.ts"));
    EXPECT_EQ(resolve_url(base, "http://cdn/seg.ts"), std::string("http://cdn/seg.ts"));
}

TEST(url_resolves_against_file_base) {
    EXPECT_EQ(resolve_url("file:///tmp/media/master.m3u8", "v0/index.m3u8"),
              std::string("file:///tmp/media/v0/index.m3u8"));
}

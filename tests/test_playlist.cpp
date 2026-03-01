#include "bitglide/playlist.h"
#include "test_framework.h"

using namespace bitglide;

static const char* kMaster = R"(#EXTM3U
#EXT-X-VERSION:3
#EXT-X-STREAM-INF:BANDWIDTH=3000000,AVERAGE-BANDWIDTH=2800000,RESOLUTION=1280x720,CODECS="avc1.4d401f,mp4a.40.2"
720p/index.m3u8
#EXT-X-STREAM-INF:BANDWIDTH=800000,RESOLUTION=640x360
360p/index.m3u8
#EXT-X-STREAM-INF:BANDWIDTH=6000000,RESOLUTION=1920x1080
http://cdn.example/1080p/index.m3u8
)";

static const char* kMedia = R"(#EXTM3U
#EXT-X-VERSION:4
#EXT-X-TARGETDURATION:6
#EXT-X-MEDIA-SEQUENCE:100
#EXT-X-PLAYLIST-TYPE:VOD
#EXTINF:6.000,
seg100.ts
#EXTINF:6.000,
seg101.ts
#EXT-X-BYTERANGE:1000@0
#EXTINF:4.5,
all.ts
#EXT-X-BYTERANGE:2000
#EXTINF:3.0,
all.ts
#EXT-X-ENDLIST
)";

TEST(playlist_detects_master) {
    EXPECT_TRUE(is_master_playlist(kMaster));
    EXPECT_FALSE(is_master_playlist(kMedia));
}

TEST(playlist_parses_and_sorts_variants) {
    auto m = parse_master_playlist(kMaster, "http://h/vod/master.m3u8");
    EXPECT_EQ(m.variants.size(), size_t(3));
    EXPECT_EQ(m.variants[0].bandwidth_bps, uint64_t(800000));
    EXPECT_EQ(m.variants[1].effective_bps(), uint64_t(2800000));
    EXPECT_EQ(m.variants[2].height, 1080);
    EXPECT_EQ(m.variants[1].codecs, std::string("avc1.4d401f,mp4a.40.2"));
    EXPECT_EQ(m.variants[0].uri, std::string("http://h/vod/360p/index.m3u8"));
    EXPECT_EQ(m.variants[2].uri, std::string("http://cdn.example/1080p/index.m3u8"));
    for (int i = 0; i < 3; ++i) EXPECT_EQ(m.variants[static_cast<size_t>(i)].index, i);
}

TEST(playlist_parses_media_segments) {
    auto p = parse_media_playlist(kMedia, "http://h/vod/720p/index.m3u8");
    EXPECT_EQ(p.segments.size(), size_t(4));
    EXPECT_TRUE(p.endlist);
    EXPECT_NEAR(p.target_duration_s, 6.0, 1e-9);
    EXPECT_EQ(p.segments[0].sequence, uint64_t(100));
    EXPECT_EQ(p.segments[3].sequence, uint64_t(103));
    EXPECT_EQ(p.segments[1].uri, std::string("http://h/vod/720p/seg101.ts"));
    EXPECT_NEAR(p.total_duration_s(), 19.5, 1e-9);
}

TEST(playlist_tracks_implicit_byterange_offsets) {
    auto p = parse_media_playlist(kMedia, "http://h/x.m3u8");
    EXPECT_EQ(p.segments[2].byterange_offset, int64_t(0));
    EXPECT_EQ(p.segments[2].byterange_length, int64_t(1000));
    EXPECT_EQ(p.segments[3].byterange_offset, int64_t(1000));
    EXPECT_EQ(p.segments[3].byterange_length, int64_t(2000));
    EXPECT_EQ(p.segments[0].byterange_length, int64_t(-1));
}

TEST(playlist_attribute_list_handles_quoted_commas) {
    auto a = parse_attribute_list(R"(BANDWIDTH=1,CODECS="a,b",NAME="x y")");
    EXPECT_EQ(a["CODECS"], std::string("a,b"));
    EXPECT_EQ(a["NAME"], std::string("x y"));
    EXPECT_EQ(a["BANDWIDTH"], std::string("1"));
}

TEST(playlist_rejects_malformed_input) {
    EXPECT_THROW(parse_media_playlist("seg.ts\n", "http://h/"), PlaylistError);
    EXPECT_THROW(parse_media_playlist("#EXTM3U\nseg.ts\n", "http://h/"), PlaylistError);
    EXPECT_THROW(parse_master_playlist("#EXTM3U\n#EXT-X-STREAM-INF:RESOLUTION=1x1\na.m3u8\n", "http://h/"),
                 PlaylistError);
}

TEST(playlist_live_without_endlist) {
    auto p = parse_media_playlist("#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXT-X-MEDIA-SEQUENCE:7\n#EXTINF:2,\na.ts\n",
                                  "http://h/live.m3u8");
    EXPECT_FALSE(p.endlist);
    EXPECT_EQ(p.segments[0].sequence, uint64_t(7));
}

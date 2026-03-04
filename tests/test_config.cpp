#include <cstdio>
#include <fstream>

#include "bitglide/config.h"
#include "test_framework.h"

using namespace bitglide;

TEST(config_parses_flags_and_positional_url) {
    Config c;
    const char* argv[] = {"bitglide", "--abr=buffer", "--workers", "4", "--lookahead=5", "http://h/master.m3u8"};
    EXPECT_TRUE(parse_args(c, 6, const_cast<char**>(argv)));
    EXPECT_EQ(c.abr, std::string("buffer"));
    EXPECT_EQ(c.workers, 4);
    EXPECT_EQ(c.lookahead_segments, 5);
    EXPECT_EQ(c.url, std::string("http://h/master.m3u8"));
    validate(c);
}

TEST(config_rejects_bad_values) {
    Config c;
    EXPECT_THROW(apply_setting(c, "workers", "two"), std::invalid_argument);
    EXPECT_THROW(apply_setting(c, "nonsense", "1"), std::invalid_argument);
    EXPECT_THROW(apply_setting(c, "abandon-slow", "maybe"), std::invalid_argument);
    c.url = "http://h/x.m3u8";
    c.abr = "unknown";
    EXPECT_THROW(validate(c), std::invalid_argument);
}

TEST(config_loads_file) {
    const char* path = "bitglide_test_config.conf";
    {
        std::ofstream out(path);
        out << "# comment\nabr = throughput\nsafety-factor = 0.7  # inline\nplayback-rate=2\n";
    }
    Config c;
    load_config_file(c, path);
    std::remove(path);
    EXPECT_EQ(c.abr, std::string("throughput"));
    EXPECT_NEAR(c.tuning.safety_factor, 0.7, 1e-9);
    EXPECT_NEAR(c.player.playback_rate, 2.0, 1e-9);
}

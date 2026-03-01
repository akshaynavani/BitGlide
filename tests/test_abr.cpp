#include "bitglide/abr.h"
#include "test_framework.h"

using namespace bitglide;

static std::vector<Variant> ladder() {
    std::vector<Variant> v(4);
    const uint64_t rates[] = {500000, 1500000, 3000000, 6000000};
    for (int i = 0; i < 4; ++i) {
        v[static_cast<size_t>(i)].index = i;
        v[static_cast<size_t>(i)].bandwidth_bps = rates[i];
    }
    return v;
}

static AbrContext ctx(const std::vector<Variant>& vs, uint64_t est, double buffer, int current) {
    AbrContext c;
    c.variants = &vs;
    c.estimate_bps = est;
    c.harmonic_bps = est;
    c.buffer_level_s = buffer;
    c.current_index = current;
    c.segment_duration_s = 6.0;
    return c;
}

TEST(abr_highest_fitting) {
    auto vs = ladder();
    EXPECT_EQ(highest_fitting(vs, 100), 0);
    EXPECT_EQ(highest_fitting(vs, 1500000), 1);
    EXPECT_EQ(highest_fitting(vs, 1e9), 3);
}

TEST(abr_throughput_respects_safety_factor) {
    auto vs = ladder();
    ThroughputAbr abr;
    EXPECT_EQ(abr.choose(ctx(vs, 4000000, 0, -1)), 2);  // 3.4M budget
    EXPECT_EQ(abr.choose(ctx(vs, 3400000, 0, 2)), 1);   // 2.89M budget
    EXPECT_EQ(abr.choose(ctx(vs, 100000, 0, 3)), 0);
}

TEST(abr_buffer_based_rate_map) {
    auto vs = ladder();
    BufferBasedAbr abr;
    EXPECT_EQ(abr.choose(ctx(vs, 1e9, 2.0, -1)), 0);   // inside reservoir
    EXPECT_EQ(abr.choose(ctx(vs, 0, 30.0, -1)), 3);    // above cushion
    EXPECT_EQ(abr.choose(ctx(vs, 0, 15.0, -1)), 2);    // target 3.25M
}

TEST(abr_buffer_based_hysteresis_holds_rate) {
    auto vs = ladder();
    BufferBasedAbr abr;
    // target at 15s is 3.25M: between neighbours of v2 (1.5M, 6M) so stay at 2.
    EXPECT_EQ(abr.choose(ctx(vs, 0, 15.0, 2)), 2);
}

TEST(abr_hybrid_steps_up_one_rendition_at_a_time) {
    auto vs = ladder();
    HybridAbr abr;
    EXPECT_EQ(abr.choose(ctx(vs, 10000000, 10.0, 1)), 2);
}

TEST(abr_hybrid_requires_buffer_before_upswitch) {
    auto vs = ladder();
    HybridAbr abr;
    EXPECT_EQ(abr.choose(ctx(vs, 10000000, 5.0, 1)), 1);
}

TEST(abr_hybrid_downswitches_when_unsustainable) {
    auto vs = ladder();
    HybridAbr abr;
    EXPECT_EQ(abr.choose(ctx(vs, 2000000, 5.0, 3)), 1);
}

TEST(abr_hybrid_rides_out_dip_with_deep_buffer) {
    auto vs = ladder();
    HybridAbr abr;
    // v2 at 3M over 6s on 3.4M link ~ 5.3s download; 25s of buffer absorbs it.
    EXPECT_EQ(abr.choose(ctx(vs, 3400000, 25.0, 2)), 2);
}

TEST(abr_hybrid_panics_on_near_empty_buffer) {
    auto vs = ladder();
    HybridAbr abr;
    EXPECT_EQ(abr.choose(ctx(vs, 10000000, 1.0, 2)), 1);
}

TEST(abr_factory) {
    EXPECT_EQ(make_abr("buffer")->name(), std::string("buffer"));
    EXPECT_EQ(make_abr("hybrid")->name(), std::string("hybrid"));
    EXPECT_THROW(make_abr("magic"), std::invalid_argument);
}

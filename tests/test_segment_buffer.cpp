#include <chrono>
#include <thread>

#include "bitglide/segment_buffer.h"
#include "test_framework.h"

using namespace bitglide;
using namespace std::chrono_literals;

static Segment seg(uint64_t seq, double dur = 2.0, int variant = 0) {
    Segment s;
    s.sequence = seq;
    s.duration_s = dur;
    s.variant_index = variant;
    return s;
}

TEST(buffer_counts_only_contiguous_media) {
    SegmentBuffer buf(10);
    buf.push(seg(12));
    EXPECT_NEAR(buf.level_s(), 0.0, 1e-9);
    EXPECT_NEAR(buf.total_s(), 2.0, 1e-9);
    buf.push(seg(10));
    EXPECT_NEAR(buf.level_s(), 2.0, 1e-9);
    buf.push(seg(11));
    EXPECT_NEAR(buf.level_s(), 6.0, 1e-9);
}

TEST(buffer_pops_in_sequence_order) {
    SegmentBuffer buf(0);
    buf.push(seg(1, 2.0, 1));
    buf.push(seg(0, 2.0, 0));
    auto a = buf.pop_next(0ms);
    auto b = buf.pop_next(0ms);
    EXPECT_TRUE(a && b);
    EXPECT_EQ(a->sequence, uint64_t(0));
    EXPECT_EQ(b->sequence, uint64_t(1));
    EXPECT_EQ(b->variant_index, 1);
    EXPECT_FALSE(buf.pop_next(10ms).has_value());
}

TEST(buffer_rejects_duplicates_and_stale_segments) {
    SegmentBuffer buf(5);
    EXPECT_TRUE(buf.push(seg(5)));
    EXPECT_FALSE(buf.push(seg(5)));
    EXPECT_FALSE(buf.push(seg(4)));
}

TEST(buffer_pop_blocks_until_push) {
    SegmentBuffer buf(0);
    std::thread producer([&] {
        std::this_thread::sleep_for(30ms);
        buf.push(seg(0));
    });
    auto s = buf.pop_next(2000ms);
    producer.join();
    EXPECT_TRUE(s.has_value());
}

TEST(buffer_signals_end_of_stream) {
    SegmentBuffer buf(0);
    buf.set_end_sequence(0);
    buf.push(seg(0));
    EXPECT_FALSE(buf.finished());
    EXPECT_TRUE(buf.pop_next(0ms).has_value());
    EXPECT_TRUE(buf.finished());
    EXPECT_FALSE(buf.pop_next(1000ms).has_value()); // returns immediately at end
}

TEST(buffer_wait_for_room_unblocks_on_pop) {
    SegmentBuffer buf(0);
    buf.push(seg(0, 5.0));
    buf.push(seg(1, 5.0));
    EXPECT_FALSE(buf.wait_for_room(8.0, 10ms));
    std::thread consumer([&] {
        std::this_thread::sleep_for(20ms);
        buf.pop_next(0ms);
    });
    EXPECT_TRUE(buf.wait_for_room(8.0, 2000ms));
    consumer.join();
}

TEST(buffer_close_wakes_waiters) {
    SegmentBuffer buf(0);
    std::thread closer([&] {
        std::this_thread::sleep_for(20ms);
        buf.close();
    });
    auto t0 = std::chrono::steady_clock::now();
    EXPECT_FALSE(buf.pop_next(5000ms).has_value());
    closer.join();
    EXPECT_TRUE(std::chrono::steady_clock::now() - t0 < 2000ms);
    EXPECT_FALSE(buf.push(seg(0)));
}

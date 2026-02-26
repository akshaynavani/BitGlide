#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>

#include "bitglide/types.h"

namespace bitglide {

// Thread-safe playback buffer. Prefetch workers may complete segments out of order,
// so segments are keyed by sequence number and only the contiguous run starting at the
// playhead counts as playable buffer.
class SegmentBuffer {
public:
    explicit SegmentBuffer(uint64_t first_sequence = 0);

    // Inserts a downloaded segment. Duplicates and segments behind the playhead are dropped.
    bool push(const Segment& seg);

    // Blocks until the next in-order segment is available, the buffer is closed, or the
    // timeout elapses. Returns nullopt on timeout/close.
    std::optional<Segment> pop_next(std::chrono::milliseconds timeout);

    // Seconds of media in the contiguous run starting at the playhead.
    double level_s() const;
    // Seconds of media held, including out-of-order segments past a gap.
    double total_s() const;
    size_t size() const;
    uint64_t next_sequence() const;
    bool has(uint64_t sequence) const;

    // Blocks until level_s() < max_s, close() is called or the timeout elapses.
    // Returns true if there is room.
    bool wait_for_room(double max_s, std::chrono::milliseconds timeout);

    // Marks the last sequence of the stream; pop_next returns nullopt once it is consumed.
    void set_end_sequence(uint64_t last);
    bool finished() const;

    void close();
    bool closed() const;

private:
    double level_locked() const;

    mutable std::mutex mu_;
    std::condition_variable data_cv_;  // signalled on push/close
    std::condition_variable room_cv_;  // signalled on pop/close
    std::map<uint64_t, Segment> segments_;
    uint64_t next_seq_;
    std::optional<uint64_t> end_seq_;
    bool closed_ = false;
};

} // namespace bitglide

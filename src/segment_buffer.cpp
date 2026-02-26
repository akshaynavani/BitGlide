#include "bitglide/segment_buffer.h"

namespace bitglide {

SegmentBuffer::SegmentBuffer(uint64_t first_sequence) : next_seq_(first_sequence) {}

bool SegmentBuffer::push(const Segment& seg) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (closed_ || seg.sequence < next_seq_ || segments_.count(seg.sequence)) return false;
        segments_.emplace(seg.sequence, seg);
    }
    data_cv_.notify_all();
    return true;
}

std::optional<Segment> SegmentBuffer::pop_next(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mu_);
    const bool ready = data_cv_.wait_for(lock, timeout, [&] {
        return closed_ || segments_.count(next_seq_) || (end_seq_ && next_seq_ > *end_seq_);
    });
    if (!ready || closed_) return std::nullopt;
    auto it = segments_.find(next_seq_);
    if (it == segments_.end()) return std::nullopt; // past end of stream
    Segment seg = it->second;
    segments_.erase(it);
    ++next_seq_;
    lock.unlock();
    room_cv_.notify_all();
    return seg;
}

double SegmentBuffer::level_locked() const {
    double level = 0.0;
    uint64_t expect = next_seq_;
    for (auto it = segments_.find(expect); it != segments_.end() && it->first == expect; ++it, ++expect)
        level += it->second.duration_s;
    return level;
}

double SegmentBuffer::level_s() const {
    std::lock_guard<std::mutex> lock(mu_);
    return level_locked();
}

double SegmentBuffer::total_s() const {
    std::lock_guard<std::mutex> lock(mu_);
    double total = 0.0;
    for (const auto& kv : segments_) total += kv.second.duration_s;
    return total;
}

size_t SegmentBuffer::size() const {
    std::lock_guard<std::mutex> lock(mu_);
    return segments_.size();
}

uint64_t SegmentBuffer::next_sequence() const {
    std::lock_guard<std::mutex> lock(mu_);
    return next_seq_;
}

bool SegmentBuffer::has(uint64_t sequence) const {
    std::lock_guard<std::mutex> lock(mu_);
    return segments_.count(sequence) > 0;
}

bool SegmentBuffer::wait_for_room(double max_s, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mu_);
    room_cv_.wait_for(lock, timeout, [&] { return closed_ || level_locked() < max_s; });
    return !closed_ && level_locked() < max_s;
}

void SegmentBuffer::set_end_sequence(uint64_t last) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        end_seq_ = last;
    }
    data_cv_.notify_all();
}

bool SegmentBuffer::finished() const {
    std::lock_guard<std::mutex> lock(mu_);
    return end_seq_ && next_seq_ > *end_seq_;
}

void SegmentBuffer::close() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        closed_ = true;
    }
    data_cv_.notify_all();
    room_cv_.notify_all();
}

bool SegmentBuffer::closed() const {
    std::lock_guard<std::mutex> lock(mu_);
    return closed_;
}

} // namespace bitglide

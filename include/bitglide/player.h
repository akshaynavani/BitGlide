#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "bitglide/segment_buffer.h"
#include "bitglide/thread.h"

namespace bitglide {

enum class PlayerState { Idle, Startup, Playing, Stalled, Ended, Stopped };
const char* to_string(PlayerState s);

struct PlayerConfig {
    double startup_buffer_s = 4.0;   // media needed before first frame
    double resume_buffer_s = 4.0;    // media needed to leave a stall
    double playback_rate = 1.0;      // >1 plays faster than wall-clock (for quick simulations)
    int tick_ms = 50;
};

struct PlayerEvent {
    double wall_s = 0.0;      // seconds since player start
    double position_s = 0.0;  // media playhead
    std::string type;         // "startup", "stall", "resume", "switch", "ended"
    int variant_index = -1;
    double value = 0.0;       // stall duration for "resume"
};

struct PlayerStats {
    double startup_delay_s = 0.0;
    double played_s = 0.0;
    double stall_time_s = 0.0;
    int stall_count = 0;
    int switch_count = 0;
    double weighted_bitrate_sum = 0.0; // sum(bitrate * duration), divide by played_s
    std::vector<PlayerEvent> events;
};

// Simulated player: a dedicated thread drains the SegmentBuffer at the media clock rate,
// tracking startup delay, stalls (rebuffering) and quality switches. No decoding happens.
class Player {
public:
    Player(SegmentBuffer& buffer, PlayerConfig cfg, std::vector<uint64_t> variant_bitrates);
    ~Player();

    void start();
    void stop();
    void wait_until_done();

    PlayerState state() const { return state_.load(); }
    // Buffered media ahead of the playhead, including the unplayed part of the current segment.
    double buffer_ahead_s() const;
    double position_s() const;
    PlayerStats stats() const;

private:
    void run();
    double now_s() const;
    void record(const std::string& type, int variant, double value = 0.0);
    bool wait_for_level(double target_s);

    SegmentBuffer& buffer_;
    PlayerConfig cfg_;
    std::vector<uint64_t> bitrates_;
    Thread thread_;
    std::atomic<PlayerState> state_{PlayerState::Idle};
    std::atomic<bool> stop_{false};
    std::atomic<double> current_remaining_s_{0.0};
    std::atomic<double> position_{0.0};

    mutable std::mutex stats_mu_;
    PlayerStats stats_;
    int last_variant_ = -1;
    double start_wall_ = 0.0;
};

} // namespace bitglide

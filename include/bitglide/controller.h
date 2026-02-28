#pragma once

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "bitglide/abr.h"
#include "bitglide/bandwidth_estimator.h"
#include "bitglide/config.h"
#include "bitglide/metrics.h"
#include "bitglide/player.h"
#include "bitglide/playlist.h"
#include "bitglide/prefetcher.h"
#include "bitglide/segment_buffer.h"

namespace bitglide {

// Orchestrates a streaming session on the calling thread:
//   playlists -> ABR decision per segment -> prefetch workers -> buffer -> player thread.
class Controller {
public:
    Controller(Config cfg, const std::atomic<bool>* interrupt = nullptr);
    ~Controller();

    // Runs the session to completion. Returns 0 on success, non-zero on fatal error.
    int run();

    const SessionSummary& summary() const { return summary_; }
    const std::string& error() const { return fatal_; }

private:
    using Clock = std::chrono::steady_clock;

    void load_root_playlist();
    const MediaPlaylist& media_for(int variant, bool force_refresh = false);
    std::optional<SegmentInfo> segment_for(int variant, uint64_t sequence);
    int decide(uint64_t sequence);
    void handle_outcomes(std::chrono::milliseconds wait);
    void check_abandon();
    bool should_stop() const;
    void finish(Clock::time_point started);

    Config cfg_;
    const std::atomic<bool>* interrupt_;
    std::unique_ptr<AbrAlgorithm> abr_;
    BandwidthEstimator estimator_;
    SessionMetrics metrics_;
    SessionSummary summary_;

    std::vector<Variant> variants_;
    std::map<int, MediaPlaylist> media_;
    std::map<int, Clock::time_point> media_fetched_at_;

    std::unique_ptr<SegmentBuffer> buffer_;
    std::unique_ptr<Player> player_;
    std::unique_ptr<Prefetcher> prefetcher_;

    int current_ = -1;
    uint64_t next_seq_ = 0;
    std::optional<uint64_t> end_seq_;
    std::map<uint64_t, int> attempts_;
    std::set<uint64_t> abandoned_;
    std::string fatal_;
};

} // namespace bitglide

#pragma once

#include <mutex>
#include <string>
#include <vector>

#include "bitglide/player.h"
#include "bitglide/types.h"

namespace bitglide {

struct SegmentRecord {
    uint64_t sequence = 0;
    int variant_index = 0;
    uint64_t variant_bps = 0;
    uint64_t bytes = 0;
    double download_ms = 0.0;
    double ttfb_ms = 0.0;
    double throughput_bps = 0.0;
    uint64_t estimate_bps = 0;  // estimate when the rendition was chosen
    double buffer_s = 0.0;      // buffer level when the rendition was chosen
    int attempt = 0;
    bool ok = false;
    bool abandoned = false;
    std::string error;
};

struct SessionSummary {
    std::string abr;
    std::string url;
    std::vector<Variant> variants;
    PlayerStats player;
    double wall_time_s = 0.0;
    uint64_t total_bytes = 0;
    int failures = 0;
    int abandons = 0;
    double avg_bitrate_bps = 0.0;
    double qoe = 0.0;
};

// Collects per-segment records from the controller thread and renders the session report.
class SessionMetrics {
public:
    void add(const SegmentRecord& rec);
    std::vector<SegmentRecord> records() const;

    // Builds the summary, including a simple linear QoE score
    // (Yin et al.): sum(bitrate_Mbps) - lambda*sum|switch delta| - mu*stall_s, normalised per segment.
    SessionSummary summarize(const std::string& abr, const std::string& url, const std::vector<Variant>& variants,
                             const PlayerStats& player, double wall_time_s) const;

    static std::string to_json(const SessionSummary& s, const std::vector<SegmentRecord>& records);
    static std::string to_text(const SessionSummary& s);

private:
    mutable std::mutex mu_;
    std::vector<SegmentRecord> records_;
};

} // namespace bitglide

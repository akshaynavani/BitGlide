#pragma once

#include <memory>
#include <string>
#include <vector>

#include "bitglide/types.h"

namespace bitglide {

// Snapshot of everything an ABR policy may look at when picking the next segment's rendition.
struct AbrContext {
    const std::vector<Variant>* variants = nullptr; // sorted ascending
    uint64_t estimate_bps = 0;        // conservative EWMA estimate
    uint64_t harmonic_bps = 0;        // harmonic mean of recent samples
    double buffer_level_s = 0.0;      // seconds of media buffered ahead of playhead
    double max_buffer_s = 30.0;
    double segment_duration_s = 6.0;
    int current_index = -1;           // -1 before the first decision
};

struct AbrTuning {
    double safety_factor = 0.85;        // fraction of estimate we are willing to spend
    double upswitch_factor = 1.15;      // estimate must exceed target bitrate by this much
    double reservoir_s = 6.0;           // BBA: below this always pick lowest
    double cushion_s = 18.0;            // BBA: linear region size
    double panic_buffer_s = 3.0;        // hybrid: below this drop to lowest
    double min_buffer_for_upswitch_s = 8.0;
};

class AbrAlgorithm {
public:
    virtual ~AbrAlgorithm() = default;
    virtual int choose(const AbrContext& ctx) = 0;
    virtual std::string name() const = 0;
};

// Picks the highest rendition whose bitrate fits within safety_factor * bandwidth.
class ThroughputAbr : public AbrAlgorithm {
public:
    explicit ThroughputAbr(AbrTuning t = {}) : t_(t) {}
    int choose(const AbrContext& ctx) override;
    std::string name() const override { return "throughput"; }

private:
    AbrTuning t_;
};

// BBA-0 (Huang et al., SIGCOMM'14): rate map from buffer occupancy only.
class BufferBasedAbr : public AbrAlgorithm {
public:
    explicit BufferBasedAbr(AbrTuning t = {}) : t_(t) {}
    int choose(const AbrContext& ctx) override;
    std::string name() const override { return "buffer"; }

private:
    AbrTuning t_;
};

// Throughput-driven with buffer guards and hysteresis: immediate down-switches,
// one-step up-switches only with a healthy buffer and bandwidth headroom.
class HybridAbr : public AbrAlgorithm {
public:
    explicit HybridAbr(AbrTuning t = {}) : t_(t) {}
    int choose(const AbrContext& ctx) override;
    std::string name() const override { return "hybrid"; }

private:
    AbrTuning t_;
};

// Highest variant index whose effective bitrate <= budget_bps (0 if none fit).
int highest_fitting(const std::vector<Variant>& variants, double budget_bps);

std::unique_ptr<AbrAlgorithm> make_abr(const std::string& name, AbrTuning tuning = {});

} // namespace bitglide

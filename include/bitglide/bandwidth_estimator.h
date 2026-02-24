#pragma once

#include <cstdint>
#include <deque>
#include <mutex>

#include "bitglide/types.h"

namespace bitglide {

struct EstimatorConfig {
    double fast_half_life_s = 3.0;   // reacts quickly to drops
    double slow_half_life_s = 9.0;   // smooths out spikes
    size_t window_size = 8;          // samples kept for harmonic mean
    uint64_t min_sample_bytes = 16 * 1024; // ignore tiny downloads (latency dominated)
    double min_sample_ms = 5.0;
    uint64_t default_estimate_bps = 1'000'000; // used before any sample
};

// Exponentially weighted moving average weighted by download time, with
// zero-bias correction (as in shaka-player / hls.js).
class Ewma {
public:
    explicit Ewma(double half_life_s);
    void add(double weight_s, double value);
    double estimate() const;
    double total_weight() const { return total_weight_; }

private:
    double alpha_;
    double estimate_ = 0.0;
    double total_weight_ = 0.0;
};

// Thread-safe bandwidth estimator. Prefetch workers call add_sample() concurrently;
// the controller reads estimate_bps().
class BandwidthEstimator {
public:
    explicit BandwidthEstimator(EstimatorConfig cfg = {});

    // Returns false if the sample was discarded as too small.
    bool add_sample(const DownloadSample& sample);

    // Conservative estimate: min(fast EWMA, slow EWMA). Falls back to default before samples.
    uint64_t estimate_bps() const;
    // Harmonic mean of the most recent window — robust to outliers.
    uint64_t harmonic_mean_bps() const;
    // Most recent single-sample throughput.
    uint64_t last_sample_bps() const;
    size_t sample_count() const;
    void reset();

private:
    EstimatorConfig cfg_;
    mutable std::mutex mu_;
    Ewma fast_;
    Ewma slow_;
    std::deque<double> window_bps_;
    double last_bps_ = 0.0;
    size_t count_ = 0;
};

} // namespace bitglide

#include "bitglide/bandwidth_estimator.h"

#include <algorithm>
#include <cmath>

namespace bitglide {

Ewma::Ewma(double half_life_s) : alpha_(std::exp(std::log(0.5) / half_life_s)) {}

void Ewma::add(double weight_s, double value) {
    const double adj_alpha = std::pow(alpha_, weight_s);
    estimate_ = value * (1.0 - adj_alpha) + adj_alpha * estimate_;
    total_weight_ += weight_s;
}

double Ewma::estimate() const {
    if (total_weight_ <= 0.0) return 0.0;
    const double zero_factor = 1.0 - std::pow(alpha_, total_weight_);
    return estimate_ / zero_factor;
}

BandwidthEstimator::BandwidthEstimator(EstimatorConfig cfg)
    : cfg_(cfg), fast_(cfg.fast_half_life_s), slow_(cfg.slow_half_life_s) {}

bool BandwidthEstimator::add_sample(const DownloadSample& sample) {
    if (sample.bytes < cfg_.min_sample_bytes) return false;
    const double ms = std::max(sample.duration_ms, cfg_.min_sample_ms);
    const double bps = static_cast<double>(sample.bytes) * 8.0 * 1000.0 / ms;
    const double weight_s = ms / 1000.0;

    std::lock_guard<std::mutex> lock(mu_);
    fast_.add(weight_s, bps);
    slow_.add(weight_s, bps);
    window_bps_.push_back(bps);
    while (window_bps_.size() > cfg_.window_size) window_bps_.pop_front();
    last_bps_ = bps;
    ++count_;
    return true;
}

uint64_t BandwidthEstimator::estimate_bps() const {
    std::lock_guard<std::mutex> lock(mu_);
    if (count_ == 0) return cfg_.default_estimate_bps;
    return static_cast<uint64_t>(std::min(fast_.estimate(), slow_.estimate()));
}

uint64_t BandwidthEstimator::harmonic_mean_bps() const {
    std::lock_guard<std::mutex> lock(mu_);
    if (window_bps_.empty()) return cfg_.default_estimate_bps;
    double inv = 0.0;
    for (double b : window_bps_) inv += 1.0 / std::max(b, 1.0);
    return static_cast<uint64_t>(static_cast<double>(window_bps_.size()) / inv);
}

uint64_t BandwidthEstimator::last_sample_bps() const {
    std::lock_guard<std::mutex> lock(mu_);
    return static_cast<uint64_t>(last_bps_);
}

size_t BandwidthEstimator::sample_count() const {
    std::lock_guard<std::mutex> lock(mu_);
    return count_;
}

void BandwidthEstimator::reset() {
    std::lock_guard<std::mutex> lock(mu_);
    fast_ = Ewma(cfg_.fast_half_life_s);
    slow_ = Ewma(cfg_.slow_half_life_s);
    window_bps_.clear();
    last_bps_ = 0.0;
    count_ = 0;
}

} // namespace bitglide

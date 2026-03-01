#include <thread>
#include <vector>

#include "bitglide/bandwidth_estimator.h"
#include "test_framework.h"

using namespace bitglide;

TEST(estimator_returns_default_before_samples) {
    EstimatorConfig cfg;
    cfg.default_estimate_bps = 750000;
    BandwidthEstimator est(cfg);
    EXPECT_EQ(est.estimate_bps(), uint64_t(750000));
    EXPECT_EQ(est.sample_count(), size_t(0));
}

TEST(estimator_converges_on_constant_throughput) {
    BandwidthEstimator est;
    for (int i = 0; i < 10; ++i) est.add_sample({500000, 1000.0, 50.0}); // 4 Mbps
    EXPECT_NEAR(static_cast<double>(est.estimate_bps()), 4e6, 1e3);
    EXPECT_NEAR(static_cast<double>(est.harmonic_mean_bps()), 4e6, 1e3);
    EXPECT_NEAR(static_cast<double>(est.last_sample_bps()), 4e6, 1.0);
}

TEST(estimator_discards_tiny_samples) {
    BandwidthEstimator est;
    EXPECT_FALSE(est.add_sample({1000, 1.0, 0.5}));
    EXPECT_EQ(est.sample_count(), size_t(0));
}

TEST(estimator_reacts_to_drops_faster_than_spikes) {
    BandwidthEstimator est;
    for (int i = 0; i < 10; ++i) est.add_sample({1000000, 1000.0, 0}); // 8 Mbps
    est.add_sample({125000, 1000.0, 0});                                // 1 Mbps drop
    const auto after_drop = est.estimate_bps();
    EXPECT_TRUE(after_drop < 7000000);

    BandwidthEstimator est2;
    for (int i = 0; i < 10; ++i) est2.add_sample({125000, 1000.0, 0}); // 1 Mbps
    est2.add_sample({1000000, 1000.0, 0});                              // 8 Mbps spike
    // min(fast, slow) means a single spike moves the estimate less than the drop did.
    EXPECT_TRUE(est2.estimate_bps() - 1000000 < 8000000 - after_drop);
}

TEST(estimator_harmonic_mean_is_outlier_robust) {
    EstimatorConfig cfg;
    cfg.window_size = 2;
    BandwidthEstimator est(cfg);
    est.add_sample({125000, 1000.0, 0}); // 1 Mbps
    est.add_sample({500000, 1000.0, 0}); // 4 Mbps
    EXPECT_NEAR(static_cast<double>(est.harmonic_mean_bps()), 1.6e6, 1e3);
}

TEST(estimator_is_thread_safe) {
    BandwidthEstimator est;
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
        threads.emplace_back([&] {
            for (int i = 0; i < 1000; ++i) est.add_sample({250000, 500.0, 0});
        });
    for (auto& t : threads) t.join();
    EXPECT_EQ(est.sample_count(), size_t(4000));
    EXPECT_NEAR(static_cast<double>(est.estimate_bps()), 4e6, 1e3);
}

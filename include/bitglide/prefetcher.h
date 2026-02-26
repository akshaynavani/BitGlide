#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "bitglide/bandwidth_estimator.h"
#include "bitglide/http_client.h"
#include "bitglide/segment_buffer.h"
#include "bitglide/thread.h"
#include "bitglide/types.h"

namespace bitglide {

struct FetchJob {
    SegmentInfo info;
    int variant_index = 0;
    uint64_t variant_bps = 0;
    int attempt = 0;
    // ABR inputs at decision time, kept for the session report.
    uint64_t estimate_bps = 0;
    double buffer_s = 0.0;
};

struct FetchOutcome {
    FetchJob job;
    bool ok = false;
    bool cancelled = false;
    std::string error;
    uint64_t bytes = 0;
    double download_ms = 0.0;
    double ttfb_ms = 0.0;
};

struct ActiveDownload {
    uint64_t sequence = 0;
    int variant_index = 0;
    uint64_t variant_bps = 0;
    double duration_s = 0.0;
    double elapsed_ms = 0.0;
};

// Pool of POSIX worker threads that download segments ahead of the playhead.
// The controller decides *what* to fetch (sequence + rendition) and how far ahead;
// workers feed throughput samples to the estimator and completed segments to the buffer.
class Prefetcher {
public:
    Prefetcher(int workers, SegmentBuffer& buffer, BandwidthEstimator& estimator, FetchOptions base_opts);
    ~Prefetcher();

    void start();
    void stop();

    // urgent jobs (retries, abandon replacements) jump the queue.
    void submit(FetchJob job, bool urgent = false);

    // Queued + downloading jobs.
    size_t pending() const;
    // Media seconds represented by queued + downloading jobs.
    double pending_duration_s() const;
    bool is_pending(uint64_t sequence) const;

    std::vector<ActiveDownload> active() const;
    bool cancel(uint64_t sequence);

    // Blocks up to `timeout` for at least one outcome, then returns all available.
    std::vector<FetchOutcome> take_outcomes(std::chrono::milliseconds timeout);

private:
    struct Active {
        FetchJob job;
        std::chrono::steady_clock::time_point started;
        std::shared_ptr<std::atomic<bool>> cancel;
    };

    void worker_loop();
    FetchOutcome download(const FetchJob& job, const std::atomic<bool>& cancel);

    int worker_count_;
    SegmentBuffer& buffer_;
    BandwidthEstimator& estimator_;
    FetchOptions base_opts_;
    std::vector<Thread> threads_;

    mutable std::mutex mu_;
    std::condition_variable job_cv_;
    std::condition_variable outcome_cv_;
    std::deque<FetchJob> queue_;
    std::map<uint64_t, Active> active_; // keyed by sequence
    std::vector<FetchOutcome> outcomes_;
    bool stopping_ = false;
};

} // namespace bitglide

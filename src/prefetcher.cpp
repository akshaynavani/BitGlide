#include "bitglide/prefetcher.h"

#include "bitglide/log.h"

namespace bitglide {

Prefetcher::Prefetcher(int workers, SegmentBuffer& buffer, BandwidthEstimator& estimator, FetchOptions base_opts)
    : worker_count_(workers), buffer_(buffer), estimator_(estimator), base_opts_(base_opts) {
    base_opts_.keep_body = false; // we only need byte counts and timing
}

Prefetcher::~Prefetcher() {
    stop();
}

void Prefetcher::start() {
    threads_.reserve(static_cast<size_t>(worker_count_));
    for (int i = 0; i < worker_count_; ++i) {
        threads_.emplace_back("bg-fetch-" + std::to_string(i), [this] { worker_loop(); });
    }
}

void Prefetcher::stop() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (stopping_ && threads_.empty()) return;
        stopping_ = true;
        queue_.clear();
        for (auto& kv : active_) kv.second.cancel->store(true);
    }
    job_cv_.notify_all();
    outcome_cv_.notify_all();
    for (auto& t : threads_) t.join();
    threads_.clear();
}

void Prefetcher::submit(FetchJob job, bool urgent) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (stopping_) return;
        BG_DEBUG("queue seq=", job.info.sequence, " variant=", job.variant_index, urgent ? " (urgent)" : "");
        if (urgent) queue_.push_front(std::move(job));
        else queue_.push_back(std::move(job));
    }
    job_cv_.notify_one();
}

size_t Prefetcher::pending() const {
    std::lock_guard<std::mutex> lock(mu_);
    return queue_.size() + active_.size();
}

double Prefetcher::pending_duration_s() const {
    std::lock_guard<std::mutex> lock(mu_);
    double total = 0.0;
    for (const auto& j : queue_) total += j.info.duration_s;
    for (const auto& kv : active_) total += kv.second.job.info.duration_s;
    return total;
}

bool Prefetcher::is_pending(uint64_t sequence) const {
    std::lock_guard<std::mutex> lock(mu_);
    if (active_.count(sequence)) return true;
    for (const auto& j : queue_)
        if (j.info.sequence == sequence) return true;
    return false;
}

std::vector<ActiveDownload> Prefetcher::active() const {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<ActiveDownload> out;
    const auto now = std::chrono::steady_clock::now();
    for (const auto& kv : active_) {
        const auto& a = kv.second;
        out.push_back({a.job.info.sequence, a.job.variant_index, a.job.variant_bps, a.job.info.duration_s,
                       std::chrono::duration<double, std::milli>(now - a.started).count()});
    }
    return out;
}

bool Prefetcher::cancel(uint64_t sequence) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = active_.find(sequence);
    if (it == active_.end()) return false;
    it->second.cancel->store(true);
    return true;
}

std::vector<FetchOutcome> Prefetcher::take_outcomes(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mu_);
    outcome_cv_.wait_for(lock, timeout, [&] { return stopping_ || !outcomes_.empty(); });
    std::vector<FetchOutcome> out;
    out.swap(outcomes_);
    return out;
}

void Prefetcher::worker_loop() {
    for (;;) {
        FetchJob job;
        std::shared_ptr<std::atomic<bool>> cancel = std::make_shared<std::atomic<bool>>(false);
        {
            std::unique_lock<std::mutex> lock(mu_);
            job_cv_.wait(lock, [&] { return stopping_ || !queue_.empty(); });
            if (stopping_) return;
            job = std::move(queue_.front());
            queue_.pop_front();
            // A replacement job for the same sequence supersedes any older in-flight one.
            if (auto it = active_.find(job.info.sequence); it != active_.end()) it->second.cancel->store(true);
            active_[job.info.sequence] = Active{job, std::chrono::steady_clock::now(), cancel};
        }

        FetchOutcome outcome = download(job, *cancel);

        {
            std::lock_guard<std::mutex> lock(mu_);
            auto it = active_.find(job.info.sequence);
            if (it != active_.end() && it->second.cancel == cancel) active_.erase(it);
            if (!stopping_) outcomes_.push_back(std::move(outcome));
        }
        outcome_cv_.notify_all();
    }
}

FetchOutcome Prefetcher::download(const FetchJob& job, const std::atomic<bool>& cancel) {
    FetchOptions opts = base_opts_;
    opts.cancel = &cancel;
    opts.range_offset = job.info.byterange_offset;
    opts.range_length = job.info.byterange_length;

    FetchResult r = fetch(job.info.uri, opts);

    FetchOutcome out;
    out.job = job;
    out.bytes = r.bytes;
    out.download_ms = r.total_ms;
    out.ttfb_ms = r.ttfb_ms;
    out.cancelled = cancel.load();
    out.ok = r.ok && !out.cancelled;
    out.error = out.cancelled ? "cancelled" : r.error;

    if (out.ok) {
        estimator_.add_sample(DownloadSample{r.bytes, r.total_ms, r.ttfb_ms});
        Segment seg;
        seg.sequence = job.info.sequence;
        seg.variant_index = job.variant_index;
        seg.duration_s = job.info.duration_s;
        seg.size_bytes = r.bytes;
        seg.download_ms = r.total_ms;
        buffer_.push(seg);
        BG_DEBUG("fetched seq=", seg.sequence, " v", seg.variant_index, " ", r.bytes, "B ", r.total_ms, "ms");
    } else {
        BG_WARN("segment seq=", job.info.sequence, " v", job.variant_index, " failed: ", out.error);
    }
    return out;
}

} // namespace bitglide

#include "bitglide/controller.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include "bitglide/http_client.h"
#include "bitglide/log.h"

namespace bitglide {

namespace {

std::string fetch_text(const std::string& url, int timeout_ms) {
    FetchOptions opts;
    opts.timeout_ms = timeout_ms;
    FetchResult r = fetch(url, opts);
    if (!r.ok) throw std::runtime_error("failed to fetch " + url + ": " + r.error);
    return r.body;
}

} // namespace

Controller::Controller(Config cfg, const std::atomic<bool>* interrupt)
    : cfg_(std::move(cfg)), interrupt_(interrupt), abr_(make_abr(cfg_.abr, cfg_.tuning)), estimator_(cfg_.estimator) {}

Controller::~Controller() {
    if (prefetcher_) prefetcher_->stop();
    if (player_) player_->stop();
}

bool Controller::should_stop() const {
    return !fatal_.empty() || (interrupt_ && interrupt_->load());
}

void Controller::load_root_playlist() {
    const std::string body = fetch_text(cfg_.url, cfg_.request_timeout_ms);
    if (is_master_playlist(body)) {
        variants_ = parse_master_playlist(body, cfg_.url).variants;
        BG_INFO("master playlist: ", variants_.size(), " variants");
        for (const auto& v : variants_)
            BG_INFO("  [", v.index, "] ", v.effective_bps() / 1000, " kbps ", v.width, "x", v.height, " ", v.uri);
        return;
    }
    // A bare media playlist is treated as a single-rendition stream.
    Variant v;
    v.index = 0;
    v.bandwidth_bps = 1;
    v.uri = cfg_.url;
    variants_.push_back(v);
    media_[0] = parse_media_playlist(body, cfg_.url);
    media_fetched_at_[0] = Clock::now();
    BG_INFO("media playlist without master: single rendition, ", media_[0].segments.size(), " segments");
}

const MediaPlaylist& Controller::media_for(int variant, bool force_refresh) {
    auto it = media_.find(variant);
    if (it != media_.end() && !force_refresh) return it->second;
    const auto& uri = variants_.at(static_cast<size_t>(variant)).uri;
    MediaPlaylist pl = parse_media_playlist(fetch_text(uri, cfg_.request_timeout_ms), uri);
    BG_DEBUG("loaded media playlist v", variant, ": seq ", pl.media_sequence, " + ", pl.segments.size(),
             pl.endlist ? " (VOD)" : " (live)");
    media_fetched_at_[variant] = Clock::now();
    return media_[variant] = std::move(pl);
}

std::optional<SegmentInfo> Controller::segment_for(int variant, uint64_t sequence) {
    const MediaPlaylist* pl = &media_for(variant);
    auto lookup = [&](const MediaPlaylist& p) -> std::optional<SegmentInfo> {
        if (sequence < p.media_sequence) return std::nullopt;
        const uint64_t idx = sequence - p.media_sequence;
        if (idx >= p.segments.size()) return std::nullopt;
        return p.segments[static_cast<size_t>(idx)];
    };
    if (auto s = lookup(*pl)) return s;
    if (pl->endlist) return std::nullopt;

    // Live: refresh at most every live_refresh_s (default half a target duration).
    const double interval = cfg_.live_refresh_s > 0 ? cfg_.live_refresh_s : std::max(0.5, pl->target_duration_s / 2);
    const double age = std::chrono::duration<double>(Clock::now() - media_fetched_at_[variant]).count();
    if (age < interval) return std::nullopt;
    pl = &media_for(variant, true);
    if (sequence < pl->media_sequence)
        BG_WARN("fell behind the live window: seq ", sequence, " < ", pl->media_sequence);
    return lookup(*pl);
}

int Controller::decide(uint64_t sequence) {
    AbrContext ctx;
    ctx.variants = &variants_;
    ctx.estimate_bps = estimator_.estimate_bps();
    ctx.harmonic_bps = estimator_.sample_count() ? estimator_.harmonic_mean_bps() : 0;
    ctx.buffer_level_s = player_ ? player_->buffer_ahead_s() : 0.0;
    ctx.max_buffer_s = cfg_.max_buffer_s;
    ctx.current_index = current_;
    const auto& ref = media_.count(std::max(current_, 0)) ? media_.at(std::max(current_, 0)) : media_.begin()->second;
    ctx.segment_duration_s = ref.target_duration_s > 0 ? ref.target_duration_s : 6.0;
    (void)sequence;

    int choice = std::clamp(abr_->choose(ctx), 0, static_cast<int>(variants_.size()) - 1);
    if (choice != current_) {
        BG_INFO("ABR ", abr_->name(), ": v", current_, " -> v", choice, " (est ", ctx.estimate_bps / 1000,
                " kbps, buffer ", ctx.buffer_level_s, " s)");
    }
    current_ = choice;
    return choice;
}

void Controller::handle_outcomes(std::chrono::milliseconds wait) {
    for (auto& out : prefetcher_->take_outcomes(wait)) {
        const uint64_t seq = out.job.info.sequence;
        SegmentRecord rec;
        rec.sequence = seq;
        rec.variant_index = out.job.variant_index;
        rec.variant_bps = out.job.variant_bps;
        rec.bytes = out.bytes;
        rec.download_ms = out.download_ms;
        rec.ttfb_ms = out.ttfb_ms;
        rec.throughput_bps = out.download_ms > 0 ? static_cast<double>(out.bytes) * 8000.0 / out.download_ms : 0.0;
        rec.estimate_bps = out.job.estimate_bps;
        rec.buffer_s = out.job.buffer_s;
        rec.attempt = out.job.attempt;
        rec.ok = out.ok;
        rec.error = out.error;

        if (out.ok) {
            metrics_.add(rec);
            continue;
        }
        if (out.cancelled) {
            if (abandoned_.count(seq) && out.job.variant_index != 0) {
                rec.abandoned = true;
                metrics_.add(rec);
            }
            continue; // replacement was already queued (or we are shutting down)
        }
        metrics_.add(rec);
        int& attempts = attempts_[seq];
        ++attempts;
        if (attempts > cfg_.max_retries) {
            fatal_ = "segment " + std::to_string(seq) + " failed after " + std::to_string(attempts) +
                     " attempts: " + out.error;
            BG_ERROR(fatal_);
            return;
        }
        // First retry keeps the rendition; later ones fall back to the lowest.
        const int fallback = attempts == 1 ? out.job.variant_index : 0;
        auto info = segment_for(fallback, seq);
        if (!info) info = out.job.info;
        FetchJob job = out.job;
        job.info = *info;
        job.variant_index = fallback;
        job.variant_bps = variants_[static_cast<size_t>(fallback)].effective_bps();
        job.attempt = attempts;
        BG_WARN("retrying seq ", seq, " on v", fallback, " (attempt ", attempts, ")");
        prefetcher_->submit(job, /*urgent=*/true);
    }
}

// Abandons the download blocking playback when it is already slower than the remaining buffer,
// and re-requests the same segment from the lowest rendition.
void Controller::check_abandon() {
    const uint64_t needed = buffer_->next_sequence();
    if (buffer_->has(needed)) return;
    const double buffer_ahead = player_->buffer_ahead_s();
    for (const auto& a : prefetcher_->active()) {
        if (a.sequence != needed || a.variant_index == 0 || abandoned_.count(needed)) continue;
        const double elapsed_s = a.elapsed_ms / 1000.0;
        const bool stalling = elapsed_s >= buffer_ahead;
        const bool slow = elapsed_s > 0.5 * a.duration_s;
        if (!stalling || !slow) continue;

        auto info = segment_for(0, needed);
        if (!info) return;
        BG_WARN("abandoning seq ", needed, " v", a.variant_index, " after ", elapsed_s, " s (buffer ",
                buffer_ahead, " s); refetching at v0");
        abandoned_.insert(needed);
        prefetcher_->cancel(needed);
        FetchJob job;
        job.info = *info;
        job.variant_index = 0;
        job.variant_bps = variants_[0].effective_bps();
        job.estimate_bps = estimator_.estimate_bps();
        job.buffer_s = buffer_ahead;
        prefetcher_->submit(job, /*urgent=*/true);
        current_ = 0; // restart ABR ramp-up from the bottom
        return;
    }
}

int Controller::run() {
    set_log_level(cfg_.log_level);
    const auto started = Clock::now();
    try {
        load_root_playlist();

        const int first_variant = decide(0);
        const MediaPlaylist& pl = media_for(first_variant);
        if (pl.segments.empty()) throw std::runtime_error("media playlist has no segments");

        uint64_t first_seq = pl.segments.front().sequence;
        if (!pl.endlist && pl.segments.size() > 3) first_seq = pl.segments[pl.segments.size() - 3].sequence; // live edge
        if (pl.endlist) end_seq_ = pl.segments.back().sequence;
        if (cfg_.max_segments > 0) {
            const uint64_t cap = first_seq + static_cast<uint64_t>(cfg_.max_segments) - 1;
            end_seq_ = end_seq_ ? std::min(*end_seq_, cap) : cap;
        }
        next_seq_ = first_seq;

        buffer_ = std::make_unique<SegmentBuffer>(first_seq);
        if (end_seq_) buffer_->set_end_sequence(*end_seq_);

        std::vector<uint64_t> bitrates;
        for (const auto& v : variants_) bitrates.push_back(v.effective_bps());
        player_ = std::make_unique<Player>(*buffer_, cfg_.player, bitrates);

        FetchOptions fopts;
        fopts.timeout_ms = cfg_.request_timeout_ms;
        prefetcher_ = std::make_unique<Prefetcher>(cfg_.workers, *buffer_, estimator_, fopts);
        prefetcher_->start();
        player_->start();
        BG_INFO("session started at seq ", first_seq, end_seq_ ? " end " + std::to_string(*end_seq_) : " (live)",
                ", lookahead ", cfg_.lookahead_segments, ", workers ", cfg_.workers);

        bool first_decision_used = false;
        while (!should_stop()) {
            const auto ps = player_->state();
            if (ps == PlayerState::Ended || ps == PlayerState::Stopped) break;

            handle_outcomes(std::chrono::milliseconds(20));
            if (should_stop()) break;
            if (cfg_.abandon_slow) check_abandon();

            if (end_seq_ && next_seq_ > *end_seq_) continue; // everything scheduled; wait for player
            if (prefetcher_->pending() >= static_cast<size_t>(cfg_.lookahead_segments)) continue;
            if (player_->buffer_ahead_s() + prefetcher_->pending_duration_s() >= cfg_.max_buffer_s) continue;

            const int v = first_decision_used ? decide(next_seq_) : first_variant;
            first_decision_used = true;
            auto info = segment_for(v, next_seq_);
            if (!info) {
                if (media_for(v).endlist && !end_seq_) {
                    end_seq_ = next_seq_ - 1;
                    buffer_->set_end_sequence(*end_seq_);
                }
                continue; // live stream: wait for the playlist to grow
            }

            FetchJob job;
            job.info = *info;
            job.variant_index = v;
            job.variant_bps = variants_[static_cast<size_t>(v)].effective_bps();
            job.estimate_bps = estimator_.estimate_bps();
            job.buffer_s = player_->buffer_ahead_s();
            prefetcher_->submit(job);
            ++next_seq_;
        }
    } catch (const std::exception& e) {
        fatal_ = e.what();
        BG_ERROR("session aborted: ", fatal_);
    }

    if (prefetcher_) prefetcher_->stop();
    if (buffer_) buffer_->close();
    if (player_) player_->stop();
    finish(started);
    return fatal_.empty() ? 0 : 1;
}

void Controller::finish(Clock::time_point started) {
    const double wall = std::chrono::duration<double>(Clock::now() - started).count();
    PlayerStats ps = player_ ? player_->stats() : PlayerStats{};
    summary_ = metrics_.summarize(cfg_.abr, cfg_.url, variants_, ps, wall);
    std::cout << SessionMetrics::to_text(summary_);
    if (!cfg_.report_path.empty()) {
        std::ofstream out(cfg_.report_path);
        if (!out) {
            BG_ERROR("cannot write report to ", cfg_.report_path);
            return;
        }
        out << SessionMetrics::to_json(summary_, metrics_.records());
        BG_INFO("report written to ", cfg_.report_path);
    }
}

} // namespace bitglide

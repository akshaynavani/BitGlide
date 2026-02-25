#include "bitglide/abr.h"

#include <algorithm>
#include <stdexcept>

namespace bitglide {

int highest_fitting(const std::vector<Variant>& variants, double budget_bps) {
    int best = 0;
    for (const auto& v : variants) {
        if (static_cast<double>(v.effective_bps()) <= budget_bps) best = v.index;
    }
    return best;
}

int ThroughputAbr::choose(const AbrContext& ctx) {
    if (!ctx.variants || ctx.variants->empty()) return 0;
    const double bw = static_cast<double>(std::min(ctx.estimate_bps, ctx.harmonic_bps ? ctx.harmonic_bps : ctx.estimate_bps));
    return highest_fitting(*ctx.variants, bw * t_.safety_factor);
}

int BufferBasedAbr::choose(const AbrContext& ctx) {
    if (!ctx.variants || ctx.variants->empty()) return 0;
    const auto& vs = *ctx.variants;
    const int top = static_cast<int>(vs.size()) - 1;
    const double buf = ctx.buffer_level_s;

    // Startup: with an empty buffer BBA would always pick the lowest; that is the intended behaviour.
    if (buf <= t_.reservoir_s) return 0;
    if (buf >= t_.reservoir_s + t_.cushion_s) return top;

    const double r_min = static_cast<double>(vs.front().effective_bps());
    const double r_max = static_cast<double>(vs.back().effective_bps());
    const double frac = (buf - t_.reservoir_s) / t_.cushion_s;
    const double target = r_min + frac * (r_max - r_min);

    int choice = highest_fitting(vs, target);

    // BBA hysteresis: stay at current rate while the map lies between its neighbours.
    if (ctx.current_index >= 0 && ctx.current_index <= top) {
        const int cur = ctx.current_index;
        const double up = cur < top ? static_cast<double>(vs[cur + 1].effective_bps()) : r_max;
        const double down = cur > 0 ? static_cast<double>(vs[cur - 1].effective_bps()) : r_min;
        if (target < up && target > down) choice = cur;
    }
    return choice;
}

int HybridAbr::choose(const AbrContext& ctx) {
    if (!ctx.variants || ctx.variants->empty()) return 0;
    const auto& vs = *ctx.variants;
    const int top = static_cast<int>(vs.size()) - 1;

    const double bw = static_cast<double>(ctx.harmonic_bps ? std::min(ctx.estimate_bps, ctx.harmonic_bps)
                                                           : ctx.estimate_bps);
    const int sustainable = highest_fitting(vs, bw * t_.safety_factor);

    // First decision: just use bandwidth.
    if (ctx.current_index < 0) return sustainable;
    const int cur = std::min(ctx.current_index, top);

    // Buffer is about to run dry: protect against a stall at any cost.
    if (ctx.buffer_level_s < t_.panic_buffer_s) return std::min(sustainable, cur > 0 ? cur - 1 : 0);

    // Down-switch as soon as the current rendition is unsustainable, unless the buffer
    // is deep enough to absorb the deficit for the next segment.
    if (sustainable < cur) {
        const double cur_bps = static_cast<double>(vs[cur].effective_bps());
        const double download_s = cur_bps * ctx.segment_duration_s / std::max(bw, 1.0);
        const bool buffer_absorbs = ctx.buffer_level_s - download_s > t_.min_buffer_for_upswitch_s;
        return buffer_absorbs ? cur : sustainable;
    }

    // Up-switch one step at a time with headroom and a healthy buffer.
    if (sustainable > cur && cur < top) {
        const double next_bps = static_cast<double>(vs[cur + 1].effective_bps());
        if (ctx.buffer_level_s >= t_.min_buffer_for_upswitch_s && bw * t_.safety_factor >= next_bps * t_.upswitch_factor)
            return cur + 1;
    }
    return cur;
}

std::unique_ptr<AbrAlgorithm> make_abr(const std::string& name, AbrTuning tuning) {
    if (name == "throughput") return std::make_unique<ThroughputAbr>(tuning);
    if (name == "buffer") return std::make_unique<BufferBasedAbr>(tuning);
    if (name == "hybrid") return std::make_unique<HybridAbr>(tuning);
    throw std::invalid_argument("unknown ABR algorithm: " + name);
}

} // namespace bitglide

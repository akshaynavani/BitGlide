#include "bitglide/metrics.h"

#include <cmath>
#include <cstdio>
#include <sstream>

namespace bitglide {

namespace {

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out += c;
            }
        }
    }
    return out;
}

std::string num(double v) {
    if (!std::isfinite(v)) return "0";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.3f", v);
    return buf;
}

constexpr double kSwitchPenalty = 1.0; // per Mbps of quality change
constexpr double kStallPenalty = 4.3;  // per second of rebuffering (Mbps-equivalent)

} // namespace

void SessionMetrics::add(const SegmentRecord& rec) {
    std::lock_guard<std::mutex> lock(mu_);
    records_.push_back(rec);
}

std::vector<SegmentRecord> SessionMetrics::records() const {
    std::lock_guard<std::mutex> lock(mu_);
    return records_;
}

SessionSummary SessionMetrics::summarize(const std::string& abr, const std::string& url,
                                         const std::vector<Variant>& variants, const PlayerStats& player,
                                         double wall_time_s) const {
    SessionSummary s;
    s.abr = abr;
    s.url = url;
    s.variants = variants;
    s.player = player;
    s.wall_time_s = wall_time_s;

    std::lock_guard<std::mutex> lock(mu_);
    double quality = 0.0, smooth = 0.0;
    int prev = -1;
    int delivered = 0;
    for (const auto& r : records_) {
        s.total_bytes += r.bytes;
        if (r.abandoned) ++s.abandons;
        if (!r.ok) {
            if (!r.abandoned) ++s.failures;
            continue;
        }
        ++delivered;
        const double mbps = static_cast<double>(r.variant_bps) / 1e6;
        quality += mbps;
        if (prev >= 0) {
            const double prev_mbps = static_cast<double>(variants[static_cast<size_t>(prev)].effective_bps()) / 1e6;
            smooth += std::fabs(mbps - prev_mbps);
        }
        prev = r.variant_index;
    }
    s.avg_bitrate_bps = player.played_s > 0 ? player.weighted_bitrate_sum / player.played_s : 0.0;
    if (delivered > 0)
        s.qoe = (quality - kSwitchPenalty * smooth - kStallPenalty * player.stall_time_s) / delivered;
    return s;
}

std::string SessionMetrics::to_json(const SessionSummary& s, const std::vector<SegmentRecord>& records) {
    std::ostringstream o;
    o << "{\n";
    o << "  \"abr\": \"" << json_escape(s.abr) << "\",\n";
    o << "  \"url\": \"" << json_escape(s.url) << "\",\n";
    o << "  \"wall_time_s\": " << num(s.wall_time_s) << ",\n";
    o << "  \"startup_delay_s\": " << num(s.player.startup_delay_s) << ",\n";
    o << "  \"played_s\": " << num(s.player.played_s) << ",\n";
    o << "  \"stall_count\": " << s.player.stall_count << ",\n";
    o << "  \"stall_time_s\": " << num(s.player.stall_time_s) << ",\n";
    o << "  \"switch_count\": " << s.player.switch_count << ",\n";
    o << "  \"avg_bitrate_bps\": " << num(s.avg_bitrate_bps) << ",\n";
    o << "  \"total_bytes\": " << s.total_bytes << ",\n";
    o << "  \"failures\": " << s.failures << ",\n";
    o << "  \"abandons\": " << s.abandons << ",\n";
    o << "  \"qoe\": " << num(s.qoe) << ",\n";

    o << "  \"variants\": [";
    for (size_t i = 0; i < s.variants.size(); ++i) {
        const auto& v = s.variants[i];
        o << (i ? ", " : "") << "{\"index\": " << v.index << ", \"bandwidth\": " << v.effective_bps()
          << ", \"resolution\": \"" << v.width << "x" << v.height << "\"}";
    }
    o << "],\n";

    o << "  \"events\": [\n";
    for (size_t i = 0; i < s.player.events.size(); ++i) {
        const auto& e = s.player.events[i];
        o << "    {\"t\": " << num(e.wall_s) << ", \"pos\": " << num(e.position_s) << ", \"type\": \""
          << json_escape(e.type) << "\", \"variant\": " << e.variant_index << ", \"value\": " << num(e.value) << "}"
          << (i + 1 < s.player.events.size() ? "," : "") << "\n";
    }
    o << "  ],\n";

    o << "  \"segments\": [\n";
    for (size_t i = 0; i < records.size(); ++i) {
        const auto& r = records[i];
        o << "    {\"seq\": " << r.sequence << ", \"variant\": " << r.variant_index << ", \"variant_bps\": "
          << r.variant_bps << ", \"bytes\": " << r.bytes << ", \"download_ms\": " << num(r.download_ms)
          << ", \"ttfb_ms\": " << num(r.ttfb_ms) << ", \"throughput_bps\": " << num(r.throughput_bps)
          << ", \"estimate_bps\": " << r.estimate_bps << ", \"buffer_s\": " << num(r.buffer_s)
          << ", \"attempt\": " << r.attempt << ", \"ok\": " << (r.ok ? "true" : "false")
          << ", \"abandoned\": " << (r.abandoned ? "true" : "false") << ", \"error\": \"" << json_escape(r.error)
          << "\"}" << (i + 1 < records.size() ? "," : "") << "\n";
    }
    o << "  ]\n}\n";
    return o.str();
}

std::string SessionMetrics::to_text(const SessionSummary& s) {
    std::ostringstream o;
    o << "BitGlide session summary (" << s.abr << ")\n"
      << "  played        " << num(s.player.played_s) << " s in " << num(s.wall_time_s) << " s wall\n"
      << "  startup delay " << num(s.player.startup_delay_s) << " s\n"
      << "  stalls        " << s.player.stall_count << " (" << num(s.player.stall_time_s) << " s)\n"
      << "  switches      " << s.player.switch_count << "\n"
      << "  avg bitrate   " << num(s.avg_bitrate_bps / 1e3) << " kbps\n"
      << "  downloaded    " << num(static_cast<double>(s.total_bytes) / 1e6) << " MB\n"
      << "  failures      " << s.failures << ", abandons " << s.abandons << "\n"
      << "  QoE score     " << num(s.qoe) << "\n";
    return o.str();
}

} // namespace bitglide

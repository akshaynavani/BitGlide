#include "bitglide/config.h"

#include <fstream>
#include <iostream>
#include <stdexcept>

namespace bitglide {

namespace {

double to_d(const std::string& key, const std::string& v) {
    try {
        size_t used = 0;
        double d = std::stod(v, &used);
        if (used != v.size()) throw std::invalid_argument(v);
        return d;
    } catch (...) {
        throw std::invalid_argument("invalid number for " + key + ": " + v);
    }
}

int to_i(const std::string& key, const std::string& v) {
    double d = to_d(key, v);
    if (d != static_cast<int>(d)) throw std::invalid_argument("expected integer for " + key + ": " + v);
    return static_cast<int>(d);
}

bool to_b(const std::string& key, const std::string& v) {
    if (v == "1" || v == "true" || v == "yes" || v == "on") return true;
    if (v == "0" || v == "false" || v == "no" || v == "off") return false;
    throw std::invalid_argument("invalid boolean for " + key + ": " + v);
}

std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

} // namespace

void apply_setting(Config& c, const std::string& key, const std::string& v) {
    if (key == "url") c.url = v;
    else if (key == "abr") c.abr = v;
    else if (key == "workers") c.workers = to_i(key, v);
    else if (key == "lookahead") c.lookahead_segments = to_i(key, v);
    else if (key == "max-buffer") c.max_buffer_s = to_d(key, v);
    else if (key == "max-retries") c.max_retries = to_i(key, v);
    else if (key == "timeout-ms") c.request_timeout_ms = to_i(key, v);
    else if (key == "abandon-slow") c.abandon_slow = to_b(key, v);
    else if (key == "max-segments") c.max_segments = to_i(key, v);
    else if (key == "live-refresh") c.live_refresh_s = to_d(key, v);
    else if (key == "report") c.report_path = v;
    else if (key == "log-level") c.log_level = parse_log_level(v);
    // ABR tuning
    else if (key == "safety-factor") c.tuning.safety_factor = to_d(key, v);
    else if (key == "upswitch-factor") c.tuning.upswitch_factor = to_d(key, v);
    else if (key == "reservoir") c.tuning.reservoir_s = to_d(key, v);
    else if (key == "cushion") c.tuning.cushion_s = to_d(key, v);
    else if (key == "panic-buffer") c.tuning.panic_buffer_s = to_d(key, v);
    else if (key == "upswitch-buffer") c.tuning.min_buffer_for_upswitch_s = to_d(key, v);
    // Estimator
    else if (key == "ewma-fast") c.estimator.fast_half_life_s = to_d(key, v);
    else if (key == "ewma-slow") c.estimator.slow_half_life_s = to_d(key, v);
    else if (key == "bw-window") c.estimator.window_size = static_cast<size_t>(to_i(key, v));
    else if (key == "initial-bw") c.estimator.default_estimate_bps = static_cast<uint64_t>(to_d(key, v));
    // Player
    else if (key == "startup-buffer") c.player.startup_buffer_s = to_d(key, v);
    else if (key == "resume-buffer") c.player.resume_buffer_s = to_d(key, v);
    else if (key == "playback-rate") c.player.playback_rate = to_d(key, v);
    else throw std::invalid_argument("unknown setting: " + key);
}

void load_config_file(Config& cfg, const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::invalid_argument("cannot open config file: " + path);
    std::string line;
    int lineno = 0;
    while (std::getline(in, line)) {
        ++lineno;
        auto hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        line = trim(line);
        if (line.empty()) continue;
        auto eq = line.find('=');
        if (eq == std::string::npos)
            throw std::invalid_argument(path + ":" + std::to_string(lineno) + ": expected key = value");
        apply_setting(cfg, trim(line.substr(0, eq)), trim(line.substr(eq + 1)));
    }
}

bool parse_args(Config& cfg, int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            std::cout << usage();
            return false;
        }
        if (arg.rfind("--", 0) != 0) {
            cfg.url = arg; // positional URL
            continue;
        }
        arg = arg.substr(2);
        std::string key = arg, value;
        auto eq = arg.find('=');
        if (eq != std::string::npos) {
            key = arg.substr(0, eq);
            value = arg.substr(eq + 1);
        } else if (i + 1 < argc) {
            value = argv[++i];
        } else {
            throw std::invalid_argument("missing value for --" + key);
        }
        if (key == "config") load_config_file(cfg, value);
        else apply_setting(cfg, key, value);
    }
    return true;
}

void validate(const Config& c) {
    if (c.url.empty()) throw std::invalid_argument("a playlist URL is required");
    if (c.workers < 1 || c.workers > 32) throw std::invalid_argument("workers must be in [1, 32]");
    if (c.lookahead_segments < 1) throw std::invalid_argument("lookahead must be >= 1");
    if (c.max_buffer_s <= 0) throw std::invalid_argument("max-buffer must be > 0");
    if (c.tuning.safety_factor <= 0 || c.tuning.safety_factor > 1.0)
        throw std::invalid_argument("safety-factor must be in (0, 1]");
    if (c.player.playback_rate <= 0) throw std::invalid_argument("playback-rate must be > 0");
    make_abr(c.abr); // throws on unknown name
}

std::string usage() {
    return R"(Usage: bitglide [options] <playlist-url>

Adaptive bitrate HLS controller. Plays an HLS stream through a simulated player,
switching renditions based on real-time bandwidth and buffer level.

Core:
  --url URL               master or media playlist (http:// or file://)
  --abr NAME              throughput | buffer | hybrid (default hybrid)
  --workers N             prefetch worker threads (default 2)
  --lookahead N           segments prefetched ahead / in flight (default 3)
  --max-buffer S          max seconds buffered ahead (default 30)
  --max-retries N         retries per segment (default 2)
  --timeout-ms MS         socket timeout (default 8000)
  --abandon-slow BOOL     abort downloads that would cause a stall (default true)
  --max-segments N        stop after N segments (default whole stream)
  --report PATH           write JSON session report
  --config PATH           load key = value settings from a file
  --log-level LEVEL       debug | info | warn | error | off

ABR tuning:
  --safety-factor F       fraction of bandwidth to spend (0.85)
  --upswitch-factor F     headroom needed to step up (1.15)
  --reservoir S --cushion S        buffer-based map (6, 18)
  --panic-buffer S --upswitch-buffer S   hybrid guards (3, 8)

Estimator:
  --ewma-fast S --ewma-slow S      EWMA half-lives (3, 9)
  --bw-window N                    harmonic mean window (8)
  --initial-bw BPS                 estimate before first sample (1000000)

Player:
  --startup-buffer S --resume-buffer S   (4, 4)
  --playback-rate F                      media clock speed multiplier (1.0)
)";
}

} // namespace bitglide

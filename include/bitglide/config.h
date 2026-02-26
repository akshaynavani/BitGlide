#pragma once

#include <string>
#include <vector>

#include "bitglide/abr.h"
#include "bitglide/bandwidth_estimator.h"
#include "bitglide/log.h"
#include "bitglide/player.h"

namespace bitglide {

struct Config {
    std::string url;                  // master or media playlist URL (http:// or file://)
    std::string abr = "hybrid";       // throughput | buffer | hybrid
    AbrTuning tuning;
    EstimatorConfig estimator;
    PlayerConfig player;

    int workers = 2;                  // prefetch worker threads
    int lookahead_segments = 3;       // max segments in flight ahead of the buffer
    double max_buffer_s = 30.0;       // stop prefetching beyond this much buffered media
    int max_retries = 2;              // per segment, before falling back to the lowest rendition
    int request_timeout_ms = 8000;
    bool abandon_slow = true;         // cancel a download that will not finish before a stall
    int max_segments = 0;             // 0 = whole stream
    double live_refresh_s = 0.0;      // 0 = target duration

    std::string report_path;          // JSON session report, empty = none
    LogLevel log_level = LogLevel::Info;
};

// Applies "key=value" settings. Keys match the CLI flags without leading dashes.
// Throws std::invalid_argument on unknown keys or malformed values.
void apply_setting(Config& cfg, const std::string& key, const std::string& value);

// Loads a config file of "key = value" lines ('#' comments allowed).
void load_config_file(Config& cfg, const std::string& path);

// Parses argv. Returns false (after printing usage) if --help was given.
bool parse_args(Config& cfg, int argc, char** argv);

void validate(const Config& cfg);
std::string usage();

} // namespace bitglide

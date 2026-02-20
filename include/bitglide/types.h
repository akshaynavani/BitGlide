#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bitglide {

// One entry from an HLS master playlist (#EXT-X-STREAM-INF).
struct Variant {
    int index = 0;                 // position after sorting by bandwidth (0 = lowest)
    uint64_t bandwidth_bps = 0;    // BANDWIDTH attribute (peak)
    uint64_t avg_bandwidth_bps = 0;// AVERAGE-BANDWIDTH attribute, 0 if absent
    int width = 0;
    int height = 0;
    std::string codecs;
    std::string uri;               // absolute URI of the media playlist

    // Bitrate the ABR logic should budget for.
    uint64_t effective_bps() const {
        return avg_bandwidth_bps ? avg_bandwidth_bps : bandwidth_bps;
    }
};

// One segment from a media playlist.
struct SegmentInfo {
    uint64_t sequence = 0;   // media sequence number
    double duration_s = 0.0; // #EXTINF duration
    std::string uri;         // absolute URI
    int64_t byterange_length = -1;
    int64_t byterange_offset = -1;
};

// A segment that has been downloaded and is waiting in the buffer.
struct Segment {
    uint64_t sequence = 0;
    int variant_index = 0;
    double duration_s = 0.0;
    uint64_t size_bytes = 0;
    double download_ms = 0.0;
};

// A single throughput observation fed to the bandwidth estimator.
struct DownloadSample {
    uint64_t bytes = 0;
    double duration_ms = 0.0;
    double ttfb_ms = 0.0;
};

} // namespace bitglide

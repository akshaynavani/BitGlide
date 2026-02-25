#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <string>

namespace bitglide {

struct FetchOptions {
    int64_t range_offset = -1;   // byte range start, -1 for whole resource
    int64_t range_length = -1;
    bool keep_body = true;       // segments can be discarded after counting bytes
    int timeout_ms = 10'000;     // per socket operation
    const std::atomic<bool>* cancel = nullptr; // checked between reads
};

struct FetchResult {
    bool ok = false;
    int status = 0;
    std::string error;
    std::string body;
    uint64_t bytes = 0;          // payload bytes received
    double ttfb_ms = 0.0;        // request sent -> first byte of response
    double total_ms = 0.0;       // request sent -> last byte
    std::map<std::string, std::string> headers; // lower-cased names
};

// Blocking HTTP/1.1 GET over POSIX sockets (one connection per request) plus file:// reads.
// Supports Content-Length, chunked and close-delimited bodies and single byte ranges.
FetchResult fetch(const std::string& url, const FetchOptions& opts = {});

} // namespace bitglide

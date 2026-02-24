#pragma once

#include <optional>
#include <string>

namespace bitglide {

struct Url {
    std::string scheme; // "http" or "file"
    std::string host;
    int port = 0;
    std::string path;   // includes query string, always starts with '/'

    std::string to_string() const;
};

// Parses "http://host[:port]/path?q" and "file:///abs/path". Returns nullopt on failure.
std::optional<Url> parse_url(const std::string& text);

// Resolves `ref` (absolute or relative, as found in an m3u8) against `base`.
std::string resolve_url(const std::string& base, const std::string& ref);

} // namespace bitglide

#pragma once

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "bitglide/types.h"

namespace bitglide {

class PlaylistError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct MasterPlaylist {
    std::vector<Variant> variants; // sorted ascending by effective bitrate
};

struct MediaPlaylist {
    int version = 1;
    double target_duration_s = 0.0;
    uint64_t media_sequence = 0;
    bool endlist = false;           // VOD if true
    std::string playlist_type;      // "VOD", "EVENT" or empty
    std::vector<SegmentInfo> segments;

    double total_duration_s() const;
};

// Parses an attribute list such as `BANDWIDTH=1280000,CODECS="avc1.4d401f,mp4a.40.2"`.
std::map<std::string, std::string> parse_attribute_list(const std::string& text);

bool is_master_playlist(const std::string& body);

// `base_uri` is the URI the playlist was fetched from; relative entries are resolved against it.
MasterPlaylist parse_master_playlist(const std::string& body, const std::string& base_uri);
MediaPlaylist parse_media_playlist(const std::string& body, const std::string& base_uri);

} // namespace bitglide

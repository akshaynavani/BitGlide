#include "bitglide/playlist.h"

#include <algorithm>
#include <sstream>

#include "bitglide/url.h"

namespace bitglide {

namespace {

std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

bool starts_with(const std::string& s, const char* prefix) {
    return s.rfind(prefix, 0) == 0;
}

std::vector<std::string> lines_of(const std::string& body) {
    std::vector<std::string> out;
    std::istringstream in(body);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (!line.empty()) out.push_back(line);
    }
    return out;
}

void require_header(const std::vector<std::string>& lines) {
    if (lines.empty() || lines.front() != "#EXTM3U")
        throw PlaylistError("playlist does not start with #EXTM3U");
}

uint64_t to_u64(const std::string& s, const char* what) {
    try {
        return std::stoull(s);
    } catch (...) {
        throw PlaylistError(std::string("invalid integer for ") + what + ": '" + s + "'");
    }
}

double to_double(const std::string& s, const char* what) {
    try {
        return std::stod(s);
    } catch (...) {
        throw PlaylistError(std::string("invalid number for ") + what + ": '" + s + "'");
    }
}

} // namespace

double MediaPlaylist::total_duration_s() const {
    double total = 0.0;
    for (const auto& s : segments) total += s.duration_s;
    return total;
}

std::map<std::string, std::string> parse_attribute_list(const std::string& text) {
    std::map<std::string, std::string> attrs;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && (text[i] == ',' || text[i] == ' ')) ++i;
        size_t eq = text.find('=', i);
        if (eq == std::string::npos) break;
        std::string key = trim(text.substr(i, eq - i));
        i = eq + 1;
        std::string value;
        if (i < text.size() && text[i] == '"') {
            size_t close = text.find('"', i + 1);
            if (close == std::string::npos) throw PlaylistError("unterminated quoted attribute: " + key);
            value = text.substr(i + 1, close - i - 1);
            i = close + 1;
        } else {
            size_t comma = text.find(',', i);
            if (comma == std::string::npos) comma = text.size();
            value = trim(text.substr(i, comma - i));
            i = comma;
        }
        if (!key.empty()) attrs[key] = value;
    }
    return attrs;
}

bool is_master_playlist(const std::string& body) {
    return body.find("#EXT-X-STREAM-INF") != std::string::npos;
}

MasterPlaylist parse_master_playlist(const std::string& body, const std::string& base_uri) {
    auto lines = lines_of(body);
    require_header(lines);

    MasterPlaylist master;
    for (size_t i = 1; i < lines.size(); ++i) {
        if (!starts_with(lines[i], "#EXT-X-STREAM-INF:")) continue;
        auto attrs = parse_attribute_list(lines[i].substr(18));

        // The URI is the next non-comment line.
        size_t j = i + 1;
        while (j < lines.size() && lines[j][0] == '#') ++j;
        if (j >= lines.size()) throw PlaylistError("#EXT-X-STREAM-INF without URI");

        Variant v;
        auto bw = attrs.find("BANDWIDTH");
        if (bw == attrs.end()) throw PlaylistError("#EXT-X-STREAM-INF missing BANDWIDTH");
        v.bandwidth_bps = to_u64(bw->second, "BANDWIDTH");
        if (auto it = attrs.find("AVERAGE-BANDWIDTH"); it != attrs.end())
            v.avg_bandwidth_bps = to_u64(it->second, "AVERAGE-BANDWIDTH");
        if (auto it = attrs.find("RESOLUTION"); it != attrs.end()) {
            auto x = it->second.find('x');
            if (x != std::string::npos) {
                v.width = static_cast<int>(to_u64(it->second.substr(0, x), "RESOLUTION"));
                v.height = static_cast<int>(to_u64(it->second.substr(x + 1), "RESOLUTION"));
            }
        }
        if (auto it = attrs.find("CODECS"); it != attrs.end()) v.codecs = it->second;
        v.uri = resolve_url(base_uri, lines[j]);
        master.variants.push_back(std::move(v));
        i = j;
    }
    if (master.variants.empty()) throw PlaylistError("master playlist has no variants");

    std::stable_sort(master.variants.begin(), master.variants.end(),
                     [](const Variant& a, const Variant& b) { return a.effective_bps() < b.effective_bps(); });
    for (size_t k = 0; k < master.variants.size(); ++k) master.variants[k].index = static_cast<int>(k);
    return master;
}

MediaPlaylist parse_media_playlist(const std::string& body, const std::string& base_uri) {
    auto lines = lines_of(body);
    require_header(lines);

    MediaPlaylist pl;
    double pending_duration = -1.0;
    int64_t pending_len = -1, pending_off = -1;
    int64_t next_implicit_offset = 0;

    for (size_t i = 1; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        if (starts_with(line, "#EXT-X-VERSION:")) {
            pl.version = static_cast<int>(to_u64(line.substr(15), "VERSION"));
        } else if (starts_with(line, "#EXT-X-TARGETDURATION:")) {
            pl.target_duration_s = to_double(line.substr(22), "TARGETDURATION");
        } else if (starts_with(line, "#EXT-X-MEDIA-SEQUENCE:")) {
            pl.media_sequence = to_u64(line.substr(22), "MEDIA-SEQUENCE");
        } else if (starts_with(line, "#EXT-X-PLAYLIST-TYPE:")) {
            pl.playlist_type = line.substr(21);
        } else if (line == "#EXT-X-ENDLIST") {
            pl.endlist = true;
        } else if (starts_with(line, "#EXTINF:")) {
            std::string v = line.substr(8);
            auto comma = v.find(',');
            pending_duration = to_double(comma == std::string::npos ? v : v.substr(0, comma), "EXTINF");
        } else if (starts_with(line, "#EXT-X-BYTERANGE:")) {
            std::string v = line.substr(17);
            auto at = v.find('@');
            pending_len = static_cast<int64_t>(to_u64(at == std::string::npos ? v : v.substr(0, at), "BYTERANGE"));
            pending_off = at == std::string::npos ? next_implicit_offset
                                                  : static_cast<int64_t>(to_u64(v.substr(at + 1), "BYTERANGE"));
        } else if (line[0] != '#') {
            if (pending_duration < 0) throw PlaylistError("segment URI without preceding #EXTINF: " + line);
            SegmentInfo seg;
            seg.sequence = pl.media_sequence + pl.segments.size();
            seg.duration_s = pending_duration;
            seg.uri = resolve_url(base_uri, line);
            seg.byterange_length = pending_len;
            seg.byterange_offset = pending_off;
            if (pending_len >= 0) next_implicit_offset = pending_off + pending_len;
            pl.segments.push_back(std::move(seg));
            pending_duration = -1.0;
            pending_len = pending_off = -1;
        }
        // Unknown tags are ignored, as the HLS spec requires.
    }
    if (pl.playlist_type == "VOD") pl.endlist = true;
    return pl;
}

} // namespace bitglide

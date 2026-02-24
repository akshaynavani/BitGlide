#include "bitglide/url.h"

#include <cctype>
#include <vector>

namespace bitglide {

namespace {

bool has_scheme(const std::string& s) {
    auto pos = s.find("://");
    if (pos == std::string::npos || pos == 0) return false;
    for (size_t i = 0; i < pos; ++i) {
        if (!std::isalpha(static_cast<unsigned char>(s[i]))) return false;
    }
    return true;
}

// Collapses "." and ".." segments in an absolute path (query string preserved).
std::string normalize_path(const std::string& path) {
    std::string query;
    std::string p = path;
    auto q = p.find('?');
    if (q != std::string::npos) {
        query = p.substr(q);
        p = p.substr(0, q);
    }
    std::vector<std::string> parts;
    size_t i = 0;
    while (i <= p.size()) {
        size_t j = p.find('/', i);
        if (j == std::string::npos) j = p.size();
        std::string seg = p.substr(i, j - i);
        if (seg == "..") {
            if (!parts.empty()) parts.pop_back();
        } else if (!seg.empty() && seg != ".") {
            parts.push_back(seg);
        }
        i = j + 1;
    }
    std::string out;
    for (const auto& s : parts) out += "/" + s;
    if (out.empty() || (!p.empty() && p.back() == '/')) out += "/";
    return out + query;
}

} // namespace

std::string Url::to_string() const {
    if (scheme == "file") return "file://" + path;
    std::string s = scheme + "://" + host;
    if (port && !(scheme == "http" && port == 80)) s += ":" + std::to_string(port);
    return s + path;
}

std::optional<Url> parse_url(const std::string& text) {
    auto pos = text.find("://");
    if (pos == std::string::npos) return std::nullopt;
    Url url;
    url.scheme = text.substr(0, pos);
    for (auto& c : url.scheme) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::string rest = text.substr(pos + 3);

    if (url.scheme == "file") {
        if (rest.empty() || rest[0] != '/') return std::nullopt;
        url.path = rest;
        return url;
    }
    if (url.scheme != "http") return std::nullopt;

    auto slash = rest.find('/');
    std::string authority = slash == std::string::npos ? rest : rest.substr(0, slash);
    url.path = slash == std::string::npos ? "/" : rest.substr(slash);
    if (authority.empty()) return std::nullopt;

    auto colon = authority.rfind(':');
    if (colon != std::string::npos) {
        url.host = authority.substr(0, colon);
        try {
            url.port = std::stoi(authority.substr(colon + 1));
        } catch (...) {
            return std::nullopt;
        }
        if (url.port <= 0 || url.port > 65535) return std::nullopt;
    } else {
        url.host = authority;
        url.port = 80;
    }
    return url;
}

std::string resolve_url(const std::string& base, const std::string& ref) {
    if (ref.empty()) return base;
    if (has_scheme(ref)) return ref;

    auto parsed = parse_url(base);
    if (!parsed) return ref;
    Url url = *parsed;

    if (ref[0] == '/') {
        url.path = normalize_path(ref);
        return url.to_string();
    }
    std::string dir = url.path;
    auto q = dir.find('?');
    if (q != std::string::npos) dir = dir.substr(0, q);
    dir = dir.substr(0, dir.rfind('/') + 1);
    url.path = normalize_path(dir + ref);
    return url.to_string();
}

} // namespace bitglide

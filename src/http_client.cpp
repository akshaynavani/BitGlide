#include "bitglide/http_client.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fstream>

#include "bitglide/log.h"
#include "bitglide/url.h"

namespace bitglide {

namespace {

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

class Socket {
public:
    explicit Socket(int fd = -1) : fd_(fd) {}
    ~Socket() {
        if (fd_ >= 0) ::close(fd_);
    }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    int fd() const { return fd_; }

private:
    int fd_;
};

int connect_to(const std::string& host, int port, int timeout_ms, std::string& err) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    const std::string port_s = std::to_string(port);
    int rc = ::getaddrinfo(host.c_str(), port_s.c_str(), &hints, &res);
    if (rc != 0) {
        err = std::string("getaddrinfo: ") + gai_strerror(rc);
        return -1;
    }
    int fd = -1;
    for (addrinfo* ai = res; ai; ai = ai->ai_next) {
        fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        int one = 1;
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        if (::connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) break;
        err = std::string("connect: ") + std::strerror(errno);
        ::close(fd);
        fd = -1;
    }
    ::freeaddrinfo(res);
    return fd;
}

bool send_all(int fd, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

// Buffered reader over a socket that tracks first-byte time and cancellation.
class Reader {
public:
    Reader(int fd, const std::atomic<bool>* cancel) : fd_(fd), cancel_(cancel) {}

    // Returns false on EOF/error/cancel.
    bool fill() {
        if (cancel_ && cancel_->load()) {
            error_ = "cancelled";
            return false;
        }
        char tmp[16 * 1024];
        for (;;) {
            ssize_t n = ::recv(fd_, tmp, sizeof(tmp), 0);
            if (n > 0) {
                if (!got_first_) {
                    got_first_ = true;
                    first_byte_ = Clock::now();
                }
                buf_.append(tmp, static_cast<size_t>(n));
                return true;
            }
            if (n == 0) {
                eof_ = true;
                return false;
            }
            if (errno == EINTR) continue;
            error_ = (errno == EAGAIN || errno == EWOULDBLOCK) ? "timeout" : std::strerror(errno);
            return false;
        }
    }

    bool read_line(std::string& line) {
        for (;;) {
            auto pos = buf_.find("\r\n");
            if (pos != std::string::npos) {
                line = buf_.substr(0, pos);
                buf_.erase(0, pos + 2);
                return true;
            }
            if (!fill()) return false;
        }
    }

    // Consumes exactly n bytes (appending to out if non-null).
    bool read_exact(uint64_t n, std::string* out, uint64_t& counted) {
        while (n > 0) {
            if (buf_.empty() && !fill()) return false;
            const size_t take = static_cast<size_t>(std::min<uint64_t>(n, buf_.size()));
            if (out) out->append(buf_, 0, take);
            buf_.erase(0, take);
            counted += take;
            n -= take;
        }
        return true;
    }

    void read_to_eof(std::string* out, uint64_t& counted) {
        for (;;) {
            if (!buf_.empty()) {
                if (out) out->append(buf_);
                counted += buf_.size();
                buf_.clear();
            }
            if (!fill()) return;
        }
    }

    bool got_first() const { return got_first_; }
    Clock::time_point first_byte() const { return first_byte_; }
    const std::string& error() const { return error_; }
    bool eof() const { return eof_; }

private:
    int fd_;
    const std::atomic<bool>* cancel_;
    std::string buf_;
    bool got_first_ = false;
    bool eof_ = false;
    Clock::time_point first_byte_{};
    std::string error_;
};

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

FetchResult fetch_file(const Url& url, const FetchOptions& opts) {
    FetchResult r;
    auto t0 = Clock::now();
    std::ifstream in(url.path, std::ios::binary);
    if (!in) {
        r.status = 404;
        r.error = "cannot open " + url.path;
        return r;
    }
    if (opts.range_offset >= 0) in.seekg(opts.range_offset);
    std::string data;
    if (opts.range_length >= 0) {
        data.resize(static_cast<size_t>(opts.range_length));
        in.read(&data[0], opts.range_length);
        data.resize(static_cast<size_t>(in.gcount()));
    } else {
        data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    r.ttfb_ms = r.total_ms = ms_since(t0);
    r.bytes = data.size();
    if (opts.keep_body) r.body = std::move(data);
    r.status = 200;
    r.ok = true;
    return r;
}

FetchResult fetch_http(const Url& url, const FetchOptions& opts) {
    FetchResult r;
    std::string err;
    Socket sock(connect_to(url.host, url.port, opts.timeout_ms, err));
    if (sock.fd() < 0) {
        r.error = err;
        return r;
    }

    std::string req = "GET " + url.path + " HTTP/1.1\r\n";
    req += "Host: " + url.host + (url.port != 80 ? ":" + std::to_string(url.port) : "") + "\r\n";
    req += "User-Agent: BitGlide/0.1\r\nAccept: */*\r\nConnection: close\r\n";
    if (opts.range_offset >= 0) {
        req += "Range: bytes=" + std::to_string(opts.range_offset) + "-";
        if (opts.range_length > 0) req += std::to_string(opts.range_offset + opts.range_length - 1);
        req += "\r\n";
    }
    req += "\r\n";

    const auto t0 = Clock::now();
    if (!send_all(sock.fd(), req)) {
        r.error = std::string("send: ") + std::strerror(errno);
        return r;
    }

    Reader rd(sock.fd(), opts.cancel);
    std::string line;
    if (!rd.read_line(line)) {
        r.error = "no status line: " + rd.error();
        return r;
    }
    // "HTTP/1.1 200 OK"
    auto sp = line.find(' ');
    if (sp == std::string::npos || line.rfind("HTTP/", 0) != 0) {
        r.error = "malformed status line: " + line;
        return r;
    }
    r.status = std::atoi(line.c_str() + sp + 1);

    while (rd.read_line(line) && !line.empty()) {
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string value = line.substr(colon + 1);
        value.erase(0, value.find_first_not_of(' '));
        r.headers[lower(line.substr(0, colon))] = value;
    }
    r.ttfb_ms = rd.got_first() ? std::chrono::duration<double, std::milli>(rd.first_byte() - t0).count() : 0.0;

    std::string* out = opts.keep_body ? &r.body : nullptr;
    bool body_ok = true;
    if (auto it = r.headers.find("transfer-encoding"); it != r.headers.end() && lower(it->second) == "chunked") {
        for (;;) {
            if (!rd.read_line(line)) {
                body_ok = false;
                break;
            }
            uint64_t size = std::strtoull(line.c_str(), nullptr, 16);
            if (size == 0) {
                rd.read_line(line); // trailing CRLF
                break;
            }
            if (!rd.read_exact(size, out, r.bytes) || !rd.read_line(line)) {
                body_ok = false;
                break;
            }
        }
    } else if (auto cl = r.headers.find("content-length"); cl != r.headers.end()) {
        body_ok = rd.read_exact(std::strtoull(cl->second.c_str(), nullptr, 10), out, r.bytes);
    } else {
        rd.read_to_eof(out, r.bytes);
        body_ok = rd.eof();
    }
    r.total_ms = ms_since(t0);

    if (!body_ok) {
        r.error = "body truncated: " + (rd.error().empty() ? std::string("eof") : rd.error());
        return r;
    }
    r.ok = r.status >= 200 && r.status < 300;
    if (!r.ok) r.error = "HTTP " + std::to_string(r.status);
    return r;
}

} // namespace

FetchResult fetch(const std::string& url_text, const FetchOptions& opts) {
    auto url = parse_url(url_text);
    if (!url) {
        FetchResult r;
        r.error = "unsupported or malformed URL: " + url_text;
        return r;
    }
    FetchResult r = url->scheme == "file" ? fetch_file(*url, opts) : fetch_http(*url, opts);
    BG_DEBUG("GET ", url_text, " -> ", r.status, " ", r.bytes, "B in ", r.total_ms, "ms",
             r.error.empty() ? "" : " (" + r.error + ")");
    return r;
}

} // namespace bitglide

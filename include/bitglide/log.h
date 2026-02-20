#pragma once

#include <sstream>
#include <string>

namespace bitglide {

enum class LogLevel { Debug = 0, Info = 1, Warn = 2, Error = 3, Off = 4 };

void set_log_level(LogLevel level);
LogLevel log_level();
LogLevel parse_log_level(const std::string& name);

// Thread-safe; writes "[ts] LEVEL [thread] message" to stderr.
void log_write(LogLevel level, const std::string& message);

namespace detail {
template <typename... Args>
std::string concat(Args&&... args) {
    std::ostringstream os;
    (os << ... << args);
    return os.str();
}
} // namespace detail

} // namespace bitglide

#define BG_LOG(level, ...)                                                   \
    do {                                                                     \
        if (static_cast<int>(level) >= static_cast<int>(::bitglide::log_level())) \
            ::bitglide::log_write(level, ::bitglide::detail::concat(__VA_ARGS__)); \
    } while (0)

#define BG_DEBUG(...) BG_LOG(::bitglide::LogLevel::Debug, __VA_ARGS__)
#define BG_INFO(...)  BG_LOG(::bitglide::LogLevel::Info, __VA_ARGS__)
#define BG_WARN(...)  BG_LOG(::bitglide::LogLevel::Warn, __VA_ARGS__)
#define BG_ERROR(...) BG_LOG(::bitglide::LogLevel::Error, __VA_ARGS__)

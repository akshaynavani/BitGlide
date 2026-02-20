#include "bitglide/log.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <pthread.h>

namespace bitglide {

namespace {
std::atomic<int> g_level{static_cast<int>(LogLevel::Info)};
std::mutex g_mutex;
const auto g_start = std::chrono::steady_clock::now();

const char* level_name(LogLevel level) {
    switch (level) {
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info: return "INFO ";
    case LogLevel::Warn: return "WARN ";
    case LogLevel::Error: return "ERROR";
    default: return "?????";
    }
}
} // namespace

void set_log_level(LogLevel level) { g_level.store(static_cast<int>(level)); }

LogLevel log_level() { return static_cast<LogLevel>(g_level.load()); }

LogLevel parse_log_level(const std::string& name) {
    if (name == "debug") return LogLevel::Debug;
    if (name == "info") return LogLevel::Info;
    if (name == "warn") return LogLevel::Warn;
    if (name == "error") return LogLevel::Error;
    if (name == "off") return LogLevel::Off;
    return LogLevel::Info;
}

void log_write(LogLevel level, const std::string& message) {
    const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_start).count();
    char name[16] = "main";
#ifdef __linux__
    pthread_getname_np(pthread_self(), name, sizeof(name));
#endif
    std::lock_guard<std::mutex> lock(g_mutex);
    std::fprintf(stderr, "[%9.3f] %s [%-12s] %s\n", t, level_name(level), name, message.c_str());
}

} // namespace bitglide

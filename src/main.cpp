#include <atomic>
#include <csignal>
#include <signal.h>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

#include "bitglide/config.h"
#include "bitglide/controller.h"

namespace {
std::atomic<bool> g_interrupted{false};

extern "C" void on_signal(int) {
    g_interrupted.store(true);
}
} // namespace

int main(int argc, char** argv) {
    bitglide::Config cfg;
    try {
        if (const char* url = std::getenv("BITGLIDE_URL")) cfg.url = url;
        if (const char* level = std::getenv("BITGLIDE_LOG_LEVEL")) cfg.log_level = bitglide::parse_log_level(level);
        if (!bitglide::parse_args(cfg, argc, argv)) return 0;
        bitglide::validate(cfg);
    } catch (const std::exception& e) {
        std::cerr << "bitglide: " << e.what() << "\n\n" << bitglide::usage();
        return 2;
    }

    struct sigaction sa {};
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    std::signal(SIGPIPE, SIG_IGN); // a closed origin connection must not kill the process

    bitglide::Controller controller(cfg, &g_interrupted);
    const int rc = controller.run();
    if (rc != 0) std::cerr << "bitglide: " << controller.error() << "\n";
    return rc;
}

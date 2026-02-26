#include "bitglide/player.h"

#include <algorithm>
#include <chrono>
#include <thread>

#include "bitglide/log.h"

namespace bitglide {

const char* to_string(PlayerState s) {
    switch (s) {
    case PlayerState::Idle: return "idle";
    case PlayerState::Startup: return "startup";
    case PlayerState::Playing: return "playing";
    case PlayerState::Stalled: return "stalled";
    case PlayerState::Ended: return "ended";
    case PlayerState::Stopped: return "stopped";
    }
    return "?";
}

namespace {
double mono_s() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
} // namespace

Player::Player(SegmentBuffer& buffer, PlayerConfig cfg, std::vector<uint64_t> variant_bitrates)
    : buffer_(buffer), cfg_(cfg), bitrates_(std::move(variant_bitrates)) {
    cfg_.playback_rate = std::max(cfg_.playback_rate, 0.01);
}

Player::~Player() {
    stop();
}

void Player::start() {
    start_wall_ = mono_s();
    state_ = PlayerState::Startup;
    thread_.start("bg-player", [this] { run(); });
}

void Player::stop() {
    stop_ = true;
    thread_.join();
}

void Player::wait_until_done() {
    thread_.join();
}

double Player::now_s() const {
    return mono_s() - start_wall_;
}

double Player::buffer_ahead_s() const {
    return buffer_.level_s() + current_remaining_s_.load();
}

double Player::position_s() const {
    return position_.load();
}

PlayerStats Player::stats() const {
    std::lock_guard<std::mutex> lock(stats_mu_);
    return stats_;
}

void Player::record(const std::string& type, int variant, double value) {
    PlayerEvent ev{now_s(), position_.load(), type, variant, value};
    BG_INFO("player ", type, " pos=", ev.position_s, "s variant=", variant,
            type == "resume" ? " stalled=" + std::to_string(value) + "s" : "");
    std::lock_guard<std::mutex> lock(stats_mu_);
    stats_.events.push_back(std::move(ev));
}

// Waits (in wall time) until the buffer holds target_s of media or the stream ends.
bool Player::wait_for_level(double target_s) {
    while (!stop_) {
        if (buffer_.level_s() >= target_s || buffer_.finished() || buffer_.closed()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(cfg_.tick_ms));
    }
    return false;
}

void Player::run() {
    // --- startup ---
    if (!wait_for_level(cfg_.startup_buffer_s)) {
        state_ = PlayerState::Stopped;
        return;
    }
    {
        std::lock_guard<std::mutex> lock(stats_mu_);
        stats_.startup_delay_s = now_s();
    }
    record("startup", -1, 0.0);
    state_ = PlayerState::Playing;

    const auto tick = std::chrono::milliseconds(cfg_.tick_ms);
    while (!stop_) {
        auto seg = buffer_.pop_next(std::chrono::milliseconds(0));
        if (!seg) {
            if (buffer_.finished() || buffer_.closed()) break;
            // Buffer underrun: rebuffer until resume threshold.
            state_ = PlayerState::Stalled;
            const double stall_start = now_s();
            record("stall", last_variant_);
            if (!wait_for_level(cfg_.resume_buffer_s)) break;
            const double stalled = now_s() - stall_start;
            {
                std::lock_guard<std::mutex> lock(stats_mu_);
                stats_.stall_time_s += stalled;
                stats_.stall_count += 1;
            }
            record("resume", last_variant_, stalled);
            state_ = PlayerState::Playing;
            continue;
        }

        if (seg->variant_index != last_variant_) {
            if (last_variant_ >= 0) {
                std::lock_guard<std::mutex> lock(stats_mu_);
                stats_.switch_count += 1;
            }
            last_variant_ = seg->variant_index;
            record("switch", last_variant_);
        }

        // Play the segment: advance the media clock in ticks.
        double remaining = seg->duration_s;
        current_remaining_s_ = remaining;
        auto last = std::chrono::steady_clock::now();
        while (remaining > 0.0 && !stop_) {
            std::this_thread::sleep_for(tick);
            auto now = std::chrono::steady_clock::now();
            double media_dt = std::chrono::duration<double>(now - last).count() * cfg_.playback_rate;
            last = now;
            media_dt = std::min(media_dt, remaining);
            remaining -= media_dt;
            current_remaining_s_ = remaining;
            position_ = position_.load() + media_dt;
        }
        const double played = seg->duration_s - remaining;
        std::lock_guard<std::mutex> lock(stats_mu_);
        stats_.played_s += played;
        if (seg->variant_index >= 0 && static_cast<size_t>(seg->variant_index) < bitrates_.size())
            stats_.weighted_bitrate_sum += static_cast<double>(bitrates_[seg->variant_index]) * played;
    }

    current_remaining_s_ = 0.0;
    if (stop_) {
        state_ = PlayerState::Stopped;
    } else {
        record("ended", last_variant_);
        state_ = PlayerState::Ended;
    }
}

} // namespace bitglide

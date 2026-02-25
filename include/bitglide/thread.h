#pragma once

#include <pthread.h>

#include <cstring>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>

namespace bitglide {

// Minimal RAII wrapper over a joinable POSIX thread. The thread is joined on destruction.
class Thread {
public:
    Thread() = default;
    Thread(std::string name, std::function<void()> fn) { start(std::move(name), std::move(fn)); }
    ~Thread() { join(); }

    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;
    Thread(Thread&& other) noexcept { *this = std::move(other); }
    Thread& operator=(Thread&& other) noexcept {
        if (this != &other) {
            join();
            tid_ = other.tid_;
            running_ = other.running_;
            other.running_ = false;
        }
        return *this;
    }

    void start(std::string name, std::function<void()> fn) {
        if (running_) throw std::logic_error("thread already started");
        auto* payload = new Payload{std::move(name), std::move(fn)};
        int rc = pthread_create(&tid_, nullptr, &Thread::trampoline, payload);
        if (rc != 0) {
            delete payload;
            throw std::runtime_error(std::string("pthread_create failed: ") + std::strerror(rc));
        }
        running_ = true;
    }

    void join() {
        if (running_) {
            pthread_join(tid_, nullptr);
            running_ = false;
        }
    }

    bool joinable() const { return running_; }

private:
    struct Payload {
        std::string name;
        std::function<void()> fn;
    };

    static void* trampoline(void* arg) {
        std::unique_ptr<Payload> p(static_cast<Payload*>(arg));
        std::string name = p->name.substr(0, 15); // kernel limit is 16 incl. NUL
#if defined(__linux__)
        pthread_setname_np(pthread_self(), name.c_str());
#elif defined(__APPLE__)
        pthread_setname_np(name.c_str());
#endif
        p->fn();
        return nullptr;
    }

    pthread_t tid_{};
    bool running_ = false;
};

} // namespace bitglide

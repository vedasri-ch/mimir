#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include "../storage/store.hpp"

namespace mimir {

// Runs a background thread that periodically sweeps expired keys from all
// shards. Lazy expiration (checked on access) handles the hot path; the
// sweeper handles cold keys that are never touched again.
class ExpiryManager {
public:
    ExpiryManager(Store& store, std::chrono::milliseconds interval)
        : store_(store), interval_(interval), running_(false) {}

    ~ExpiryManager() { stop(); }

    void start() {
        running_ = true;
        thread_ = std::thread([this] {
            while (true) {
                std::unique_lock<std::mutex> lock(mu_);
                // Wait for the interval or until stop() wakes us early.
                cv_.wait_for(lock, interval_, [this] { return !running_.load(); });
                if (!running_) break;
                lock.unlock();
                store_.sweep_expired();
            }
        });
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(mu_);
            running_ = false;
        }
        cv_.notify_all();
        if (thread_.joinable()) thread_.join();
    }

private:
    Store&                      store_;
    std::chrono::milliseconds   interval_;
    std::atomic<bool>           running_;
    std::thread                 thread_;
    std::mutex                  mu_;
    std::condition_variable     cv_;
};

} // namespace mimir

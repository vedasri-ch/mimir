#pragma once

#include <chrono>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>
#include "../config/config.hpp"

namespace mimir {

using Clock     = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<Clock>;

// A stored value: string or integer.
// Using variant avoids heap allocation for the integer case.
using Value = std::variant<std::string, int64_t>;

struct Entry {
    Value      value;
    // Optional absolute expiry. No value = no expiration.
    std::optional<TimePoint> expires_at;

    // LRU / LFU metadata
    uint64_t   access_time  = 0; // logical clock for LRU
    uint64_t   access_count = 0; // frequency counter for LFU

    bool is_expired() const {
        return expires_at.has_value() && Clock::now() >= *expires_at;
    }
};

// One shard of the key-value store. Each shard has its own reader-writer lock
// to minimise contention across unrelated keys.
class StoreShard {
public:
    // Returns false if key not found or expired.
    bool get(const std::string& key, Entry& out);
    void set(const std::string& key, Entry entry);
    bool del(const std::string& key);
    bool exists(const std::string& key);
    // Returns true if the key existed and had a TTL set.
    bool expire(const std::string& key, std::chrono::seconds ttl);
    // Returns remaining TTL in seconds. -2 = no key, -1 = no expiry.
    int64_t ttl(const std::string& key);
    void flush();

    // Called by expiration sweeper — removes expired entries.
    std::size_t sweep_expired();

    // Atomic read-modify-write for INCR/DECR. Holds the write lock for the
    // entire operation — no TOCTOU race between reading and writing.
    std::variant<int64_t, std::string> atomic_incr(const std::string& key, int64_t delta);

    // For snapshotting: iterate all live entries (holds read lock).
    template <typename Fn>
    void for_each(Fn&& fn) const {
        std::shared_lock lock(rw_);
        for (auto& [k, e] : map_) {
            if (!e.is_expired()) fn(k, e);
        }
    }

    std::size_t size() const {
        std::shared_lock lock(rw_);
        return map_.size();
    }

private:
    mutable std::shared_mutex                rw_;
    std::unordered_map<std::string, Entry>   map_;
    std::atomic<uint64_t>                    logical_clock_{0};
};

// Sharded key-value store. Routes keys to shards by hash to reduce lock
// contention under concurrent workloads.
class Store {
public:
    explicit Store(int num_shards, EvictionPolicy policy = EvictionPolicy::LRU,
                   std::size_t max_memory = 0);

    bool    get(const std::string& key, Entry& out);
    void    set(const std::string& key, Value val,
                std::optional<std::chrono::seconds> ttl = std::nullopt);
    bool    del(const std::string& key);
    bool    exists(const std::string& key);
    bool    expire(const std::string& key, std::chrono::seconds ttl);
    int64_t ttl(const std::string& key);
    void    flush_all();

    // Atomic increment/decrement. Returns new value or error string.
    std::variant<int64_t, std::string> incr(const std::string& key, int64_t delta = 1);

    std::size_t total_keys() const;
    std::size_t sweep_expired();

    // Snapshot support
    template <typename Fn>
    void for_each(Fn&& fn) const {
        for (auto& shard : shards_) shard->for_each(fn);
    }

    int num_shards() const { return static_cast<int>(shards_.size()); }

private:
    StoreShard& shard_for(const std::string& key);

    std::vector<std::unique_ptr<StoreShard>> shards_;
    EvictionPolicy                           eviction_policy_;
    std::size_t                              max_memory_;
};

} // namespace mimir

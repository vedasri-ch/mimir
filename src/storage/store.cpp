#include "store.hpp"
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace mimir {

// ── StoreShard ──────────────────────────────────────────────────────────────

bool StoreShard::get(const std::string& key, Entry& out) {
    // Use unique_lock so we can safely update LRU/LFU metadata.
    // For read-heavy workloads where metadata accuracy is not critical,
    // this could be relaxed to a shared_lock with atomic metadata fields.
    std::unique_lock lock(rw_);
    auto it = map_.find(key);
    if (it == map_.end()) return false;
    if (it->second.is_expired()) return false;
    out = it->second;
    it->second.access_time  = logical_clock_.fetch_add(1, std::memory_order_relaxed);
    it->second.access_count++;
    return true;
}

void StoreShard::set(const std::string& key, Entry entry) {
    entry.access_time = logical_clock_.fetch_add(1, std::memory_order_relaxed);
    std::unique_lock lock(rw_);
    map_.insert_or_assign(key, std::move(entry));
}

bool StoreShard::del(const std::string& key) {
    std::unique_lock lock(rw_);
    return map_.erase(key) > 0;
}

bool StoreShard::exists(const std::string& key) {
    std::shared_lock lock(rw_);
    auto it = map_.find(key);
    return it != map_.end() && !it->second.is_expired();
}

bool StoreShard::expire(const std::string& key, std::chrono::seconds ttl) {
    std::unique_lock lock(rw_);
    auto it = map_.find(key);
    if (it == map_.end() || it->second.is_expired()) return false;
    it->second.expires_at = Clock::now() + ttl;
    return true;
}

int64_t StoreShard::ttl(const std::string& key) {
    std::shared_lock lock(rw_);
    auto it = map_.find(key);
    if (it == map_.end() || it->second.is_expired()) return -2;
    if (!it->second.expires_at.has_value()) return -1;
    auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
        *it->second.expires_at - Clock::now());
    return remaining.count() > 0 ? remaining.count() : 0;
}

void StoreShard::flush() {
    std::unique_lock lock(rw_);
    map_.clear();
}

std::size_t StoreShard::sweep_expired() {
    std::unique_lock lock(rw_);
    std::size_t removed = 0;
    for (auto it = map_.begin(); it != map_.end(); ) {
        if (it->second.is_expired()) {
            it = map_.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

std::variant<int64_t, std::string> StoreShard::atomic_incr(const std::string& key, int64_t delta) {
    // Hold the write lock for the entire read-modify-write — no TOCTOU race.
    std::unique_lock lock(rw_);
    int64_t current = 0;
    std::optional<TimePoint> existing_expiry;

    auto it = map_.find(key);
    if (it != map_.end() && !it->second.is_expired()) {
        existing_expiry = it->second.expires_at;
        if (std::holds_alternative<int64_t>(it->second.value)) {
            current = std::get<int64_t>(it->second.value);
        } else {
            const auto& s = std::get<std::string>(it->second.value);
            try { current = std::stoll(s); }
            catch (...) { return std::string("ERR value is not an integer or out of range"); }
        }
    }

    int64_t next = current + delta;
    uint64_t prev_count = (it != map_.end() && !it->second.is_expired())
                          ? it->second.access_count : 0;
    Entry e;
    e.value        = next;
    e.expires_at   = existing_expiry;
    e.access_time  = logical_clock_.fetch_add(1, std::memory_order_relaxed);
    e.access_count = prev_count + 1;
    map_.insert_or_assign(key, std::move(e));
    return next;
}

// ── Store ───────────────────────────────────────────────────────────────────

Store::Store(int num_shards, EvictionPolicy policy, std::size_t max_memory)
    : eviction_policy_(policy), max_memory_(max_memory) {
    shards_.reserve(num_shards);
    for (int i = 0; i < num_shards; ++i)
        shards_.push_back(std::make_unique<StoreShard>());
}

StoreShard& Store::shard_for(const std::string& key) {
    std::size_t h = std::hash<std::string>{}(key);
    return *shards_[h % shards_.size()];
}

bool Store::get(const std::string& key, Entry& out) {
    return shard_for(key).get(key, out);
}

void Store::set(const std::string& key, Value val,
                std::optional<std::chrono::seconds> ttl) {
    Entry e;
    e.value = std::move(val);
    if (ttl) e.expires_at = Clock::now() + *ttl;
    shard_for(key).set(key, std::move(e));
}

bool Store::del(const std::string& key) {
    return shard_for(key).del(key);
}

bool Store::exists(const std::string& key) {
    return shard_for(key).exists(key);
}

bool Store::expire(const std::string& key, std::chrono::seconds ttl) {
    return shard_for(key).expire(key, ttl);
}

int64_t Store::ttl(const std::string& key) {
    return shard_for(key).ttl(key);
}

void Store::flush_all() {
    for (auto& s : shards_) s->flush();
}

std::variant<int64_t, std::string> Store::incr(const std::string& key, int64_t delta) {
    // Must hold the write lock for the entire read-modify-write to be atomic.
    // We delegate to StoreShard::atomic_incr which takes the exclusive lock once.
    return shard_for(key).atomic_incr(key, delta);
}

std::size_t Store::total_keys() const {
    std::size_t total = 0;
    for (auto& s : shards_) total += s->size();
    return total;
}

std::size_t Store::sweep_expired() {
    std::size_t total = 0;
    for (auto& s : shards_) total += s->sweep_expired();
    return total;
}

} // namespace mimir

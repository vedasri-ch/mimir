#pragma once

#include <cstdint>
#include <string>

namespace mimir {

enum class EvictionPolicy { NOEVICTION, LRU, LFU };

struct Config {
    // Network
    std::string host          = "0.0.0.0";
    uint16_t    port          = 6379;
    int         backlog       = 128;

    // Threading
    int worker_threads        = 4;

    // Storage
    int         num_shards    = 16;   // hash table shards for reduced contention
    std::size_t max_memory    = 0;    // 0 = unlimited
    EvictionPolicy eviction   = EvictionPolicy::LRU;

    // WAL / Persistence
    std::string wal_path      = "./data/mimir.wal";
    std::string rdb_path      = "./data/mimir.rdb";
    int         snapshot_interval_sec = 300; // 0 = disabled
    bool        wal_enabled   = true;
    bool        fsync_always  = false;   // fsync on every write; safe but slow
    bool        fsync_every_sec = true;

    // Expiration
    int expiry_check_interval_ms = 100;

    // Logging
    std::string log_level     = "INFO";

    static Config from_file(const std::string& path);
    static Config defaults() { return Config{}; }
};

} // namespace mimir

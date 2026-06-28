#include "config.hpp"
#include "../logging/logger.hpp"

#include <yaml-cpp/yaml.h>
#include <stdexcept>

namespace mimir {

static EvictionPolicy parse_eviction(const std::string& s) {
    if (s == "lru")         return EvictionPolicy::LRU;
    if (s == "lfu")         return EvictionPolicy::LFU;
    if (s == "noeviction")  return EvictionPolicy::NOEVICTION;
    throw std::invalid_argument("Unknown eviction policy: " + s);
}

Config Config::from_file(const std::string& path) {
    Config cfg;
    try {
        YAML::Node node = YAML::LoadFile(path);

        if (auto n = node["host"])                     cfg.host                    = n.as<std::string>();
        if (auto n = node["port"])                     cfg.port                    = n.as<uint16_t>();
        if (auto n = node["worker_threads"])           cfg.worker_threads          = n.as<int>();
        if (auto n = node["num_shards"])               cfg.num_shards              = n.as<int>();
        if (auto n = node["max_memory"])               cfg.max_memory              = n.as<std::size_t>();
        if (auto n = node["eviction_policy"])          cfg.eviction                = parse_eviction(n.as<std::string>());
        if (auto n = node["wal_path"])                 cfg.wal_path                = n.as<std::string>();
        if (auto n = node["rdb_path"])                 cfg.rdb_path                = n.as<std::string>();
        if (auto n = node["snapshot_interval_sec"])    cfg.snapshot_interval_sec   = n.as<int>();
        if (auto n = node["wal_enabled"])              cfg.wal_enabled             = n.as<bool>();
        if (auto n = node["fsync_always"])             cfg.fsync_always            = n.as<bool>();
        if (auto n = node["expiry_check_interval_ms"]) cfg.expiry_check_interval_ms= n.as<int>();
        if (auto n = node["log_level"])                cfg.log_level               = n.as<std::string>();

    } catch (const YAML::Exception& e) {
        LOG_WARN("Failed to load config file '%s': %s. Using defaults.", path.c_str(), e.what());
    }
    return cfg;
}

} // namespace mimir

#include "snapshot.hpp"
#include "../logging/logger.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace mimir {

static constexpr uint32_t SNAP_MAGIC = 0x524B5356; // "RKSV"
static constexpr uint8_t  TYPE_STR   = 1;
static constexpr uint8_t  TYPE_INT   = 2;

// ── helpers ──────────────────────────────────────────────────────────────────

static void w8(std::ostream& os, uint8_t v)   { os.write((char*)&v, 1); }
static void w16(std::ostream& os, uint16_t v) { os.write((char*)&v, 2); }
static void w32(std::ostream& os, uint32_t v) { os.write((char*)&v, 4); }
static void w64(std::ostream& os, uint64_t v) { os.write((char*)&v, 8); }
static void wstr(std::ostream& os, const std::string& s) {
    w16(os, static_cast<uint16_t>(s.size()));
    os.write(s.data(), static_cast<std::streamsize>(s.size()));
}

static bool r8(std::istream& is, uint8_t& v)   { return (bool)is.read((char*)&v, 1); }
static bool r16(std::istream& is, uint16_t& v) { return (bool)is.read((char*)&v, 2); }
static bool r32(std::istream& is, uint32_t& v) { return (bool)is.read((char*)&v, 4); }
static bool r64(std::istream& is, uint64_t& v) { return (bool)is.read((char*)&v, 8); }
static bool rstr(std::istream& is, std::string& s) {
    uint16_t len;
    if (!r16(is, len)) return false;
    s.resize(len);
    return (bool)is.read(s.data(), len);
}

// ── Snapshot ─────────────────────────────────────────────────────────────────

Snapshot::Snapshot(const std::string& path) : path_(path) {
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
}

bool Snapshot::save(const Store& store) {
    std::string tmp = path_ + ".tmp";
    std::ofstream os(tmp, std::ios::binary | std::ios::trunc);
    if (!os) {
        LOG_ERR("Snapshot: failed to open temp file %s", tmp.c_str());
        return false;
    }

    // Count live entries first
    std::size_t count = 0;
    store.for_each([&](const std::string&, const Entry&) { ++count; });

    w32(os, SNAP_MAGIC);
    w64(os, static_cast<uint64_t>(count));

    store.for_each([&](const std::string& key, const Entry& e) {
        wstr(os, key);

        if (std::holds_alternative<std::string>(e.value)) {
            w8(os, TYPE_STR);
            wstr(os, std::get<std::string>(e.value));
        } else {
            w8(os, TYPE_INT);
            w64(os, static_cast<uint64_t>(std::get<int64_t>(e.value)));
        }

        if (e.expires_at.has_value()) {
            w8(os, 1);
            // Store as remaining milliseconds from now — avoids steady_clock
            // epoch being arbitrary and non-portable across restarts.
            auto remaining_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                *e.expires_at - Clock::now()).count();
            if (remaining_ms < 0) remaining_ms = 0;
            w64(os, static_cast<uint64_t>(remaining_ms));
        } else {
            w8(os, 0);
            w64(os, 0);
        }
    });

    os.flush();
    if (!os) {
        LOG_ERR("Snapshot: write error");
        return false;
    }
    os.close();

    // Atomic rename
    std::error_code ec;
    std::filesystem::rename(tmp, path_, ec);
    if (ec) {
        LOG_ERR("Snapshot: rename failed: %s", ec.message().c_str());
        return false;
    }
    LOG_INFO("Snapshot saved: %zu entries -> %s", count, path_.c_str());
    return true;
}

bool Snapshot::load(Store& store) {
    std::ifstream is(path_, std::ios::binary);
    if (!is) return false;

    uint32_t magic;
    if (!r32(is, magic) || magic != SNAP_MAGIC) {
        LOG_ERR("Snapshot: bad magic in %s", path_.c_str());
        return false;
    }

    uint64_t count;
    if (!r64(is, count)) return false;

    for (uint64_t i = 0; i < count; ++i) {
        std::string key;
        if (!rstr(is, key)) { LOG_ERR("Snapshot: truncated at entry %llu", (unsigned long long)i); return false; }

        uint8_t type;
        if (!r8(is, type)) return false;

        Value val;
        if (type == TYPE_STR) {
            std::string s;
            if (!rstr(is, s)) return false;
            val = std::move(s);
        } else {
            uint64_t iv;
            if (!r64(is, iv)) return false;
            val = static_cast<int64_t>(iv);
        }

        uint8_t has_ttl;
        uint64_t ttl_ms_raw;
        if (!r8(is, has_ttl) || !r64(is, ttl_ms_raw)) return false;

        std::optional<std::chrono::seconds> ttl;
        if (has_ttl) {
            // remaining_ms was stored relative to save time; subtract elapsed
            // time since save is not tracked, so we use it directly as a TTL.
            // If remaining_ms is 0 the key is already expired — skip it.
            if (ttl_ms_raw == 0) continue;
            ttl = std::chrono::seconds(static_cast<int64_t>(ttl_ms_raw) / 1000);
            if (ttl->count() <= 0) continue;
        }

        store.set(key, std::move(val), ttl);
    }

    LOG_INFO("Snapshot loaded: %llu entries from %s", (unsigned long long)count, path_.c_str());
    return true;
}

} // namespace mimir

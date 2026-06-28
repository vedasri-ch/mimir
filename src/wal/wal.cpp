#include "wal.hpp"
#include "../logging/logger.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <inttypes.h>
#include <mutex>
#include <stdexcept>
#include <unistd.h>
#include <sys/stat.h>
#include <filesystem>

namespace mimir {

static constexpr uint32_t MAGIC = 0x4D4D4952; // "MMIR"

// ── CRC32 (simple implementation, no dependency) ────────────────────────────

static uint32_t crc32_table[256];
static std::once_flag crc_init_flag;

static void init_crc32() {
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int j = 0; j < 8; ++j)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc32_table[i] = c;
    }
}

static uint32_t crc32(const uint8_t* data, std::size_t len) {
    std::call_once(crc_init_flag, init_crc32);
    uint32_t c = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < len; ++i)
        c = crc32_table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

// ── Serialisation helpers ────────────────────────────────────────────────────

static void write_u8(std::vector<uint8_t>& buf, uint8_t v) {
    buf.push_back(v);
}

static void write_u16le(std::vector<uint8_t>& buf, uint16_t v) {
    buf.push_back(v & 0xFF);
    buf.push_back((v >> 8) & 0xFF);
}

static void write_u32le(std::vector<uint8_t>& buf, uint32_t v) {
    for (int i = 0; i < 4; ++i) { buf.push_back(v & 0xFF); v >>= 8; }
}

static void write_u64le(std::vector<uint8_t>& buf, uint64_t v) {
    for (int i = 0; i < 8; ++i) { buf.push_back(v & 0xFF); v >>= 8; }
}

static void write_bytes(std::vector<uint8_t>& buf, const std::string& s) {
    buf.insert(buf.end(), s.begin(), s.end());
}

// ── Read helpers (from raw pointer) ─────────────────────────────────────────

static bool read_u8(const uint8_t* buf, std::size_t size, std::size_t& pos, uint8_t& out) {
    if (pos + 1 > size) return false;
    out = buf[pos++];
    return true;
}
static bool read_u16le(const uint8_t* buf, std::size_t size, std::size_t& pos, uint16_t& out) {
    if (pos + 2 > size) return false;
    out = (uint16_t)buf[pos] | ((uint16_t)buf[pos+1] << 8);
    pos += 2;
    return true;
}
static bool read_u32le(const uint8_t* buf, std::size_t size, std::size_t& pos, uint32_t& out) {
    if (pos + 4 > size) return false;
    out = 0;
    for (int i = 0; i < 4; ++i) out |= (uint32_t)buf[pos+i] << (8*i);
    pos += 4;
    return true;
}
static bool read_u64le(const uint8_t* buf, std::size_t size, std::size_t& pos, uint64_t& out) {
    if (pos + 8 > size) return false;
    out = 0;
    for (int i = 0; i < 8; ++i) out |= (uint64_t)buf[pos+i] << (8*i);
    pos += 8;
    return true;
}

// ── Wal ─────────────────────────────────────────────────────────────────────

Wal::Wal(const std::string& path, bool fsync_always, bool fsync_every_sec)
    : path_(path), fsync_always_(fsync_always), fsync_every_sec_(fsync_every_sec) {
    // Ensure directory exists
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    if (!open_or_create())
        throw std::runtime_error("Failed to open WAL file: " + path);
}

Wal::~Wal() { close_fd(); }

bool Wal::open_or_create() {
    fd_ = ::open(path_.c_str(), O_CREAT | O_RDWR | O_APPEND, 0644);
    if (fd_ < 0) {
        LOG_ERR("WAL open failed: %s", std::strerror(errno));
        return false;
    }
    return true;
}

void Wal::close_fd() {
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
}

bool Wal::append(WalOp op, const std::string& key,
                 const std::string& value, int64_t ttl_sec) {
    std::vector<uint8_t> buf;
    buf.reserve(64 + key.size() + value.size());

    uint64_t seq = seq_.fetch_add(1, std::memory_order_relaxed);

    // Build record body (everything before CRC)
    write_u32le(buf, MAGIC);
    write_u8(buf, static_cast<uint8_t>(op));
    write_u64le(buf, seq);
    write_u16le(buf, static_cast<uint16_t>(key.size()));
    write_bytes(buf, key);
    write_u64le(buf, static_cast<uint64_t>(value.size()));
    write_bytes(buf, value);
    write_u64le(buf, static_cast<uint64_t>(ttl_sec));

    uint32_t crc = crc32(buf.data(), buf.size());
    write_u32le(buf, crc);

    std::lock_guard<std::mutex> lock(mu_);
    ssize_t written = ::write(fd_, buf.data(), buf.size());
    if (written < 0 || static_cast<std::size_t>(written) != buf.size()) {
        LOG_ERR("WAL write failed: %s", std::strerror(errno));
        return false;
    }

    if (fsync_always_) ::fdatasync(fd_);
    return true;
}

void Wal::replay(const ReplayFn& fn) {
    // Read entire file into memory for parsing.
    struct stat st;
    if (::fstat(fd_, &st) < 0) return;
    if (st.st_size == 0) return;

    std::vector<uint8_t> data(st.st_size);
    if (::pread(fd_, data.data(), data.size(), 0) != st.st_size) {
        LOG_ERR("WAL read failed during replay");
        return;
    }

    std::size_t pos = 0;
    std::size_t sz  = data.size();
    uint64_t    max_seq = 0;
    int         records = 0;

    while (pos < sz) {
        std::size_t record_start = pos;

        uint32_t magic;
        if (!read_u32le(data.data(), sz, pos, magic) || magic != MAGIC) {
            LOG_WARN("WAL: corrupt record at offset %zu, stopping replay", record_start);
            break;
        }

        uint8_t op_byte;
        uint64_t seq;
        uint16_t key_len;
        if (!read_u8(data.data(), sz, pos, op_byte) ||
            !read_u64le(data.data(), sz, pos, seq)) break;

        if (!read_u16le(data.data(), sz, pos, key_len)) break;
        if (pos + key_len > sz) break;
        std::string key(reinterpret_cast<const char*>(data.data() + pos), key_len);
        pos += key_len;

        uint64_t val_len;
        if (!read_u64le(data.data(), sz, pos, val_len)) break;
        if (pos + val_len > sz) break;
        std::string value(reinterpret_cast<const char*>(data.data() + pos), val_len);
        pos += val_len;

        uint64_t ttl_raw;
        if (!read_u64le(data.data(), sz, pos, ttl_raw)) break;

        uint32_t stored_crc;
        if (!read_u32le(data.data(), sz, pos, stored_crc)) break;

        uint32_t computed = crc32(data.data() + record_start, pos - 4 - record_start);
        if (computed != stored_crc) {
            LOG_WARN("WAL: CRC mismatch at offset %zu, stopping replay", record_start);
            break;
        }

        WalRecord rec;
        rec.op      = static_cast<WalOp>(op_byte);
        rec.seq     = seq;
        rec.key     = std::move(key);
        rec.value   = std::move(value);
        rec.ttl_sec = static_cast<int64_t>(ttl_raw);

        fn(rec);
        max_seq = std::max(max_seq, seq);
        ++records;
    }

    seq_.store(max_seq + 1, std::memory_order_relaxed);
    LOG_INFO("WAL replay complete: %d records, max_seq=%" PRIu64, records, max_seq);
}

void Wal::truncate() {
    std::lock_guard<std::mutex> lock(mu_);
    ::ftruncate(fd_, 0);
    ::lseek(fd_, 0, SEEK_SET);
    LOG_INFO("WAL truncated");
}

void Wal::sync() {
    ::fdatasync(fd_);
}

} // namespace mimir

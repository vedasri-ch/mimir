#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace mimir {

// WAL record types
enum class WalOp : uint8_t {
    SET    = 1,
    DEL    = 2,
    EXPIRE = 3,
    FLUSH  = 4,
    INCR   = 5,
};

// Binary WAL record layout (little-endian):
//   [4 bytes] magic  0x4D4D4952 "MMIR"
//   [1 byte]  op
//   [8 bytes] sequence number
//   [2 bytes] key length
//   [N bytes] key
//   [8 bytes] value length (0 for DEL/FLUSH)
//   [M bytes] value
//   [8 bytes] ttl_seconds (0 = no TTL, for SET/EXPIRE)
//   [4 bytes] CRC32 of the above

struct WalRecord {
    WalOp       op;
    uint64_t    seq;
    std::string key;
    std::string value;    // empty for DEL, FLUSH
    int64_t     ttl_sec = 0;
};

// Callback invoked during replay.
using ReplayFn = std::function<void(const WalRecord&)>;

class Wal {
public:
    explicit Wal(const std::string& path, bool fsync_always, bool fsync_every_sec);
    ~Wal();

    // Append a record to the WAL. Thread-safe.
    // Returns false if the write failed.
    bool append(WalOp op, const std::string& key,
                const std::string& value = {},
                int64_t ttl_sec = 0);

    // Replay all records from the WAL file, calling fn for each.
    // Stops at first corrupt record (truncated / bad CRC).
    void replay(const ReplayFn& fn);

    // Truncate the WAL file (called after a successful snapshot).
    void truncate();

    // Force fsync on the underlying file descriptor.
    void sync();

    uint64_t next_seq() const { return seq_.load(std::memory_order_relaxed); }

private:
    std::string  path_;
    int          fd_       = -1;
    bool         fsync_always_;
    bool         fsync_every_sec_;
    std::mutex   mu_;
    std::atomic<uint64_t> seq_{1};

    bool open_or_create();
    void close_fd();
};

} // namespace mimir

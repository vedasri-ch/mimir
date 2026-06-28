#pragma once

#include <string>
#include "../storage/store.hpp"

namespace mimir {

// RDB-style snapshot: serialises the entire store to a binary file.
// Format: MAGIC(4) + num_entries(8) + [key_len(2) + key + val_type(1) +
//          val_data + has_ttl(1) + ttl_unix_sec(8)] * N + CRC32(4)
//
// The snapshot is written atomically via a temp file + rename.

class Snapshot {
public:
    explicit Snapshot(const std::string& path);

    // Write current store state. Returns false on failure.
    bool save(const Store& store);

    // Load snapshot into store. Returns false if no snapshot or corrupt.
    bool load(Store& store);

private:
    std::string path_;
};

} // namespace mimir

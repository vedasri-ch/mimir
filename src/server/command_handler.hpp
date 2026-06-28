#pragma once

#include <memory>
#include <string>
#include "../protocol/command.hpp"
#include "../storage/store.hpp"
#include "../wal/wal.hpp"

namespace mimir {

// Executes a parsed Command against the Store and WAL.
// Returns a RESP-encoded response string ready to send back to the client.
class CommandHandler {
public:
    CommandHandler(Store& store, Wal* wal);

    std::string handle(const Command& cmd);

private:
    Store& store_;
    Wal*   wal_;   // nullable — persistence may be disabled

    std::string handle_ping(const Command& cmd);
    std::string handle_set(const Command& cmd);
    std::string handle_get(const Command& cmd);
    std::string handle_del(const Command& cmd);
    std::string handle_exists(const Command& cmd);
    std::string handle_incr(const Command& cmd, int64_t delta);
    std::string handle_expire(const Command& cmd);
    std::string handle_ttl(const Command& cmd);
    std::string handle_flushall(const Command& cmd);
};

} // namespace mimir

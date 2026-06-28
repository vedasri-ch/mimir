#pragma once

#include <string>
#include <vector>

namespace mimir {

enum class CommandType {
    PING,
    SET,
    GET,
    DEL,
    EXISTS,
    INCR,
    DECR,
    EXPIRE,
    TTL,
    FLUSHALL,
    UNKNOWN,
};

struct Command {
    CommandType          type = CommandType::UNKNOWN;
    std::vector<std::string> args; // raw tokens including command name at [0]
};

} // namespace mimir

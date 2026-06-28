#pragma once

#include <cstdint>
#include <string>

namespace mimir {

// Produces Redis-compatible RESP (REdis Serialisation Protocol) responses.
// Using RESP ensures compatibility with existing Redis clients (redis-cli, etc.)
//
// RESP wire format recap:
//   +OK\r\n               simple string
//   -ERR message\r\n      error
//   :42\r\n               integer
//   $6\r\nfoobar\r\n      bulk string
//   $-1\r\n               null bulk string (nil)

class Serializer {
public:
    static std::string ok();
    static std::string error(const std::string& msg);
    static std::string integer(int64_t v);
    static std::string bulk(const std::string& s);
    static std::string null_bulk();
    static std::string pong();
};

} // namespace mimir

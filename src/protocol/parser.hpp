#pragma once

#include "command.hpp"
#include <string>
#include <string_view>

namespace mimir {

// Parses a simple inline text protocol inspired by Redis inline commands.
// Each request is a newline-terminated line:
//   COMMAND [arg1] [arg2] ...
//
// Tokens are separated by spaces. Values with spaces are not supported in
// inline mode (use RESP framing for that — see serializer.hpp).

class Parser {
public:
    // Parse a single command line (without trailing \n).
    // Returns a Command with type == UNKNOWN on parse error.
    static Command parse(std::string_view line);

private:
    static CommandType identify(std::string_view token);
};

} // namespace mimir

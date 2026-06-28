#include "parser.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace mimir {

static std::string to_upper(std::string_view sv) {
    std::string s(sv);
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return s;
}

Command Parser::parse(std::string_view line) {
    Command cmd;

    // Tokenise on whitespace
    std::string_view remaining = line;
    while (!remaining.empty()) {
        // Skip leading spaces
        auto start = remaining.find_first_not_of(" \t\r");
        if (start == std::string_view::npos) break;
        remaining = remaining.substr(start);

        auto end = remaining.find_first_of(" \t\r");
        std::string token(remaining.substr(0, end));
        cmd.args.push_back(std::move(token));
        if (end == std::string_view::npos) break;
        remaining = remaining.substr(end);
    }

    if (cmd.args.empty()) return cmd;

    cmd.type = identify(cmd.args[0]);
    return cmd;
}

CommandType Parser::identify(std::string_view token) {
    std::string upper = to_upper(token);
    if (upper == "PING")     return CommandType::PING;
    if (upper == "SET")      return CommandType::SET;
    if (upper == "GET")      return CommandType::GET;
    if (upper == "DEL")      return CommandType::DEL;
    if (upper == "EXISTS")   return CommandType::EXISTS;
    if (upper == "INCR")     return CommandType::INCR;
    if (upper == "DECR")     return CommandType::DECR;
    if (upper == "EXPIRE")   return CommandType::EXPIRE;
    if (upper == "TTL")      return CommandType::TTL;
    if (upper == "FLUSHALL") return CommandType::FLUSHALL;
    return CommandType::UNKNOWN;
}

} // namespace mimir

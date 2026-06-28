#include "serializer.hpp"

namespace mimir {

std::string Serializer::ok()                       { return "+OK\r\n"; }
std::string Serializer::pong()                     { return "+PONG\r\n"; }
std::string Serializer::null_bulk()                { return "$-1\r\n"; }
std::string Serializer::error(const std::string& m){ return "-ERR " + m + "\r\n"; }
std::string Serializer::integer(int64_t v)         { return ":" + std::to_string(v) + "\r\n"; }

std::string Serializer::bulk(const std::string& s) {
    return "$" + std::to_string(s.size()) + "\r\n" + s + "\r\n";
}

} // namespace mimir

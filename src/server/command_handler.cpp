#include "command_handler.hpp"
#include "../protocol/serializer.hpp"
#include "../logging/logger.hpp"

#include <charconv>

namespace mimir {

CommandHandler::CommandHandler(Store& store, Wal* wal)
    : store_(store), wal_(wal) {}

std::string CommandHandler::handle(const Command& cmd) {
    switch (cmd.type) {
        case CommandType::PING:     return handle_ping(cmd);
        case CommandType::SET:      return handle_set(cmd);
        case CommandType::GET:      return handle_get(cmd);
        case CommandType::DEL:      return handle_del(cmd);
        case CommandType::EXISTS:   return handle_exists(cmd);
        case CommandType::INCR:     return handle_incr(cmd, +1);
        case CommandType::DECR:     return handle_incr(cmd, -1);
        case CommandType::EXPIRE:   return handle_expire(cmd);
        case CommandType::TTL:      return handle_ttl(cmd);
        case CommandType::FLUSHALL: return handle_flushall(cmd);
        default:
            return Serializer::error("unknown command '" +
                (cmd.args.empty() ? "" : cmd.args[0]) + "'");
    }
}

std::string CommandHandler::handle_ping(const Command& cmd) {
    if (cmd.args.size() > 1)
        return Serializer::bulk(cmd.args[1]);
    return Serializer::pong();
}

std::string CommandHandler::handle_set(const Command& cmd) {
    // SET key value [EX seconds]
    if (cmd.args.size() < 3)
        return Serializer::error("wrong number of arguments for 'SET'");

    const std::string& key = cmd.args[1];
    const std::string& val = cmd.args[2];

    std::optional<std::chrono::seconds> ttl;
    int64_t ttl_sec = 0;

    if (cmd.args.size() >= 5) {
        std::string opt = cmd.args[3];
        std::transform(opt.begin(), opt.end(), opt.begin(), ::toupper);
        if (opt == "EX") {
            int64_t sec = 0;
            auto [p, ec] = std::from_chars(cmd.args[4].data(),
                                           cmd.args[4].data() + cmd.args[4].size(), sec);
            if (ec != std::errc{} || sec <= 0)
                return Serializer::error("invalid expire time in 'SET'");
            ttl     = std::chrono::seconds(sec);
            ttl_sec = sec;
        }
    }

    if (wal_) wal_->append(WalOp::SET, key, val, ttl_sec);
    store_.set(key, val, ttl);
    return Serializer::ok();
}

std::string CommandHandler::handle_get(const Command& cmd) {
    if (cmd.args.size() < 2)
        return Serializer::error("wrong number of arguments for 'GET'");

    Entry e;
    if (!store_.get(cmd.args[1], e)) return Serializer::null_bulk();

    if (std::holds_alternative<std::string>(e.value))
        return Serializer::bulk(std::get<std::string>(e.value));

    return Serializer::bulk(std::to_string(std::get<int64_t>(e.value)));
}

std::string CommandHandler::handle_del(const Command& cmd) {
    if (cmd.args.size() < 2)
        return Serializer::error("wrong number of arguments for 'DEL'");

    int deleted = 0;
    for (std::size_t i = 1; i < cmd.args.size(); ++i) {
        if (wal_) wal_->append(WalOp::DEL, cmd.args[i]);
        if (store_.del(cmd.args[i])) ++deleted;
    }
    return Serializer::integer(deleted);
}

std::string CommandHandler::handle_exists(const Command& cmd) {
    if (cmd.args.size() < 2)
        return Serializer::error("wrong number of arguments for 'EXISTS'");
    int count = 0;
    for (std::size_t i = 1; i < cmd.args.size(); ++i)
        if (store_.exists(cmd.args[i])) ++count;
    return Serializer::integer(count);
}

std::string CommandHandler::handle_incr(const Command& cmd, int64_t delta) {
    if (cmd.args.size() < 2)
        return Serializer::error("wrong number of arguments");

    // WAL records the delta before the mutation so replay is correct
    // regardless of current store state.
    if (wal_) wal_->append(WalOp::INCR, cmd.args[1],
                            std::to_string(delta));

    auto result = store_.incr(cmd.args[1], delta);
    if (std::holds_alternative<std::string>(result))
        return Serializer::error(std::get<std::string>(result));

    return Serializer::integer(std::get<int64_t>(result));
}

std::string CommandHandler::handle_expire(const Command& cmd) {
    if (cmd.args.size() < 3)
        return Serializer::error("wrong number of arguments for 'EXPIRE'");

    int64_t sec = 0;
    auto [p, ec] = std::from_chars(cmd.args[2].data(),
                                   cmd.args[2].data() + cmd.args[2].size(), sec);
    if (ec != std::errc{} || sec < 0)
        return Serializer::error("invalid expire time");

    if (wal_) wal_->append(WalOp::EXPIRE, cmd.args[1], {}, sec);
    bool ok = store_.expire(cmd.args[1], std::chrono::seconds(sec));
    return Serializer::integer(ok ? 1 : 0);
}

std::string CommandHandler::handle_ttl(const Command& cmd) {
    if (cmd.args.size() < 2)
        return Serializer::error("wrong number of arguments for 'TTL'");
    return Serializer::integer(store_.ttl(cmd.args[1]));
}

std::string CommandHandler::handle_flushall(const Command& cmd) {
    if (wal_) wal_->append(WalOp::FLUSH, {});
    store_.flush_all();
    return Serializer::ok();
}

} // namespace mimir

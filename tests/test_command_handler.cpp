#include <gtest/gtest.h>
#include "../src/server/command_handler.hpp"
#include "../src/protocol/parser.hpp"
#include "../src/storage/store.hpp"

using namespace mimir;

class HandlerTest : public ::testing::Test {
protected:
    Store           store{16};
    CommandHandler  handler{store, nullptr}; // no WAL in unit tests

    std::string run(const std::string& line) {
        return handler.handle(Parser::parse(line));
    }
};

TEST_F(HandlerTest, Ping) {
    EXPECT_EQ(run("PING"), "+PONG\r\n");
}

TEST_F(HandlerTest, PingWithMessage) {
    EXPECT_EQ(run("PING hello"), "$5\r\nhello\r\n");
}

TEST_F(HandlerTest, SetAndGet) {
    EXPECT_EQ(run("SET foo bar"), "+OK\r\n");
    EXPECT_EQ(run("GET foo"),     "$3\r\nbar\r\n");
}

TEST_F(HandlerTest, GetMissing) {
    EXPECT_EQ(run("GET nothing"), "$-1\r\n");
}

TEST_F(HandlerTest, Del) {
    run("SET k v");
    EXPECT_EQ(run("DEL k"),     ":1\r\n");
    EXPECT_EQ(run("DEL k"),     ":0\r\n");
}

TEST_F(HandlerTest, Exists) {
    run("SET a 1");
    EXPECT_EQ(run("EXISTS a"),   ":1\r\n");
    EXPECT_EQ(run("EXISTS nope"),":0\r\n");
}

TEST_F(HandlerTest, IncrDecr) {
    EXPECT_EQ(run("INCR cnt"), ":1\r\n");
    EXPECT_EQ(run("INCR cnt"), ":2\r\n");
    EXPECT_EQ(run("DECR cnt"), ":1\r\n");
}

TEST_F(HandlerTest, IncrOnNonInteger) {
    run("SET s hello");
    auto resp = run("INCR s");
    EXPECT_TRUE(resp.rfind("-ERR", 0) == 0);
}

TEST_F(HandlerTest, TTLOnPersistentKey) {
    run("SET k v");
    EXPECT_EQ(run("TTL k"), ":-1\r\n");
}

TEST_F(HandlerTest, TTLOnMissingKey) {
    EXPECT_EQ(run("TTL ghost"), ":-2\r\n");
}

TEST_F(HandlerTest, ExpireAndTTL) {
    run("SET k v");
    run("EXPIRE k 100");
    std::string ttlResp = run("TTL k");
    // Should be :99\r\n or :100\r\n
    EXPECT_TRUE(ttlResp.rfind(":", 0) == 0);
    EXPECT_NE(ttlResp, ":-1\r\n");
    EXPECT_NE(ttlResp, ":-2\r\n");
}

TEST_F(HandlerTest, FlushAll) {
    run("SET a 1");
    run("SET b 2");
    EXPECT_EQ(run("FLUSHALL"), "+OK\r\n");
    EXPECT_EQ(run("GET a"),    "$-1\r\n");
}

TEST_F(HandlerTest, SetWithEX) {
    EXPECT_EQ(run("SET tmp val EX 60"), "+OK\r\n");
    std::string ttl = run("TTL tmp");
    EXPECT_TRUE(ttl.rfind(":", 0) == 0);
    EXPECT_NE(ttl, ":-1\r\n");
}

TEST_F(HandlerTest, UnknownCommand) {
    auto r = run("BLORP");
    EXPECT_TRUE(r.rfind("-ERR", 0) == 0);
}

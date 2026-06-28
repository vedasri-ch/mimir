#include <gtest/gtest.h>
#include "../src/protocol/parser.hpp"

using namespace mimir;

TEST(ParserTest, Ping) {
    auto cmd = Parser::parse("PING");
    EXPECT_EQ(cmd.type, CommandType::PING);
}

TEST(ParserTest, PingWithMessage) {
    auto cmd = Parser::parse("PING hello");
    EXPECT_EQ(cmd.type, CommandType::PING);
    ASSERT_EQ(cmd.args.size(), 2u);
    EXPECT_EQ(cmd.args[1], "hello");
}

TEST(ParserTest, SetCommand) {
    auto cmd = Parser::parse("SET mykey myvalue");
    EXPECT_EQ(cmd.type, CommandType::SET);
    ASSERT_EQ(cmd.args.size(), 3u);
    EXPECT_EQ(cmd.args[1], "mykey");
    EXPECT_EQ(cmd.args[2], "myvalue");
}

TEST(ParserTest, SetWithExpiry) {
    auto cmd = Parser::parse("SET k v EX 30");
    EXPECT_EQ(cmd.type, CommandType::SET);
    ASSERT_EQ(cmd.args.size(), 5u);
    EXPECT_EQ(cmd.args[4], "30");
}

TEST(ParserTest, GetCommand) {
    auto cmd = Parser::parse("GET key");
    EXPECT_EQ(cmd.type, CommandType::GET);
    EXPECT_EQ(cmd.args[1], "key");
}

TEST(ParserTest, DelCommand) {
    auto cmd = Parser::parse("DEL a b c");
    EXPECT_EQ(cmd.type, CommandType::DEL);
    EXPECT_EQ(cmd.args.size(), 4u);
}

TEST(ParserTest, CaseInsensitive) {
    EXPECT_EQ(Parser::parse("set k v").type, CommandType::SET);
    EXPECT_EQ(Parser::parse("Get k").type,   CommandType::GET);
    EXPECT_EQ(Parser::parse("PING").type,    CommandType::PING);
}

TEST(ParserTest, UnknownCommand) {
    auto cmd = Parser::parse("BLORP key");
    EXPECT_EQ(cmd.type, CommandType::UNKNOWN);
}

TEST(ParserTest, EmptyLine) {
    auto cmd = Parser::parse("");
    EXPECT_EQ(cmd.type, CommandType::UNKNOWN);
    EXPECT_TRUE(cmd.args.empty());
}

TEST(ParserTest, ExtraWhitespace) {
    auto cmd = Parser::parse("  SET   k   v  ");
    EXPECT_EQ(cmd.type, CommandType::SET);
    EXPECT_EQ(cmd.args[1], "k");
    EXPECT_EQ(cmd.args[2], "v");
}

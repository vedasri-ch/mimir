#include <gtest/gtest.h>
#include "../src/wal/wal.hpp"

#include <cstdio>
#include <filesystem>
#include <vector>

using namespace mimir;

static const std::string TMP_WAL = "/tmp/mimir_test.wal";

class WalTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::filesystem::remove(TMP_WAL);
    }
    void TearDown() override {
        std::filesystem::remove(TMP_WAL);
    }
};

TEST_F(WalTest, AppendAndReplay) {
    {
        Wal wal(TMP_WAL, false, false);
        EXPECT_TRUE(wal.append(WalOp::SET, "name", "alice"));
        EXPECT_TRUE(wal.append(WalOp::SET, "age",  "30", 0));
        EXPECT_TRUE(wal.append(WalOp::DEL, "name"));
    }

    Wal wal(TMP_WAL, false, false);
    std::vector<WalRecord> records;
    wal.replay([&](const WalRecord& r) { records.push_back(r); });

    ASSERT_EQ(records.size(), 3u);
    EXPECT_EQ(records[0].op,    WalOp::SET);
    EXPECT_EQ(records[0].key,   "name");
    EXPECT_EQ(records[0].value, "alice");
    EXPECT_EQ(records[1].op,    WalOp::SET);
    EXPECT_EQ(records[1].key,   "age");
    EXPECT_EQ(records[2].op,    WalOp::DEL);
    EXPECT_EQ(records[2].key,   "name");
}

TEST_F(WalTest, TruncateClearsRecords) {
    Wal wal(TMP_WAL, false, false);
    wal.append(WalOp::SET, "k", "v");
    wal.truncate();

    std::vector<WalRecord> records;
    wal.replay([&](const WalRecord& r) { records.push_back(r); });
    EXPECT_TRUE(records.empty());
}

TEST_F(WalTest, TTLPreserved) {
    {
        Wal wal(TMP_WAL, false, false);
        wal.append(WalOp::SET, "tmp", "val", 42);
    }

    Wal wal(TMP_WAL, false, false);
    std::vector<WalRecord> records;
    wal.replay([&](const WalRecord& r) { records.push_back(r); });

    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].ttl_sec, 42);
}

TEST_F(WalTest, FlushOp) {
    Wal wal(TMP_WAL, false, false);
    wal.append(WalOp::SET,   "a", "1");
    wal.append(WalOp::FLUSH, "");

    std::vector<WalRecord> records;
    wal.replay([&](const WalRecord& r) { records.push_back(r); });

    ASSERT_EQ(records.size(), 2u);
    EXPECT_EQ(records[1].op, WalOp::FLUSH);
}

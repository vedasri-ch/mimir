#include <gtest/gtest.h>
#include "../src/storage/store.hpp"

#include <chrono>
#include <thread>

using namespace mimir;

class StoreTest : public ::testing::Test {
protected:
    Store store{16};
};

TEST_F(StoreTest, SetAndGet) {
    store.set("name", std::string("alice"));
    Entry e;
    EXPECT_TRUE(store.get("name", e));
    EXPECT_EQ(std::get<std::string>(e.value), "alice");
}

TEST_F(StoreTest, GetMissingKey) {
    Entry e;
    EXPECT_FALSE(store.get("missing", e));
}

TEST_F(StoreTest, DeleteKey) {
    store.set("x", std::string("1"));
    EXPECT_TRUE(store.del("x"));
    Entry e;
    EXPECT_FALSE(store.get("x", e));
}

TEST_F(StoreTest, ExistsKey) {
    store.set("k", std::string("v"));
    EXPECT_TRUE(store.exists("k"));
    EXPECT_FALSE(store.exists("nope"));
}

TEST_F(StoreTest, IncrDecr) {
    auto r1 = store.incr("counter", 1);
    ASSERT_TRUE(std::holds_alternative<int64_t>(r1));
    EXPECT_EQ(std::get<int64_t>(r1), 1);

    auto r2 = store.incr("counter", 5);
    EXPECT_EQ(std::get<int64_t>(r2), 6);

    auto r3 = store.incr("counter", -2);
    EXPECT_EQ(std::get<int64_t>(r3), 4);
}

TEST_F(StoreTest, IncrOnStringFails) {
    store.set("s", std::string("not-a-number"));
    auto r = store.incr("s", 1);
    EXPECT_TRUE(std::holds_alternative<std::string>(r));
}

TEST_F(StoreTest, TTLExpiry) {
    store.set("tmp", std::string("val"), std::chrono::seconds(1));
    Entry e;
    EXPECT_TRUE(store.get("tmp", e));

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    EXPECT_FALSE(store.get("tmp", e));
}

TEST_F(StoreTest, TtlCommand) {
    store.set("k", std::string("v"), std::chrono::seconds(10));
    int64_t t = store.ttl("k");
    EXPECT_GE(t, 9);
    EXPECT_LE(t, 10);
}

TEST_F(StoreTest, TtlNoExpiry) {
    store.set("k", std::string("v"));
    EXPECT_EQ(store.ttl("k"), -1);
}

TEST_F(StoreTest, TtlMissingKey) {
    EXPECT_EQ(store.ttl("ghost"), -2);
}

TEST_F(StoreTest, FlushAll) {
    store.set("a", std::string("1"));
    store.set("b", std::string("2"));
    store.flush_all();
    EXPECT_EQ(store.total_keys(), 0u);
}

TEST_F(StoreTest, ExpireSetsNewTTL) {
    store.set("k", std::string("v"));
    EXPECT_TRUE(store.expire("k", std::chrono::seconds(5)));
    int64_t t = store.ttl("k");
    EXPECT_GE(t, 4);
    EXPECT_LE(t, 5);
}

TEST_F(StoreTest, SweepExpired) {
    store.set("exp1", std::string("v"), std::chrono::seconds(1));
    store.set("exp2", std::string("v"), std::chrono::seconds(1));
    store.set("perm", std::string("v"));

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    std::size_t removed = store.sweep_expired();
    EXPECT_EQ(removed, 2u);
    EXPECT_TRUE(store.exists("perm"));
}

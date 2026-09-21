#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <thread>

#include "quantengine/core/events.hpp"
#include "quantengine/journal/AsyncJournal.hpp"

namespace quantengine::test {

using namespace quantengine::core;
using namespace quantengine::journal;
using namespace quantengine::market;

class AsyncJournalTest : public ::testing::Test {
protected:
    std::string test_file_ = "test_async_journal.qevj";

    void SetUp() override { std::filesystem::remove(test_file_); }

    void TearDown() override { std::filesystem::remove(test_file_); }
};

TEST_F(AsyncJournalTest, StartStopLifecycle) {
    AsyncJournal async_j;
    EXPECT_FALSE(async_j.is_running());

    EXPECT_TRUE(async_j.start(test_file_));
    EXPECT_TRUE(async_j.is_running());

    async_j.stop();
    EXPECT_FALSE(async_j.is_running());
}

TEST_F(AsyncJournalTest, AsyncLogAndFlushDrainsQueue) {
    AsyncJournal async_j;
    ASSERT_TRUE(async_j.start(test_file_));

    // Submit a burst of commands asynchronously
    constexpr std::uint64_t BURST_COUNT = 100;
    for (std::uint64_t i = 1; i <= BURST_COUNT; ++i) {
        OrderCommand cmd{};
        cmd.sequence_number = i;
        cmd.payload =
            CreateOrderCommand{.order_id = i, .side = Side::Buy, .price = 10000, .quantity = 10};
        EXPECT_TRUE(async_j.log_command(cmd, static_cast<std::int64_t>(1000 + i)));
    }

    // Submit a market event
    Quote q{};
    q.symbol = make_symbol("AMD");
    q.bid_price = 15000;
    q.bid_size = 100;
    q.ask_price = 15010;
    q.ask_size = 200;
    EXPECT_TRUE(async_j.log_event(MarketEvent{q}));

    EXPECT_EQ(async_j.records_queued(), BURST_COUNT + 1);

    // Stop will flush all remaining queue items to disk
    async_j.stop();

    EXPECT_EQ(async_j.records_flushed(), BURST_COUNT + 1);
    EXPECT_EQ(async_j.records_dropped(), 0u);

    // Verify reading back using BinaryJournalReader
    BinaryJournalReader reader;
    ASSERT_TRUE(reader.open(test_file_));
    const auto commands = reader.read_all_commands();
    EXPECT_EQ(commands.size(), BURST_COUNT);

    const auto events = reader.read_all_market_events();
    ASSERT_EQ(events.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<Quote>(events[0]));
    EXPECT_EQ(symbol_view(std::get<Quote>(events[0]).symbol), "AMD");
}

}  // namespace quantengine::test

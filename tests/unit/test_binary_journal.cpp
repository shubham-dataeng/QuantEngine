#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "quantengine/core/events.hpp"
#include "quantengine/engine/canonical_state.hpp"
#include "quantengine/engine/matching_engine.hpp"
#include "quantengine/journal/BinaryJournal.hpp"

namespace quantengine::test {

using namespace quantengine::core;
using namespace quantengine::engine;
using namespace quantengine::journal;
using namespace quantengine::market;

class BinaryJournalTest : public ::testing::Test {
protected:
    std::string test_file_ = "test_journal.qevj";

    void SetUp() override { std::filesystem::remove(test_file_); }

    void TearDown() override { std::filesystem::remove(test_file_); }
};

TEST_F(BinaryJournalTest, CreateAndValidateHeader) {
    {
        BinaryJournalWriter writer;
        ASSERT_TRUE(writer.open(test_file_));
        EXPECT_EQ(writer.record_count(), 0u);
        EXPECT_EQ(writer.bytes_written(), sizeof(JournalHeader));
    }

    {
        BinaryJournalReader reader;
        ASSERT_TRUE(reader.open(test_file_));
        EXPECT_TRUE(reader.header().is_valid());
        EXPECT_EQ(reader.header().version, kJournalCurrentVersion);
    }
}

TEST_F(BinaryJournalTest, AppendAndReadCommands) {
    std::vector<OrderCommand> original_commands;

    // Create command
    OrderCommand c1{};
    c1.sequence_number = 1;
    c1.payload =
        CreateOrderCommand{.order_id = 1001, .side = Side::Buy, .price = 15000, .quantity = 200};
    original_commands.push_back(c1);

    // Modify command
    OrderCommand c2{};
    c2.sequence_number = 2;
    c2.payload = ModifyOrderCommand{.order_id = 1001, .new_price = 15050, .new_quantity = 250};
    original_commands.push_back(c2);

    // Cancel command
    OrderCommand c3{};
    c3.sequence_number = 3;
    c3.payload = CancelOrderCommand{.order_id = 1001};
    original_commands.push_back(c3);

    // Write
    {
        BinaryJournalWriter writer;
        ASSERT_TRUE(writer.open(test_file_));
        for (const auto& cmd : original_commands) {
            EXPECT_TRUE(writer.append_command(cmd, 123456789));
        }
        EXPECT_EQ(writer.record_count(), 3u);
    }

    // Read
    {
        BinaryJournalReader reader;
        ASSERT_TRUE(reader.open(test_file_));
        const auto read_commands = reader.read_all_commands();
        ASSERT_EQ(read_commands.size(), original_commands.size());

        for (std::size_t i = 0; i < original_commands.size(); ++i) {
            EXPECT_EQ(read_commands[i].sequence_number, original_commands[i].sequence_number);
            EXPECT_EQ(read_commands[i].payload, original_commands[i].payload);
        }
    }
}

TEST_F(BinaryJournalTest, AppendAndReadMarketEvents) {
    Quote q{};
    q.symbol = make_symbol("NVDA");
    q.bid_price = 12000;
    q.bid_size = 500;
    q.ask_price = 12010;
    q.ask_size = 300;
    q.exchange_ts = 1000;
    q.recv_ts = 1005;

    Tick t{};
    t.symbol = make_symbol("NVDA");
    t.price = 12005;
    t.quantity = 100;
    t.exchange_ts = 2000;
    t.recv_ts = 2005;

    // Write
    {
        BinaryJournalWriter writer;
        ASSERT_TRUE(writer.open(test_file_));
        EXPECT_TRUE(writer.append_market_event(MarketEvent{q}));
        EXPECT_TRUE(writer.append_market_event(MarketEvent{t}));
        EXPECT_EQ(writer.record_count(), 2u);
    }

    // Read
    {
        BinaryJournalReader reader;
        ASSERT_TRUE(reader.open(test_file_));
        const auto read_events = reader.read_all_market_events();
        ASSERT_EQ(read_events.size(), 2u);

        ASSERT_TRUE(std::holds_alternative<Quote>(read_events[0]));
        const auto& read_q = std::get<Quote>(read_events[0]);
        EXPECT_EQ(symbol_view(read_q.symbol), "NVDA");
        EXPECT_EQ(read_q.bid_price, 12000);
        EXPECT_EQ(read_q.bid_size, 500u);

        ASSERT_TRUE(std::holds_alternative<Tick>(read_events[1]));
        const auto& read_t = std::get<Tick>(read_events[1]);
        EXPECT_EQ(symbol_view(read_t.symbol), "NVDA");
        EXPECT_EQ(read_t.price, 12005);
        EXPECT_EQ(read_t.quantity, 100u);
    }
}

TEST_F(BinaryJournalTest, ReplayIntoEngineBitForBitDeterministic) {
    OptimizedMatchingEngine live_engine;
    BinaryJournalWriter writer;
    ASSERT_TRUE(writer.open(test_file_));

    // Submit live commands and journal them
    for (std::uint64_t i = 1; i <= 20; ++i) {
        OrderCommand cmd{};
        cmd.sequence_number = i;
        if (i % 2 == 1) {
            cmd.payload = CreateOrderCommand{.order_id = i,
                                             .side = Side::Buy,
                                             .price = static_cast<PriceTicks>(1000 + i),
                                             .quantity = 10};
        } else {
            cmd.payload = CreateOrderCommand{.order_id = i,
                                             .side = Side::Sell,
                                             .price = static_cast<PriceTicks>(1050 + i),
                                             .quantity = 10};
        }
        (void)live_engine.process_command(cmd);
        writer.append_command(cmd);
    }
    writer.close();

    // Replay into a fresh engine from journal
    OptimizedMatchingEngine replayed_engine;
    BinaryJournalReader reader;
    ASSERT_TRUE(reader.open(test_file_));
    const std::size_t count = reader.replay_into(replayed_engine);
    EXPECT_EQ(count, 20u);

    // Verify bit-for-bit canonical state hash match
    EXPECT_EQ(CanonicalState::compute_hash(live_engine),
              CanonicalState::compute_hash(replayed_engine));
}

TEST_F(BinaryJournalTest, InvalidMagicFailsToOpen) {
    // Write invalid file
    {
        std::ofstream bad(test_file_, std::ios::binary);
        std::array<char, 32> junk{};
        junk.fill('X');
        bad.write(junk.data(), junk.size());
    }

    BinaryJournalReader reader;
    EXPECT_FALSE(reader.open(test_file_));
    EXPECT_FALSE(reader.is_open());
}

}  // namespace quantengine::test

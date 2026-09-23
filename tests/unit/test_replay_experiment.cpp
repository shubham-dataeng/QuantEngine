// tests/unit/test_replay_experiment.cpp
//
// Phase 5: Deterministic Market-Making Strategy Research Experiment.
// Proves end-to-end integration across all L3 replay components:
//   L3CsvParser -> HistoricalL3Book -> QueuePositionTracker -> ReplayGateway -> Strategy ->
//   Portfolio

#include <gtest/gtest.h>

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "quantengine/core/types.hpp"
#include "quantengine/execution/IExecutionGateway.hpp"
#include "quantengine/portfolio/Portfolio.hpp"
#include "quantengine/replay/EventClock.hpp"
#include "quantengine/replay/FifoQueueModel.hpp"
#include "quantengine/replay/HistoricalL3Book.hpp"
#include "quantengine/replay/L3CsvParser.hpp"
#include "quantengine/replay/LatencyModel.hpp"
#include "quantengine/replay/QueuePositionTracker.hpp"
#include "quantengine/replay/ReplayGateway.hpp"
#include "quantengine/replay/VirtualTimeline.hpp"

namespace quantengine::replay::test {

using namespace quantengine::core;
using namespace quantengine::execution;
using namespace quantengine::portfolio;

// Simple deterministic market-making strategy
class SimpleMarketMaker {
public:
    SimpleMarketMaker(ReplayGateway& gateway, const HistoricalL3Book& book,
                      market::SymbolArray symbol, Quantity quote_qty = 10)
        : gateway_(gateway), book_(book), symbol_(symbol), quote_qty_(quote_qty) {}

    void on_market_update() {
        auto best_bid = book_.best_bid_price();
        auto best_ask = book_.best_ask_price();

        if (!best_bid.has_value() || !best_ask.has_value() || *best_bid >= *best_ask) {
            return;
        }

        // Quote at the inside market
        PriceTicks target_bid = *best_bid;
        PriceTicks target_ask = *best_ask;

        if (!active_bid_id_ || current_bid_price_ != target_bid) {
            if (active_bid_id_) {
                (void)gateway_.cancel_order(CancelRequest{.client_order_id = *active_bid_id_});
            }
            OrderId id = ++next_order_id_;
            OrderAck ack = gateway_.submit_order(OrderRequest{
                .client_order_id = id,
                .side = Side::Buy,
                .type = OrderType::Limit,
                .time_in_force = TimeInForce::Day,
                .symbol = symbol_,
                .price = target_bid,
                .quantity = quote_qty_,
            });
            if (ack.accepted()) {
                active_bid_id_ = id;
                current_bid_price_ = target_bid;
                ++quotes_placed_;
            }
        }

        if (!active_ask_id_ || current_ask_price_ != target_ask) {
            if (active_ask_id_) {
                (void)gateway_.cancel_order(CancelRequest{.client_order_id = *active_ask_id_});
            }
            OrderId id = ++next_order_id_;
            OrderAck ack = gateway_.submit_order(OrderRequest{
                .client_order_id = id,
                .side = Side::Sell,
                .type = OrderType::Limit,
                .time_in_force = TimeInForce::Day,
                .symbol = symbol_,
                .price = target_ask,
                .quantity = quote_qty_,
            });
            if (ack.accepted()) {
                active_ask_id_ = id;
                current_ask_price_ = target_ask;
                ++quotes_placed_;
            }
        }
    }

    [[nodiscard]] auto quotes_placed() const noexcept -> std::size_t { return quotes_placed_; }

private:
    ReplayGateway& gateway_;
    const HistoricalL3Book& book_;
    market::SymbolArray symbol_;
    Quantity quote_qty_{10};

    OrderId next_order_id_{1000};
    std::optional<OrderId> active_bid_id_;
    std::optional<OrderId> active_ask_id_;
    PriceTicks current_bid_price_{0};
    PriceTicks current_ask_price_{0};
    std::size_t quotes_placed_{0};
};

struct ExperimentResult {
    PriceTicks realized_pnl{0};
    int64_t net_position{0};
    uint64_t book_hash{0};
    std::size_t quotes_placed{0};
};

static auto run_experiment(std::string_view csv_data) -> ExperimentResult {
    std::istringstream stream{std::string(csv_data)};
    L3CsvParser parser{stream, "experiment.csv"};

    HistoricalL3Book book;
    QueuePositionTracker tracker;
    EventClock clock(0);
    LatencyConfig latency = LatencyConfig::symmetric_network(10'000);  // 10 us network delay

    ReplayGateway gateway{book, tracker, clock, latency};
    Portfolio portfolio;
    (void)gateway.connect(portfolio);

    const auto sym = market::make_symbol("AAPL");
    SimpleMarketMaker mm{gateway, book, sym, 25};

    L3Message msg;
    while (parser.next(msg)) {
        gateway.process_historical_message(msg);
        mm.on_market_update();
    }

    const auto* pos = portfolio.find_position(sym);
    return ExperimentResult{
        .realized_pnl = portfolio.total_realized_pnl(),
        .net_position = pos ? pos->net_quantity : 0,
        .book_hash = book.compute_canonical_hash(),
        .quotes_placed = mm.quotes_placed(),
    };
}

TEST(ReplayExperimentTest, EndToEndMarketMakingPipeline) {
    // Synthetic market scenario with liquidity entering, quotes establishing spread,
    // and aggressors sweeping both bids and asks:
    const std::string session_csv =
        // Initial book establishing 150.00 / 150.50 spread
        "A,1000000000,101,AAPL,15000,50,B\n"
        "A,1000000010,102,AAPL,15050,50,S\n"
        // Aggressive sell hits bid level: 50 consumes order 101, next 25 fills MM's bid!
        "E,1000000100,101,AAPL,75,901\n"
        // Aggressive buy sweeps ask level: 50 consumes order 102, next 25 fills MM's ask!
        "E,1000000200,102,AAPL,75,902\n";

    ExperimentResult res = run_experiment(session_csv);

    // MM bought 25 @ 150.00 and sold 25 @ 150.50 -> realized gain = 25 * 50 ticks = +1250 ticks
    // (+$12.50)
    EXPECT_GT(res.quotes_placed, 0u);
    EXPECT_EQ(res.net_position, 0);  // Flat position after round-trip
    EXPECT_EQ(res.realized_pnl, 1250);
}

TEST(ReplayExperimentTest, BitForBitDeterminismAcrossIndependentRuns) {
    const std::string session_csv =
        "A,1000000000,101,AAPL,15000,50,B\n"
        "A,1000000010,102,AAPL,15050,50,S\n"
        "A,1000000020,103,AAPL,14990,100,B\n"
        "A,1000000030,104,AAPL,15060,100,S\n"
        "E,1000000100,101,AAPL,75,901\n"
        "C,1000000150,103,AAPL,50\n"
        "E,1000000200,102,AAPL,75,902\n";

    ExperimentResult run1 = run_experiment(session_csv);
    ExperimentResult run2 = run_experiment(session_csv);

    EXPECT_EQ(run1.realized_pnl, run2.realized_pnl);
    EXPECT_EQ(run1.net_position, run2.net_position);
    EXPECT_EQ(run1.book_hash, run2.book_hash);
    EXPECT_EQ(run1.quotes_placed, run2.quotes_placed);
}

}  // namespace quantengine::replay::test

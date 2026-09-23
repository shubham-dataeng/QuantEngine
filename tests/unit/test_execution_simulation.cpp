// tests/unit/test_execution_simulation.cpp
//
// Phase 4 TDD: Virtual Time, Latency Model, and Execution Simulation.

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "quantengine/core/events.hpp"
#include "quantengine/core/types.hpp"
#include "quantengine/execution/IExecutionGateway.hpp"
#include "quantengine/portfolio/Portfolio.hpp"
#include "quantengine/replay/EventClock.hpp"
#include "quantengine/replay/FifoQueueModel.hpp"
#include "quantengine/replay/HistoricalL3Book.hpp"
#include "quantengine/replay/LatencyModel.hpp"
#include "quantengine/replay/QueuePositionTracker.hpp"
#include "quantengine/replay/ReplayGateway.hpp"
#include "quantengine/replay/VirtualTimeline.hpp"

namespace quantengine::replay::test {

using namespace quantengine::core;
using namespace quantengine::execution;
using namespace quantengine::portfolio;

// Mock Fill Handler for tracking reports
class MockFillHandler : public IFillHandler {
public:
    void on_fill(const ExecutionReport& report) noexcept override { reports.push_back(report); }
    void on_gateway_connected() noexcept override { connected = true; }
    void on_gateway_disconnected(std::string_view /*reason*/) noexcept override {
        connected = false;
    }

    std::vector<ExecutionReport> reports;
    bool connected{false};
};

// ===========================================================================
// EventClock Tests
// ===========================================================================

TEST(EventClockTest, MonotonicAdvanceAndReset) {
    EventClock clock(1000);
    EXPECT_EQ(clock.current_time(), 1000);

    EXPECT_TRUE(clock.advance_to(1500));
    EXPECT_EQ(clock.current_time(), 1500);

    // Advancing backwards is rejected
    EXPECT_FALSE(clock.advance_to(1200));
    EXPECT_EQ(clock.current_time(), 1500);

    // Advancing by delta
    EXPECT_TRUE(clock.advance_by(500));
    EXPECT_EQ(clock.current_time(), 2000);

    // Negative delta rejected
    EXPECT_FALSE(clock.advance_by(-100));
    EXPECT_EQ(clock.current_time(), 2000);

    // Reset
    clock.reset(5000);
    EXPECT_EQ(clock.current_time(), 5000);
}

// ===========================================================================
// LatencyModel Tests
// ===========================================================================

TEST(LatencyModelTest, ConfigCalculations) {
    LatencyConfig cfg{
        .feed_latency_ns = 50'000,      // 50 us
        .decision_latency_ns = 10'000,  // 10 us
        .entry_latency_ns = 40'000,     // 40 us
        .response_latency_ns = 40'000,  // 40 us
    };

    EXPECT_EQ(cfg.total_round_trip(), 80'000);
    EXPECT_EQ(cfg.total_pipeline_delay(), 140'000);

    auto sym = LatencyConfig::symmetric_network(25'000);
    EXPECT_EQ(sym.feed_latency_ns, 25'000);
    EXPECT_EQ(sym.entry_latency_ns, 25'000);
    EXPECT_EQ(sym.response_latency_ns, 25'000);
    EXPECT_EQ(sym.total_round_trip(), 50'000);
}

// ===========================================================================
// VirtualTimeline Tests
// ===========================================================================

TEST(VirtualTimelineTest, DeterministicOrderingAndDispatch) {
    EventClock clock(0);
    VirtualTimeline timeline(clock);

    std::vector<int> executed_order;

    // Schedule out-of-order in time
    timeline.schedule_action(300, [&](EventClock& c) {
        EXPECT_EQ(c.current_time(), 300);
        executed_order.push_back(3);
    });
    timeline.schedule_action(100, [&](EventClock& c) {
        EXPECT_EQ(c.current_time(), 100);
        executed_order.push_back(1);
    });
    timeline.schedule_action(200, [&](EventClock& c) {
        EXPECT_EQ(c.current_time(), 200);
        executed_order.push_back(2);
    });

    EXPECT_EQ(timeline.event_count(), 3u);
    EXPECT_EQ(timeline.peek_next_time(), 100);

    // Run all
    size_t processed = timeline.run_all();
    EXPECT_EQ(processed, 3u);
    EXPECT_EQ(clock.current_time(), 300);

    ASSERT_EQ(executed_order.size(), 3u);
    EXPECT_EQ(executed_order[0], 1);
    EXPECT_EQ(executed_order[1], 2);
    EXPECT_EQ(executed_order[2], 3);
}

TEST(VirtualTimelineTest, RunUntilLimitsExecution) {
    EventClock clock(0);
    VirtualTimeline timeline(clock);

    timeline.schedule_action(100, [](EventClock&) {});
    timeline.schedule_action(200, [](EventClock&) {});
    timeline.schedule_action(500, [](EventClock&) {});

    EXPECT_EQ(timeline.run_until(250), 2u);
    EXPECT_EQ(clock.current_time(), 250);
    EXPECT_EQ(timeline.event_count(), 1u);
}

// ===========================================================================
// Execution Simulator: ReplayGateway Passive Execution
// ===========================================================================

TEST(ReplayGatewayTest, PassiveOrderQueueTrackingAndFill) {
    HistoricalL3Book book;
    QueuePositionTracker tracker;
    EventClock clock(1'000'000'000LL);

    ReplayGateway gateway(book, tracker, clock);
    MockFillHandler handler;
    ASSERT_TRUE(gateway.connect(handler));
    EXPECT_TRUE(handler.connected);

    // Initial historical market: Buy 100 @ 15000
    book.apply_add(OrderAdded{
        .exchange_ts = 1'000'000'000LL,
        .venue_order_id = 101,
        .price = 15000,
        .quantity = 100,
        .side = Side::Buy,
    });

    // Strategy submits passive Buy 50 @ 15000
    OrderRequest req{
        .client_order_id = 5001,
        .side = Side::Buy,
        .type = OrderType::Limit,
        .time_in_force = TimeInForce::Day,
        .symbol = market::make_symbol("AAPL"),
        .price = 15000,
        .quantity = 50,
    };

    OrderAck ack = gateway.submit_order(req);
    EXPECT_EQ(ack.status, GatewayStatus::Accepted);
    EXPECT_EQ(tracker.queue_ahead_of(5001), 100u);

    // Historical execution of 120 against price 15000:
    // 100 consumes historical order 101, 20 fills our simulated order!
    gateway.process_historical_message(L3Message(OrderExecuted{
        .exchange_ts = 1'000'000'500LL,
        .venue_order_id = 101,
        .symbol = market::make_symbol("AAPL"),
        .executed_qty = 120,
        .match_number = 999,
    }));

    // Check fill report delivered
    ASSERT_FALSE(handler.reports.empty());
    const auto& last_report = handler.reports.back();
    EXPECT_EQ(last_report.order_id, 5001u);
    EXPECT_EQ(last_report.status, OrderStatus::PartiallyFilled);
    EXPECT_EQ(last_report.filled_quantity, 20u);
    EXPECT_EQ(last_report.remaining_quantity, 30u);
    EXPECT_EQ(last_report.price, 15000);
}

// ===========================================================================
// Execution Simulator: Aggressive Orders (Market & Crossing Limit)
// ===========================================================================

TEST(ReplayGatewayTest, AggressiveMarketOrderSweepsBook) {
    HistoricalL3Book book;
    QueuePositionTracker tracker;
    EventClock clock(1'000'000'000LL);

    ReplayGateway gateway(book, tracker, clock);
    MockFillHandler handler;
    ASSERT_TRUE(gateway.connect(handler));

    // Two ask levels in historical book:
    // 15050 (qty 40), 15100 (qty 100)
    book.apply_add(OrderAdded{
        .venue_order_id = 201,
        .price = 15050,
        .quantity = 40,
        .side = Side::Sell,
    });
    book.apply_add(OrderAdded{
        .venue_order_id = 202,
        .price = 15100,
        .quantity = 100,
        .side = Side::Sell,
    });

    // Strategy submits Market Buy (price = 0) for 60 shares
    OrderRequest req{
        .client_order_id = 7001,
        .side = Side::Buy,
        .type = OrderType::Limit,
        .time_in_force = TimeInForce::Ioc,
        .symbol = market::make_symbol("AAPL"),
        .price = 0,
        .quantity = 60,
    };

    OrderAck ack = gateway.submit_order(req);
    EXPECT_EQ(ack.status, GatewayStatus::Accepted);

    // Verify fills: 40 filled at 15050, 20 filled at 15100!
    ASSERT_FALSE(handler.reports.empty());
    const auto& fill_report = handler.reports[0];
    EXPECT_EQ(fill_report.order_id, 7001u);
    EXPECT_EQ(fill_report.status, OrderStatus::Filled);
    EXPECT_EQ(fill_report.filled_quantity, 60u);
    EXPECT_EQ(fill_report.remaining_quantity, 0u);

    ASSERT_EQ(fill_report.trades.size(), 2u);
    EXPECT_EQ(fill_report.trades[0].price, 15050);
    EXPECT_EQ(fill_report.trades[0].quantity, 40u);
    EXPECT_EQ(fill_report.trades[1].price, 15100);
    EXPECT_EQ(fill_report.trades[1].quantity, 20u);

    // Verify historical book was updated (40 consumed at 15050, 20 at 15100)
    EXPECT_EQ(book.best_ask_price(), 15100);
    EXPECT_EQ(book.best_ask_quantity(), 80u);  // 100 - 20
    EXPECT_EQ(book.total_ask_volume(), 80u);
}

// ===========================================================================
// Wiring to Portfolio & P&L System
// ===========================================================================

TEST(ReplayGatewayTest, FillsUpdatePortfolioPositionAndPnL) {
    HistoricalL3Book book;
    QueuePositionTracker tracker;
    EventClock clock(1'000'000'000LL);

    ReplayGateway gateway(book, tracker, clock);
    Portfolio portfolio;
    ASSERT_TRUE(gateway.connect(portfolio));

    const auto sym = market::make_symbol("AAPL");

    // Add historical resting ask: 100 @ 150.00
    book.apply_add(OrderAdded{
        .venue_order_id = 1,
        .price = 15000,
        .quantity = 100,
        .side = Side::Sell,
    });

    // 1. Buy 50 shares aggressively @ 15000 (price = 0 for market order)
    EXPECT_EQ(gateway
                  .submit_order(OrderRequest{
                      .client_order_id = 1,
                      .side = Side::Buy,
                      .type = OrderType::Limit,
                      .time_in_force = TimeInForce::Ioc,
                      .symbol = sym,
                      .price = 0,
                      .quantity = 50,
                  })
                  .status,
              GatewayStatus::Accepted);

    const auto* pos = portfolio.find_position(sym);
    ASSERT_NE(pos, nullptr);
    EXPECT_EQ(pos->net_quantity, 50);
    EXPECT_EQ(pos->avg_cost_ticks, 15000);
    EXPECT_EQ(portfolio.total_realized_pnl(), 0);

    // Add historical resting bid: 100 @ 152.00
    book.apply_add(OrderAdded{
        .venue_order_id = 2,
        .price = 15200,
        .quantity = 100,
        .side = Side::Buy,
    });

    // 2. Sell 50 shares aggressively @ 15200 (realizing gain of +$2.00/share = +10000 ticks)
    EXPECT_EQ(gateway
                  .submit_order(OrderRequest{
                      .client_order_id = 2,
                      .side = Side::Sell,
                      .type = OrderType::Limit,
                      .time_in_force = TimeInForce::Ioc,
                      .symbol = sym,
                      .price = 0,
                      .quantity = 50,
                  })
                  .status,
              GatewayStatus::Accepted);

    pos = portfolio.find_position(sym);
    ASSERT_NE(pos, nullptr);
    EXPECT_EQ(pos->net_quantity, 0);  // Flat
    EXPECT_EQ(portfolio.total_realized_pnl(),
              10000);  // 50 shares * 200 ticks = 10,000 ticks ($100.00)
}

// ===========================================================================
// Virtual Latency Pipeline Simulation via Timeline
// ===========================================================================

TEST(ReplayGatewayTest, LatencyTimelineDelaysExecutionAndFill) {
    HistoricalL3Book book;
    QueuePositionTracker tracker;
    EventClock clock(1'000'000'000LL);
    VirtualTimeline timeline(clock);

    LatencyConfig latency{
        .entry_latency_ns = 50'000,     // 50 us wire latency to venue
        .response_latency_ns = 30'000,  // 30 us wire latency for fill back to client
    };

    ReplayGateway gateway(book, tracker, clock, latency, &timeline);
    MockFillHandler handler;
    EXPECT_TRUE(gateway.connect(handler));

    // Book has ask at 15000
    book.apply_add(OrderAdded{
        .venue_order_id = 1,
        .price = 15000,
        .quantity = 100,
        .side = Side::Sell,
    });

    // Submit market order (price = 0) at t = 1,000,000,000
    EXPECT_EQ(gateway
                  .submit_order(OrderRequest{
                      .client_order_id = 8001,
                      .side = Side::Buy,
                      .type = OrderType::Limit,
                      .time_in_force = TimeInForce::Ioc,
                      .price = 0,
                      .quantity = 50,
                  })
                  .status,
              GatewayStatus::Accepted);

    // At t = 1,000,000,000, order has NOT reached the venue yet
    EXPECT_TRUE(handler.reports.empty());
    EXPECT_EQ(book.best_ask_quantity(), 100u);

    // Advance timeline to t = 1,000,050,000 (when order arrives at venue)
    timeline.run_until(1'000'050'000LL);
    EXPECT_EQ(book.best_ask_quantity(), 50u);  // Venue matched!
    // But client handler has NOT received report yet due to response latency!
    EXPECT_TRUE(handler.reports.empty());

    // Advance timeline to t = 1,000,080,000 (venue arrival + response latency)
    timeline.run_until(1'000'080'000LL);
    ASSERT_EQ(handler.reports.size(), 1u);
    EXPECT_EQ(handler.reports[0].order_id, 8001u);
    EXPECT_EQ(handler.reports[0].filled_quantity, 50u);
}

}  // namespace quantengine::replay::test

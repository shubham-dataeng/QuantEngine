#include <gtest/gtest.h>

#include "quantengine/broker/AlpacaGateway.hpp"
#include "quantengine/broker/AlpacaWsFeed.hpp"

namespace quantengine::test {

using namespace quantengine::broker;
using namespace quantengine::core;
using namespace quantengine::execution;
using namespace quantengine::market;

// Mock event handler for AlpacaWsFeed
class TestFeedHandler final : public EventHandlerBase {
public:
    void on_event(const MarketEvent& event) noexcept override { events.push_back(event); }
    void on_connected() noexcept override { connected = true; }
    void on_disconnected() noexcept override { connected = false; }
    void on_error(std::string_view reason) noexcept override {
        errors.push_back(std::string(reason));
    }

    std::vector<MarketEvent> events;
    std::vector<std::string> errors;
    bool connected{false};
};

// Mock fill handler for AlpacaGateway
class TestFillHandler final : public IFillHandler {
public:
    void on_fill(const ExecutionReport& report) noexcept override { fills.push_back(report); }
    void on_gateway_connected() noexcept override { connected = true; }
    void on_gateway_disconnected(std::string_view) noexcept override { connected = false; }

    std::vector<ExecutionReport> fills;
    bool connected{false};
};

// ---------------------------------------------------------------------------
// AlpacaWsFeedTest (M9 & M12)
// ---------------------------------------------------------------------------
TEST(AlpacaWsFeedTest, ParseQuotesAndTicksFromJson) {
    AlpacaWsFeed feed;
    TestFeedHandler handler;

    EXPECT_EQ(feed.subscribe(handler, "AAPL"), FeedStatus::Ok);
    EXPECT_EQ(feed.start(), FeedStatus::Ok);
    EXPECT_TRUE(handler.connected);

    // Feed raw JSON quotes and trades
    std::string raw =
        "[{\"T\":\"q\",\"S\":\"AAPL\",\"bp\":150.25,\"ap\":150.30,\"bs\":100,\"as\":200,\"t\":"
        "1672531199000},"
        "{\"T\":\"t\",\"S\":\"AAPL\",\"p\":150.28,\"s\":50,\"t\":1672531200000}]";

    EXPECT_TRUE(feed.process_raw_message(raw));
    EXPECT_EQ(feed.messages_received(), 1u);
    EXPECT_EQ(feed.quotes_dispatched(), 1u);
    EXPECT_EQ(feed.trades_dispatched(), 1u);
    ASSERT_EQ(handler.events.size(), 2u);

    // Verify Quote
    ASSERT_TRUE(std::holds_alternative<Quote>(handler.events[0]));
    const auto& q = std::get<Quote>(handler.events[0]);
    EXPECT_EQ(symbol_view(q.symbol), "AAPL");
    EXPECT_EQ(q.bid_price, 15025);
    EXPECT_EQ(q.ask_price, 15030);
    EXPECT_EQ(q.bid_size, 100u);
    EXPECT_EQ(q.ask_size, 200u);

    // Verify Tick
    ASSERT_TRUE(std::holds_alternative<Tick>(handler.events[1]));
    const auto& t = std::get<Tick>(handler.events[1]);
    EXPECT_EQ(symbol_view(t.symbol), "AAPL");
    EXPECT_EQ(t.price, 15028);
    EXPECT_EQ(t.quantity, 50u);

    (void)feed.stop();
    EXPECT_FALSE(handler.connected);
}

TEST(AlpacaWsFeedTest, GapDetectionAndReconnectSimulation) {
    AlpacaWsFeed feed;
    TestFeedHandler handler;

    (void)feed.subscribe(handler, "TSLA");
    (void)feed.start();

    // Normal message with seq=1
    feed.process_raw_message(
        "{\"T\":\"t\",\"S\":\"TSLA\",\"p\":200.0,\"s\":10,\"seq\":1,\"t\":100}");
    EXPECT_EQ(feed.gaps_detected(), 0u);

    // Sequence jump to seq=5 (gap detected!)
    feed.process_raw_message(
        "{\"T\":\"t\",\"S\":\"TSLA\",\"p\":201.0,\"s\":10,\"seq\":5,\"t\":200}");
    EXPECT_EQ(feed.gaps_detected(), 1u);
    ASSERT_FALSE(handler.errors.empty());
    EXPECT_EQ(handler.errors.back(), "SEQUENCE_GAP_DETECTED");

    // Simulate network disconnect and reconnect (M12)
    feed.simulate_disconnect("TIMEOUT");
    EXPECT_FALSE(handler.connected);

    EXPECT_TRUE(feed.reconnect());
    EXPECT_TRUE(handler.connected);
    EXPECT_EQ(feed.reconnect_count(), 1u);

    (void)feed.stop();
}

// ---------------------------------------------------------------------------
// AlpacaGatewayTest (M10 & M11)
// ---------------------------------------------------------------------------
TEST(AlpacaGatewayTest, SubmitOrderAndReceiveRestingAck) {
    AlpacaGateway gateway;
    TestFillHandler fill_handler;

    EXPECT_FALSE(gateway.is_connected());
    EXPECT_TRUE(gateway.connect(fill_handler));
    EXPECT_TRUE(gateway.is_connected());
    EXPECT_TRUE(fill_handler.connected);

    OrderRequest req{};
    req.client_order_id = 777;
    req.side = Side::Buy;
    req.type = OrderType::Limit;
    req.price = 25000;
    req.quantity = 50;
    req.symbol = make_symbol("SPY");

    const auto ack = gateway.submit_order(req);
    EXPECT_TRUE(ack.accepted());
    EXPECT_EQ(ack.client_order_id, 777u);

    // Simulated venue ACK transitions order to Resting
    EXPECT_EQ(gateway.state_machine().resting_count(), 1u);
    ASSERT_EQ(fill_handler.fills.size(), 1u);
    EXPECT_EQ(fill_handler.fills[0].status, OrderStatus::Resting);

    // Format JSON check
    const auto json = AlpacaGateway::format_order_json(req);
    EXPECT_NE(json.find("\"symbol\":\"SPY\""), std::string::npos);
    EXPECT_NE(json.find("\"qty\":\"50\""), std::string::npos);
    EXPECT_NE(json.find("\"side\":\"buy\""), std::string::npos);

    // Cancel order
    CancelRequest cancel_req{.client_order_id = 777};
    const auto cancel_ack = gateway.cancel_order(cancel_req);
    EXPECT_TRUE(cancel_ack.accepted());
    EXPECT_EQ(gateway.state_machine().resting_count(), 0u);
    EXPECT_EQ(fill_handler.fills.back().status, OrderStatus::Cancelled);

    gateway.disconnect();
}

TEST(AlpacaGatewayTest, VenueFillInjectionAndReconciliation) {
    AlpacaGateway gateway;
    TestFillHandler fill_handler;
    ASSERT_TRUE(gateway.connect(fill_handler));

    OrderRequest req{};
    req.client_order_id = 888;
    req.side = Side::Sell;
    req.price = 10000;
    req.quantity = 100;
    req.symbol = make_symbol("QQQ");

    (void)gateway.submit_order(req);

    // Inject venue partial fill (40 shares)
    gateway.inject_venue_fill(888, 10000, 40);
    EXPECT_EQ(fill_handler.fills.back().status, OrderStatus::PartiallyFilled);
    EXPECT_EQ(fill_handler.fills.back().filled_quantity, 40u);

    // Inject final fill (60 shares)
    gateway.inject_venue_fill(888, 10000, 60);
    EXPECT_EQ(fill_handler.fills.back().status, OrderStatus::Filled);
    EXPECT_EQ(fill_handler.fills.back().filled_quantity, 100u);

    // Terminal order purge / reconciliation (M11)
    EXPECT_EQ(gateway.reconcile_open_orders(), 1u);
    EXPECT_EQ(gateway.state_machine().active_count(), 0u);

    gateway.disconnect();
}

}  // namespace quantengine::test

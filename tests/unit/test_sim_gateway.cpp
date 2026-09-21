#include <gtest/gtest.h>

#include "quantengine/execution/IExecutionGateway.hpp"
#include "quantengine/execution/SimGateway.hpp"
#include "quantengine/market/MarketEvent.hpp"

// =============================================================================
// SimGatewayTest — Integration tests for the SimGateway backed by
// OptimizedMatchingEngine. Verifies the full IExecutionGateway contract.
// =============================================================================

namespace quantengine::test {

using namespace quantengine::core;
using namespace quantengine::execution;
using namespace quantengine::market;

// ---------------------------------------------------------------------------
// RecordingFillHandler: captures all on_fill() callbacks for assertion.
// ---------------------------------------------------------------------------
class RecordingFillHandler final : public IFillHandler {
public:
    static constexpr std::size_t kMaxFills = 128;

    void on_fill(const ExecutionReport& rpt) noexcept override {
        if (fill_count_ < kMaxFills) {
            fills_[fill_count_++] = rpt;
        }
    }
    void on_gateway_connected() noexcept override { connected_ = true; }
    void on_gateway_disconnected(std::string_view) noexcept override { connected_ = false; }

    [[nodiscard]] auto fill_count() const noexcept -> std::size_t { return fill_count_; }
    [[nodiscard]] auto at(std::size_t i) const noexcept -> const ExecutionReport& {
        return fills_[i];
    }
    [[nodiscard]] auto last() const noexcept -> const ExecutionReport& {
        return fills_[fill_count_ - 1];
    }
    [[nodiscard]] auto is_connected() const noexcept -> bool { return connected_; }
    void reset() noexcept {
        fill_count_ = 0;
        connected_ = false;
    }

private:
    ExecutionReport fills_[kMaxFills]{};
    std::size_t fill_count_{0};
    bool connected_{false};
};

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------
class SimGatewayTest : public ::testing::Test {
protected:
    SimGateway gw_;
    RecordingFillHandler fh_;

    void SetUp() override {
        ASSERT_TRUE(gw_.connect(fh_));
        ASSERT_TRUE(gw_.is_connected());
        ASSERT_TRUE(fh_.is_connected());
    }
    void TearDown() override {
        gw_.disconnect();
        gw_.reset();
        fh_.reset();
    }

    [[nodiscard]] static auto req(OrderId id, Side side, PriceTicks px, Quantity qty,
                                  TimeInForce tif = TimeInForce::Day) noexcept -> OrderRequest {
        OrderRequest r{};
        r.client_order_id = id;
        r.side = side;
        r.price = px;
        r.quantity = qty;
        r.type = OrderType::Limit;
        r.time_in_force = tif;
        r.symbol = make_symbol("AAPL");
        return r;
    }
};

// ---------------------------------------------------------------------------
// Connection lifecycle
// ---------------------------------------------------------------------------
TEST_F(SimGatewayTest, ConnectFires_on_gateway_connected) {
    EXPECT_TRUE(fh_.is_connected());
}

TEST_F(SimGatewayTest, DoubleConnectReturnsFalse) {
    RecordingFillHandler fh2;
    EXPECT_FALSE(gw_.connect(fh2));  // already connected
}

TEST_F(SimGatewayTest, DisconnectFires_on_gateway_disconnected) {
    gw_.disconnect();
    EXPECT_FALSE(fh_.is_connected());
    EXPECT_FALSE(gw_.is_connected());
    // Reconnect for TearDown
    ASSERT_TRUE(gw_.connect(fh_));
}

TEST_F(SimGatewayTest, SubmitWhenNotConnectedReturnsNotConnected) {
    gw_.disconnect();
    const auto ack = gw_.submit_order(req(1, Side::Buy, 10000, 100));
    EXPECT_EQ(ack.status, GatewayStatus::NotConnected);
    // Reconnect for TearDown
    ASSERT_TRUE(gw_.connect(fh_));
}

// ---------------------------------------------------------------------------
// Order submission — passive (no opposing book)
// ---------------------------------------------------------------------------
TEST_F(SimGatewayTest, PassiveBuyRestingReturnsAccepted) {
    const auto ack = gw_.submit_order(req(1, Side::Buy, 10000, 100));
    EXPECT_EQ(ack.status, GatewayStatus::Accepted);
    EXPECT_EQ(ack.client_order_id, 1u);

    // on_fill fires with Resting report
    ASSERT_EQ(fh_.fill_count(), 1u);
    const auto& rpt = fh_.last();
    EXPECT_EQ(rpt.order_id, 1u);
    EXPECT_EQ(rpt.status, OrderStatus::Resting);
    EXPECT_EQ(rpt.remaining_quantity, 100u);
    EXPECT_EQ(rpt.filled_quantity, 0u);
    EXPECT_TRUE(rpt.trades.empty());
}

TEST_F(SimGatewayTest, PassiveSellRestingReturnsAccepted) {
    const auto ack = gw_.submit_order(req(1, Side::Sell, 10500, 50));
    EXPECT_EQ(ack.status, GatewayStatus::Accepted);
    ASSERT_EQ(fh_.fill_count(), 1u);
    EXPECT_EQ(fh_.last().status, OrderStatus::Resting);
    EXPECT_EQ(fh_.last().remaining_quantity, 50u);
}

// ---------------------------------------------------------------------------
// Order submission — aggressive (crosses book → immediate fill)
// ---------------------------------------------------------------------------
TEST_F(SimGatewayTest, AggressiveBuyFullyFillsRestingSell) {
    // Seed: resting sell at 10500 qty 20
    ASSERT_EQ(gw_.submit_order(req(1, Side::Sell, 10500, 20)).status, GatewayStatus::Accepted);
    fh_.reset();

    // Aggressive buy at 10500 qty 20
    const auto ack = gw_.submit_order(req(2, Side::Buy, 10500, 20));
    EXPECT_EQ(ack.status, GatewayStatus::Accepted);

    // Expect exactly one on_fill: the buyer's Filled report (with trade)
    ASSERT_EQ(fh_.fill_count(), 1u);
    const auto& rpt = fh_.last();
    EXPECT_EQ(rpt.order_id, 2u);
    EXPECT_EQ(rpt.status, OrderStatus::Filled);
    EXPECT_EQ(rpt.filled_quantity, 20u);
    EXPECT_EQ(rpt.remaining_quantity, 0u);
    ASSERT_EQ(rpt.trades.size(), 1u);
    EXPECT_EQ(rpt.trades[0].maker_order_id, 1u);
    EXPECT_EQ(rpt.trades[0].taker_order_id, 2u);
    EXPECT_EQ(rpt.trades[0].price, 10500);
    EXPECT_EQ(rpt.trades[0].quantity, 20u);
}

TEST_F(SimGatewayTest, AggressiveBuyPartialFillLeavesResidualResting) {
    // Seed: resting sell at 10500 qty 10
    ASSERT_EQ(gw_.submit_order(req(1, Side::Sell, 10500, 10)).status, GatewayStatus::Accepted);
    fh_.reset();

    // Aggressive buy qty 30: fills 10, rests 20
    const auto ack = gw_.submit_order(req(2, Side::Buy, 10500, 30));
    EXPECT_EQ(ack.status, GatewayStatus::Accepted);

    ASSERT_EQ(fh_.fill_count(), 1u);
    const auto& rpt = fh_.last();
    // Engine returns PartiallyFilled status when some quantity matched and rest rests
    EXPECT_EQ(rpt.order_id, 2u);
    EXPECT_EQ(rpt.filled_quantity, 10u);
    EXPECT_EQ(rpt.remaining_quantity, 20u);
    ASSERT_EQ(rpt.trades.size(), 1u);
}

// ---------------------------------------------------------------------------
// IOC (Immediate-Or-Cancel)
// ---------------------------------------------------------------------------
TEST_F(SimGatewayTest, IocOrderFullyFilled) {
    ASSERT_EQ(gw_.submit_order(req(1, Side::Sell, 10000, 50)).status, GatewayStatus::Accepted);
    fh_.reset();

    // IOC buy at 10000 qty 50: fully matches — no cancel needed
    const auto ack = gw_.submit_order(req(2, Side::Buy, 10000, 50, TimeInForce::Ioc));
    EXPECT_EQ(ack.status, GatewayStatus::Accepted);
    // Only the Filled report: no spurious Cancelled
    ASSERT_EQ(fh_.fill_count(), 1u);
    EXPECT_EQ(fh_.last().status, OrderStatus::Filled);
}

TEST_F(SimGatewayTest, IocOrderPartiallyFilledRestCancelled) {
    ASSERT_EQ(gw_.submit_order(req(1, Side::Sell, 10000, 10)).status, GatewayStatus::Accepted);
    fh_.reset();

    // IOC buy qty 30: 10 fills, 20 cancelled
    const auto ack = gw_.submit_order(req(2, Side::Buy, 10000, 30, TimeInForce::Ioc));
    EXPECT_EQ(ack.status, GatewayStatus::Accepted);

    // Two on_fill calls: PartiallyFilled (with trade) + Cancelled (residual)
    ASSERT_EQ(fh_.fill_count(), 2u);
    EXPECT_EQ(fh_.at(0).filled_quantity, 10u);
    EXPECT_EQ(fh_.at(1).status, OrderStatus::Cancelled);
}

TEST_F(SimGatewayTest, IocOrderNoMatchFullyCancelled) {
    // Empty book — IOC finds nothing to fill
    const auto ack = gw_.submit_order(req(1, Side::Buy, 10000, 100, TimeInForce::Ioc));
    EXPECT_EQ(ack.status, GatewayStatus::Accepted);

    // Resting report then Cancelled report
    ASSERT_EQ(fh_.fill_count(), 2u);
    EXPECT_EQ(fh_.at(0).status, OrderStatus::Resting);
    EXPECT_EQ(fh_.at(1).status, OrderStatus::Cancelled);
}

// ---------------------------------------------------------------------------
// Cancel
// ---------------------------------------------------------------------------
TEST_F(SimGatewayTest, CancelRestingOrder) {
    ASSERT_EQ(gw_.submit_order(req(1, Side::Buy, 10000, 100)).status, GatewayStatus::Accepted);
    fh_.reset();

    CancelRequest cr{};
    cr.client_order_id = 1;
    cr.symbol = make_symbol("AAPL");
    const auto ack = gw_.cancel_order(cr);
    EXPECT_EQ(ack.status, GatewayStatus::Accepted);

    ASSERT_EQ(fh_.fill_count(), 1u);
    EXPECT_EQ(fh_.last().status, OrderStatus::Cancelled);
    EXPECT_EQ(fh_.last().order_id, 1u);
}

TEST_F(SimGatewayTest, CancelUnknownOrderReturnsRejected) {
    CancelRequest cr{};
    cr.client_order_id = 999;
    const auto ack = gw_.cancel_order(cr);
    EXPECT_EQ(ack.status, GatewayStatus::Rejected);
    EXPECT_EQ(ack.reject_reason, RejectReason::UnknownOrder);
}

// ---------------------------------------------------------------------------
// Modify
// ---------------------------------------------------------------------------
TEST_F(SimGatewayTest, ModifyPricePassiveAccepted) {
    ASSERT_EQ(gw_.submit_order(req(1, Side::Buy, 9000, 100)).status, GatewayStatus::Accepted);
    fh_.reset();

    ModifyRequest mr{};
    mr.client_order_id = 1;
    mr.new_price = 9500;
    mr.new_quantity = 100;
    const auto ack = gw_.modify_order(mr);
    EXPECT_EQ(ack.status, GatewayStatus::Accepted);

    // Resting report at new price
    ASSERT_EQ(fh_.fill_count(), 1u);
    EXPECT_EQ(fh_.last().status, OrderStatus::Resting);
    EXPECT_EQ(fh_.last().price, 9500);
}

TEST_F(SimGatewayTest, ModifyCrossesSpreadIsRejectedAtGateway) {
    ASSERT_EQ(gw_.submit_order(req(1, Side::Sell, 10000, 20)).status, GatewayStatus::Accepted);
    ASSERT_EQ(gw_.submit_order(req(2, Side::Buy, 9500, 50)).status, GatewayStatus::Accepted);
    fh_.reset();

    ModifyRequest mr{};
    mr.client_order_id = 2;
    mr.new_price = 10000;  // crosses the sell at 10000
    mr.new_quantity = 50;
    const auto ack = gw_.modify_order(mr);
    EXPECT_EQ(ack.status, GatewayStatus::Rejected);
    EXPECT_EQ(ack.reject_reason, RejectReason::ModifyCrossesSpread);
}

// ---------------------------------------------------------------------------
// Engine state visible via gateway_name and engine accessor
// ---------------------------------------------------------------------------
TEST_F(SimGatewayTest, GatewayName) {
    EXPECT_EQ(gw_.gateway_name(), "SimGateway");
}

TEST_F(SimGatewayTest, EngineBookReflectsSubmittedOrders) {
    ASSERT_EQ(gw_.submit_order(req(1, Side::Buy, 9000, 100)).status, GatewayStatus::Accepted);
    ASSERT_EQ(gw_.submit_order(req(2, Side::Sell, 10000, 50)).status, GatewayStatus::Accepted);

    EXPECT_EQ(gw_.engine().book().total_orders(), 2u);
    EXPECT_EQ(gw_.engine().book().best_bid_price(), 9000);
    EXPECT_EQ(gw_.engine().book().best_ask_price(), 10000);
}

// ---------------------------------------------------------------------------
// Invalid request validation
// ---------------------------------------------------------------------------
TEST_F(SimGatewayTest, ZeroPriceRejected) {
    const auto ack = gw_.submit_order(req(1, Side::Buy, 0, 100));
    EXPECT_EQ(ack.status, GatewayStatus::Rejected);
    EXPECT_EQ(ack.reject_reason, RejectReason::InvalidPrice);
}

TEST_F(SimGatewayTest, ZeroQuantityRejected) {
    const auto ack = gw_.submit_order(req(1, Side::Buy, 10000, 0));
    EXPECT_EQ(ack.status, GatewayStatus::Rejected);
    EXPECT_EQ(ack.reject_reason, RejectReason::InvalidQuantity);
}

}  // namespace quantengine::test

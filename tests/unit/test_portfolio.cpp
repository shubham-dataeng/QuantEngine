#include <gtest/gtest.h>

#include "quantengine/market/MarketEvent.hpp"
#include "quantengine/portfolio/Portfolio.hpp"

namespace quantengine::test {

using namespace quantengine::core;
using namespace quantengine::market;
using namespace quantengine::portfolio;

class PortfolioTest : public ::testing::Test {
protected:
    Portfolio portfolio_;
    SymbolArray aapl_ = make_symbol("AAPL");
    SymbolArray msft_ = make_symbol("MSFT");

    void SetUp() override { portfolio_.reset(); }

    [[nodiscard]] static auto make_fill_report(
        OrderId order_id, Side side, PriceTicks price, Quantity qty,
        OrderStatus status = OrderStatus::Filled) -> ExecutionReport {
        ExecutionReport rpt{};
        rpt.order_id = order_id;
        rpt.status = status;
        rpt.remaining_quantity = (status == OrderStatus::Filled) ? 0 : 50;
        rpt.filled_quantity = qty;
        rpt.price = price;
        rpt.side = side;

        // Populate trade
        Trade t{};
        t.trade_id = 1;
        // Assume taker is this order
        t.taker_order_id = order_id;
        t.maker_order_id = 9999;
        // If taker is Buy, maker side was Sell
        t.maker_side = (side == Side::Buy) ? Side::Sell : Side::Buy;
        t.price = price;
        t.quantity = qty;
        rpt.trades.push_back(t);

        return rpt;
    }
};

// ---------------------------------------------------------------------------
// Basic Position Opening & Inquiries
// ---------------------------------------------------------------------------

TEST_F(PortfolioTest, InitialStateEmpty) {
    EXPECT_EQ(portfolio_.total_realized_pnl(), 0);
    EXPECT_EQ(portfolio_.total_unrealized_pnl(), 0);
    EXPECT_EQ(portfolio_.open_order_count(), 0u);
    EXPECT_EQ(portfolio_.find_position(aapl_), nullptr);

    const auto snap = portfolio_.snapshot_for(aapl_);
    EXPECT_EQ(snap.session_realized_pnl, 0);
    EXPECT_EQ(snap.session_unrealized_pnl, 0);
    EXPECT_EQ(snap.gross_notional_exposure, 0);
    EXPECT_EQ(snap.net_position, 0u);
}

TEST_F(PortfolioTest, OpenLongPosition) {
    const auto rpt = make_fill_report(1, Side::Buy, 15000, 100);
    portfolio_.apply_fill_with_symbol(rpt, aapl_);

    const auto* pos = portfolio_.find_position(aapl_);
    ASSERT_NE(pos, nullptr);
    EXPECT_EQ(pos->net_quantity, 100);
    EXPECT_EQ(pos->avg_cost_ticks, 15000);
    EXPECT_EQ(pos->realized_pnl, 0);

    const auto snap = portfolio_.snapshot_for(aapl_);
    EXPECT_EQ(snap.net_position, 100u);
    EXPECT_EQ(snap.session_realized_pnl, 0);
}

TEST_F(PortfolioTest, OpenShortPosition) {
    const auto rpt = make_fill_report(1, Side::Sell, 15000, 50);
    portfolio_.apply_fill_with_symbol(rpt, aapl_);

    const auto* pos = portfolio_.find_position(aapl_);
    ASSERT_NE(pos, nullptr);
    EXPECT_EQ(pos->net_quantity, -50);
    EXPECT_EQ(pos->avg_cost_ticks, 15000);
    EXPECT_EQ(pos->realized_pnl, 0);

    const auto snap = portfolio_.snapshot_for(aapl_);
    EXPECT_EQ(static_cast<std::int64_t>(snap.net_position), -50);
}

// ---------------------------------------------------------------------------
// Average Cost Accounting (Adding to Position)
// ---------------------------------------------------------------------------

TEST_F(PortfolioTest, AddToLongUpdatesAverageCost) {
    // Buy 100 @ 100
    portfolio_.apply_fill_with_symbol(make_fill_report(1, Side::Buy, 100, 100), aapl_);
    // Buy 100 @ 200 -> total 200 @ avg 150
    portfolio_.apply_fill_with_symbol(make_fill_report(2, Side::Buy, 200, 100), aapl_);

    const auto* pos = portfolio_.find_position(aapl_);
    ASSERT_NE(pos, nullptr);
    EXPECT_EQ(pos->net_quantity, 200);
    EXPECT_EQ(pos->avg_cost_ticks, 150);
    EXPECT_EQ(pos->realized_pnl, 0);
}

TEST_F(PortfolioTest, AddToShortUpdatesAverageCost) {
    // Sell 100 @ 200
    portfolio_.apply_fill_with_symbol(make_fill_report(1, Side::Sell, 200, 100), aapl_);
    // Sell 100 @ 100 -> total -200 @ avg 150
    portfolio_.apply_fill_with_symbol(make_fill_report(2, Side::Sell, 100, 100), aapl_);

    const auto* pos = portfolio_.find_position(aapl_);
    ASSERT_NE(pos, nullptr);
    EXPECT_EQ(pos->net_quantity, -200);
    EXPECT_EQ(pos->avg_cost_ticks, 150);
    EXPECT_EQ(pos->realized_pnl, 0);
}

// ---------------------------------------------------------------------------
// Realizing PnL (Partial Close & Full Close)
// ---------------------------------------------------------------------------

TEST_F(PortfolioTest, PartiallyCloseLongRealizesGain) {
    // Buy 100 @ 100
    portfolio_.apply_fill_with_symbol(make_fill_report(1, Side::Buy, 100, 100), aapl_);
    // Sell 40 @ 120 -> gain of 20 * 40 = 800
    portfolio_.apply_fill_with_symbol(make_fill_report(2, Side::Sell, 120, 40), aapl_);

    const auto* pos = portfolio_.find_position(aapl_);
    ASSERT_NE(pos, nullptr);
    EXPECT_EQ(pos->net_quantity, 60);
    EXPECT_EQ(pos->avg_cost_ticks, 100);
    EXPECT_EQ(pos->realized_pnl, 800);
    EXPECT_EQ(portfolio_.total_realized_pnl(), 800);
}

TEST_F(PortfolioTest, FullyCloseLongRealizesLossAndResetsCost) {
    // Buy 100 @ 100
    portfolio_.apply_fill_with_symbol(make_fill_report(1, Side::Buy, 100, 100), aapl_);
    // Sell 100 @ 80 -> loss of -20 * 100 = -2000
    portfolio_.apply_fill_with_symbol(make_fill_report(2, Side::Sell, 80, 100), aapl_);

    const auto* pos = portfolio_.find_position(aapl_);
    ASSERT_NE(pos, nullptr);
    EXPECT_EQ(pos->net_quantity, 0);
    EXPECT_EQ(pos->avg_cost_ticks, 0);
    EXPECT_EQ(pos->realized_pnl, -2000);
    EXPECT_EQ(portfolio_.total_realized_pnl(), -2000);
}

TEST_F(PortfolioTest, PartiallyCloseShortRealizesGain) {
    // Sell 100 @ 150
    portfolio_.apply_fill_with_symbol(make_fill_report(1, Side::Sell, 150, 100), aapl_);
    // Buy 50 @ 130 -> gain of (150 - 130) * 50 = 1000
    portfolio_.apply_fill_with_symbol(make_fill_report(2, Side::Buy, 130, 50), aapl_);

    const auto* pos = portfolio_.find_position(aapl_);
    ASSERT_NE(pos, nullptr);
    EXPECT_EQ(pos->net_quantity, -50);
    EXPECT_EQ(pos->avg_cost_ticks, 150);
    EXPECT_EQ(pos->realized_pnl, 1000);
}

// ---------------------------------------------------------------------------
// Position Reversal (Long -> Short & Short -> Long)
// ---------------------------------------------------------------------------

TEST_F(PortfolioTest, ReverseLongToShort) {
    // Buy 50 @ 100
    portfolio_.apply_fill_with_symbol(make_fill_report(1, Side::Buy, 100, 50), aapl_);
    // Sell 80 @ 120 -> closes 50 @ 120 (gain = 20 * 50 = 1000), opens short 30 @ 120
    portfolio_.apply_fill_with_symbol(make_fill_report(2, Side::Sell, 120, 80), aapl_);

    const auto* pos = portfolio_.find_position(aapl_);
    ASSERT_NE(pos, nullptr);
    EXPECT_EQ(pos->net_quantity, -30);
    EXPECT_EQ(pos->avg_cost_ticks, 120);
    EXPECT_EQ(pos->realized_pnl, 1000);
}

TEST_F(PortfolioTest, ReverseShortToLong) {
    // Sell 50 @ 100
    portfolio_.apply_fill_with_symbol(make_fill_report(1, Side::Sell, 100, 50), aapl_);
    // Buy 70 @ 90 -> closes 50 @ 90 (gain = 10 * 50 = 500), opens long 20 @ 90
    portfolio_.apply_fill_with_symbol(make_fill_report(2, Side::Buy, 90, 70), aapl_);

    const auto* pos = portfolio_.find_position(aapl_);
    ASSERT_NE(pos, nullptr);
    EXPECT_EQ(pos->net_quantity, 20);
    EXPECT_EQ(pos->avg_cost_ticks, 90);
    EXPECT_EQ(pos->realized_pnl, 500);
}

// ---------------------------------------------------------------------------
// Mark-To-Market & Unrealized PnL
// ---------------------------------------------------------------------------

TEST_F(PortfolioTest, MarkToMarketLong) {
    // Buy 100 @ 100
    portfolio_.apply_fill_with_symbol(make_fill_report(1, Side::Buy, 100, 100), aapl_);

    portfolio_.mark_to_market(aapl_, 105);
    const auto* pos = portfolio_.find_position(aapl_);
    ASSERT_NE(pos, nullptr);
    EXPECT_EQ(pos->last_price, 105);
    EXPECT_EQ(pos->unrealized_pnl, 500);  // (105 - 100) * 100
    EXPECT_EQ(portfolio_.total_unrealized_pnl(), 500);

    const auto snap = portfolio_.snapshot_for(aapl_);
    EXPECT_EQ(snap.session_unrealized_pnl, 500);
    EXPECT_EQ(snap.gross_notional_exposure, 105 * 100);
}

TEST_F(PortfolioTest, MarkToMarketShort) {
    // Sell 100 @ 100
    portfolio_.apply_fill_with_symbol(make_fill_report(1, Side::Sell, 100, 100), aapl_);

    portfolio_.mark_to_market(aapl_, 95);
    const auto* pos = portfolio_.find_position(aapl_);
    ASSERT_NE(pos, nullptr);
    EXPECT_EQ(pos->last_price, 95);
    EXPECT_EQ(pos->unrealized_pnl, 500);  // (100 - 95) * 100

    portfolio_.mark_to_market(aapl_, 105);
    EXPECT_EQ(pos->unrealized_pnl, -500);  // (100 - 105) * 100
}

// ---------------------------------------------------------------------------
// Multi-Symbol Exposure & Snapshots
// ---------------------------------------------------------------------------

TEST_F(PortfolioTest, MultiSymbolPortfolioSnapshot) {
    portfolio_.apply_fill_with_symbol(make_fill_report(1, Side::Buy, 100, 50), aapl_);
    portfolio_.apply_fill_with_symbol(make_fill_report(2, Side::Sell, 200, 30), msft_);

    portfolio_.mark_to_market(aapl_, 110);
    portfolio_.mark_to_market(msft_, 190);

    // AAPL: realized 0, unrealized (110 - 100)*50 = 500, notional 110*50 = 5500
    // MSFT: realized 0, unrealized (200 - 190)*30 = 300, notional 190*30 = 5700
    // Total gross notional = 5500 + 5700 = 11200
    // Total unrealized = 500 + 300 = 800

    const auto snap_aapl = portfolio_.snapshot_for(aapl_);
    EXPECT_EQ(snap_aapl.net_position, 50u);
    EXPECT_EQ(snap_aapl.session_unrealized_pnl, 800);
    EXPECT_EQ(snap_aapl.gross_notional_exposure, 11200);

    const auto snap_msft = portfolio_.snapshot_for(msft_);
    EXPECT_EQ(static_cast<std::int64_t>(snap_msft.net_position), -30);
    EXPECT_EQ(snap_msft.gross_notional_exposure, 11200);

    const auto agg_snap = portfolio_.snapshot();
    EXPECT_EQ(agg_snap.net_position, 80u);  // 50 + 30
    EXPECT_EQ(agg_snap.gross_notional_exposure, 11200);
}

// ---------------------------------------------------------------------------
// Open Order Count Tracking
// ---------------------------------------------------------------------------

TEST_F(PortfolioTest, OpenOrderCountTracking) {
    EXPECT_EQ(portfolio_.open_order_count(), 0u);

    // Resting order report
    ExecutionReport rpt1{};
    rpt1.order_id = 1;
    rpt1.status = OrderStatus::Resting;
    portfolio_.apply_fill_with_symbol(rpt1, aapl_);
    EXPECT_EQ(portfolio_.open_order_count(), 1u);

    // Second resting order
    ExecutionReport rpt2{};
    rpt2.order_id = 2;
    rpt2.status = OrderStatus::Resting;
    portfolio_.apply_fill_with_symbol(rpt2, aapl_);
    EXPECT_EQ(portfolio_.open_order_count(), 2u);

    // One order cancelled
    ExecutionReport rpt_cancel{};
    rpt_cancel.order_id = 1;
    rpt_cancel.status = OrderStatus::Cancelled;
    portfolio_.apply_fill_with_symbol(rpt_cancel, aapl_);
    EXPECT_EQ(portfolio_.open_order_count(), 1u);

    // Other order filled
    portfolio_.apply_fill_with_symbol(make_fill_report(2, Side::Buy, 100, 10), aapl_);
    EXPECT_EQ(portfolio_.open_order_count(), 0u);
}

}  // namespace quantengine::test

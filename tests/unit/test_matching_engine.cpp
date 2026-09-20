#include <gtest/gtest.h>

#include "quantengine/engine/matching_engine.hpp"

namespace quantengine::test {

using namespace quantengine::core;
using namespace quantengine::engine;

class MatchingEngineTest : public ::testing::Test {
protected:
    MatchingEngine engine;
};

TEST_F(MatchingEngineTest, PassiveRestingOrdersNoTrade) {
    auto r1 = engine.submit_order(1, Side::Buy, 10000, 10);
    EXPECT_EQ(r1.status, OrderStatus::Resting);
    EXPECT_EQ(r1.remaining_quantity, 10);
    EXPECT_EQ(r1.filled_quantity, 0);
    EXPECT_TRUE(r1.trades.empty());

    auto r2 = engine.submit_order(2, Side::Sell, 10500, 20);
    EXPECT_EQ(r2.status, OrderStatus::Resting);
    EXPECT_EQ(r2.remaining_quantity, 20);
    EXPECT_EQ(r2.filled_quantity, 0);
    EXPECT_TRUE(r2.trades.empty());

    EXPECT_EQ(engine.book().total_orders(), 2);
    EXPECT_EQ(engine.book().best_bid_price(), 10000);
    EXPECT_EQ(engine.book().best_ask_price(), 10500);
}

TEST_F(MatchingEngineTest, FullFillSingleOrder) {
    EXPECT_EQ(engine.submit_order(1, Side::Sell, 10500, 20).status, OrderStatus::Resting);

    auto report = engine.submit_order(2, Side::Buy, 10500, 20);
    EXPECT_EQ(report.status, OrderStatus::Filled);
    EXPECT_EQ(report.remaining_quantity, 0);
    EXPECT_EQ(report.filled_quantity, 20);
    ASSERT_EQ(report.trades.size(), 1);

    const auto& trade = report.trades[0];
    EXPECT_EQ(trade.trade_id, 1);
    EXPECT_EQ(trade.maker_order_id, 1);
    EXPECT_EQ(trade.taker_order_id, 2);
    EXPECT_EQ(trade.maker_side, Side::Sell);
    EXPECT_EQ(trade.price, 10500);
    EXPECT_EQ(trade.quantity, 20);

    EXPECT_TRUE(engine.book().is_empty());
}

TEST_F(MatchingEngineTest, MakerPriceRule) {
    // Resting sell at 10000
    EXPECT_EQ(engine.submit_order(1, Side::Sell, 10000, 10).status, OrderStatus::Resting);

    // Aggressive incoming buy at 11000
    auto report = engine.submit_order(2, Side::Buy, 11000, 10);
    EXPECT_EQ(report.status, OrderStatus::Filled);
    ASSERT_EQ(report.trades.size(), 1);

    // Trade price must be the maker's resting price (10000), not the taker's price (11000)
    EXPECT_EQ(report.trades[0].price, 10000);
    EXPECT_EQ(report.trades[0].quantity, 10);
    EXPECT_TRUE(engine.book().is_empty());
}

TEST_F(MatchingEngineTest, PartialFillTakerRestsRemaining) {
    EXPECT_EQ(engine.submit_order(1, Side::Sell, 10000, 10).status, OrderStatus::Resting);

    // Incoming buy for 30
    auto report = engine.submit_order(2, Side::Buy, 10000, 30);
    EXPECT_EQ(report.status, OrderStatus::PartiallyFilled);
    EXPECT_EQ(report.filled_quantity, 10);
    EXPECT_EQ(report.remaining_quantity, 20);
    ASSERT_EQ(report.trades.size(), 1);

    // Remaining 20 must rest on the bid book
    EXPECT_EQ(engine.book().total_orders(), 1);
    EXPECT_EQ(engine.book().best_bid_price(), 10000);
    EXPECT_EQ(engine.book().best_bid_quantity(), 20);
    EXPECT_FALSE(engine.book().best_ask_price().has_value());
}

TEST_F(MatchingEngineTest, PartialFillMakerLeavesRestingQuantity) {
    EXPECT_EQ(engine.submit_order(1, Side::Sell, 10000, 50).status, OrderStatus::Resting);

    // Incoming buy for 20
    auto report = engine.submit_order(2, Side::Buy, 10000, 20);
    EXPECT_EQ(report.status, OrderStatus::Filled);
    EXPECT_EQ(report.filled_quantity, 20);
    EXPECT_EQ(report.remaining_quantity, 0);

    // Maker still has 30 resting
    EXPECT_EQ(engine.book().total_orders(), 1);
    EXPECT_EQ(engine.book().best_ask_price(), 10000);
    EXPECT_EQ(engine.book().best_ask_quantity(), 30);
}

TEST_F(MatchingEngineTest, MultiLevelSweep) {
    EXPECT_EQ(engine.submit_order(1, Side::Sell, 10100, 10).status, OrderStatus::Resting);
    EXPECT_EQ(engine.submit_order(2, Side::Sell, 10200, 15).status, OrderStatus::Resting);
    EXPECT_EQ(engine.submit_order(3, Side::Sell, 10300, 20).status, OrderStatus::Resting);

    // Buy sweeping through 10100, 10200, and into 10300
    auto report = engine.submit_order(4, Side::Buy, 10400, 35);
    EXPECT_EQ(report.status, OrderStatus::Filled);
    EXPECT_EQ(report.filled_quantity, 35);
    EXPECT_EQ(report.remaining_quantity, 0);
    ASSERT_EQ(report.trades.size(), 3);

    // Trade 1 @ 10100 for 10
    EXPECT_EQ(report.trades[0].maker_order_id, 1);
    EXPECT_EQ(report.trades[0].price, 10100);
    EXPECT_EQ(report.trades[0].quantity, 10);

    // Trade 2 @ 10200 for 15
    EXPECT_EQ(report.trades[1].maker_order_id, 2);
    EXPECT_EQ(report.trades[1].price, 10200);
    EXPECT_EQ(report.trades[1].quantity, 15);

    // Trade 3 @ 10300 for 10
    EXPECT_EQ(report.trades[2].maker_order_id, 3);
    EXPECT_EQ(report.trades[2].price, 10300);
    EXPECT_EQ(report.trades[2].quantity, 10);

    // Order 3 still has 10 remaining at 10300
    EXPECT_EQ(engine.book().total_orders(), 1);
    EXPECT_EQ(engine.book().best_ask_price(), 10300);
    EXPECT_EQ(engine.book().best_ask_quantity(), 10);
}

TEST_F(MatchingEngineTest, FIFOPriorityAtSamePrice) {
    // Two asks at same price: seq 1 (qty 10), seq 2 (qty 20)
    EXPECT_EQ(engine.submit_order(1, Side::Sell, 10000, 10).status, OrderStatus::Resting);
    EXPECT_EQ(engine.submit_order(2, Side::Sell, 10000, 20).status, OrderStatus::Resting);

    // Incoming buy for 15
    auto report = engine.submit_order(3, Side::Buy, 10000, 15);
    EXPECT_EQ(report.status, OrderStatus::Filled);
    ASSERT_EQ(report.trades.size(), 2);

    // First trade must execute against older Order 1
    EXPECT_EQ(report.trades[0].maker_order_id, 1);
    EXPECT_EQ(report.trades[0].quantity, 10);

    // Second trade against Order 2
    EXPECT_EQ(report.trades[1].maker_order_id, 2);
    EXPECT_EQ(report.trades[1].quantity, 5);

    // Order 2 still has 15 resting
    EXPECT_EQ(engine.book().best_ask_quantity(), 15);
}

TEST_F(MatchingEngineTest, CancellationAndRejections) {
    EXPECT_EQ(engine.submit_order(1, Side::Buy, 10000, 10).status, OrderStatus::Resting);

    // Successful cancel
    auto cr = engine.cancel_order(1);
    EXPECT_EQ(cr.status, OrderStatus::Cancelled);
    EXPECT_EQ(cr.order_id, 1);
    EXPECT_TRUE(engine.book().is_empty());

    // Cancel unknown order
    auto cr_unknown = engine.cancel_order(999);
    EXPECT_EQ(cr_unknown.status, OrderStatus::Rejected);
    EXPECT_EQ(cr_unknown.reject_reason, RejectReason::UnknownOrder);

    // Reject duplicate ID
    EXPECT_EQ(engine.submit_order(10, Side::Buy, 10000, 10).status, OrderStatus::Resting);
    auto dup = engine.submit_order(10, Side::Buy, 10000, 10);
    EXPECT_EQ(dup.status, OrderStatus::Rejected);
    EXPECT_EQ(dup.reject_reason, RejectReason::DuplicateOrderId);

    // Reject invalid price and quantity
    auto bad_px = engine.submit_order(20, Side::Buy, 0, 10);
    EXPECT_EQ(bad_px.status, OrderStatus::Rejected);
    EXPECT_EQ(bad_px.reject_reason, RejectReason::InvalidPrice);

    auto bad_qty = engine.submit_order(30, Side::Buy, 10000, 0);
    EXPECT_EQ(bad_qty.status, OrderStatus::Rejected);
    EXPECT_EQ(bad_qty.reject_reason, RejectReason::InvalidQuantity);
}

TEST_F(MatchingEngineTest, ModifyOrderAndCrossingExecution) {
    // Resting sell at 10500
    EXPECT_EQ(engine.submit_order(1, Side::Sell, 10500, 20).status, OrderStatus::Resting);
    // Resting buy at 9500
    EXPECT_EQ(engine.submit_order(2, Side::Buy, 9500, 10).status, OrderStatus::Resting);

    // Attempt to modify Buy to 10600 — would cross the resting sell at 10500.
    // This is now REJECTED: modify cannot silently become an aggressive order.
    // The caller must explicitly cancel-then-place to achieve crossing execution.
    auto mr = engine.modify_order(2, 10600, 15);
    EXPECT_EQ(mr.status, OrderStatus::Rejected);
    EXPECT_EQ(mr.reject_reason, RejectReason::ModifyCrossesSpread);
    EXPECT_EQ(mr.order_id, 2);

    // Both original orders must remain untouched
    EXPECT_EQ(engine.book().total_orders(), 2);
    EXPECT_EQ(engine.book().best_bid_price(), 9500);
    EXPECT_EQ(engine.book().best_bid_quantity(), 10);
    EXPECT_EQ(engine.book().best_ask_price(), 10500);
    EXPECT_EQ(engine.book().best_ask_quantity(), 20);

    // Explicit cancel-then-place is the supported path for intentional crossing execution
    auto cr = engine.cancel_order(2);
    EXPECT_EQ(cr.status, OrderStatus::Cancelled);

    auto nr = engine.submit_order(3, Side::Buy, 10600, 15);
    EXPECT_EQ(nr.status, OrderStatus::Filled);
    EXPECT_EQ(nr.filled_quantity, 15);
    ASSERT_EQ(nr.trades.size(), 1);
    EXPECT_EQ(nr.trades[0].maker_order_id, 1);
    EXPECT_EQ(nr.trades[0].taker_order_id, 3);
    EXPECT_EQ(nr.trades[0].price, 10500);  // maker price rule
    EXPECT_EQ(nr.trades[0].quantity, 15);

    // Resting sell has 5 remaining
    EXPECT_EQ(engine.book().total_orders(), 1);
    EXPECT_EQ(engine.book().best_ask_quantity(), 5);
}

TEST_F(MatchingEngineTest, ProcessCommandInterface) {
    OrderCommand cmd1{.sequence_number = 100,
                      .payload = CreateOrderCommand{1, Side::Buy, 10000, 50}};
    auto r1 = engine.process_command(cmd1);
    EXPECT_EQ(r1.sequence_number, 100);
    EXPECT_EQ(r1.status, OrderStatus::Resting);

    OrderCommand cmd2{.sequence_number = 101, .payload = ModifyOrderCommand{1, 10000, 40}};
    auto r2 = engine.process_command(cmd2);
    EXPECT_EQ(r2.sequence_number, 101);
    EXPECT_EQ(r2.remaining_quantity, 40);

    OrderCommand cmd3{.sequence_number = 102, .payload = CancelOrderCommand{1}};
    auto r3 = engine.process_command(cmd3);
    EXPECT_EQ(r3.sequence_number, 102);
    EXPECT_EQ(r3.status, OrderStatus::Cancelled);
    EXPECT_TRUE(engine.book().is_empty());
}

// ---------------------------------------------------------------------------
// Milestone 1: modify-crosses-spread guard tests
// ---------------------------------------------------------------------------

// A buy resting at 9500 must not be modifiable to a price >= best ask (10000).
// The original order must remain in the book untouched after rejection.
TEST_F(MatchingEngineTest, ModifyCrossesSpreadBuyIsRejected) {
    // Seed book: ask at 10000, bid at 9500
    ASSERT_EQ(engine.submit_order(1, Side::Sell, 10000, 20).status, OrderStatus::Resting);
    ASSERT_EQ(engine.submit_order(2, Side::Buy, 9500, 50).status, OrderStatus::Resting);
    ASSERT_EQ(engine.book().total_orders(), 2);

    // Attempt to modify bid to 10000 — would cross the best ask
    auto r = engine.modify_order(2, 10000, 50);
    EXPECT_EQ(r.status, OrderStatus::Rejected);
    EXPECT_EQ(r.reject_reason, RejectReason::ModifyCrossesSpread);
    EXPECT_EQ(r.order_id, 2);

    // Original bid must still be resting at its original price and quantity
    EXPECT_EQ(engine.book().total_orders(), 2);
    EXPECT_EQ(engine.book().best_bid_price(), 9500);
    EXPECT_EQ(engine.book().best_bid_quantity(), 50);

    // Existing ask must also be untouched
    EXPECT_EQ(engine.book().best_ask_price(), 10000);
    EXPECT_EQ(engine.book().best_ask_quantity(), 20);
}

// A sell resting at 10500 must not be modifiable to a price <= best bid (10000).
// The original order must remain in the book untouched after rejection.
TEST_F(MatchingEngineTest, ModifyCrossesSpreadSellIsRejected) {
    // Seed book: bid at 10000, ask at 10500
    ASSERT_EQ(engine.submit_order(1, Side::Buy, 10000, 30).status, OrderStatus::Resting);
    ASSERT_EQ(engine.submit_order(2, Side::Sell, 10500, 40).status, OrderStatus::Resting);
    ASSERT_EQ(engine.book().total_orders(), 2);

    // Attempt to modify ask to 10000 — would cross the best bid
    auto r = engine.modify_order(2, 10000, 40);
    EXPECT_EQ(r.status, OrderStatus::Rejected);
    EXPECT_EQ(r.reject_reason, RejectReason::ModifyCrossesSpread);
    EXPECT_EQ(r.order_id, 2);

    // Original ask must still be resting
    EXPECT_EQ(engine.book().total_orders(), 2);
    EXPECT_EQ(engine.book().best_ask_price(), 10500);
    EXPECT_EQ(engine.book().best_ask_quantity(), 40);

    // Existing bid must also be untouched
    EXPECT_EQ(engine.book().best_bid_price(), 10000);
    EXPECT_EQ(engine.book().best_bid_quantity(), 30);
}

// A modify that only changes quantity (same non-crossing price) must complete.
// Quantity reduction must NOT preserve the order's original position in the FIFO queue:
// by cancel+reinsert semantics, the order moves to the tail. This is the documented
// semantic for QuantEngine (all modifies are cancel+reinsert; partial-qty-reduce
// with priority retention would require a separate in-place API).
TEST_F(MatchingEngineTest, ModifyQuantityNonCrossingSucceeds) {
    ASSERT_EQ(engine.submit_order(1, Side::Sell, 10500, 50).status, OrderStatus::Resting);

    // Modify to a smaller quantity at same non-crossing price — must succeed
    auto r = engine.modify_order(1, 10500, 30);
    EXPECT_EQ(r.status, OrderStatus::Resting);
    EXPECT_EQ(r.reject_reason, RejectReason::None);
    EXPECT_EQ(r.remaining_quantity, 30);
    EXPECT_EQ(engine.book().total_orders(), 1);
    EXPECT_EQ(engine.book().best_ask_price(), 10500);
    EXPECT_EQ(engine.book().best_ask_quantity(), 30);
}

// Modify to a passively non-crossing price on an empty opposing side must succeed.
TEST_F(MatchingEngineTest, ModifyNoCrossWhenOpposingSideEmpty) {
    ASSERT_EQ(engine.submit_order(1, Side::Buy, 9000, 10).status, OrderStatus::Resting);

    // No asks exist — any bid price modification is safe
    auto r = engine.modify_order(1, 9500, 10);
    EXPECT_EQ(r.status, OrderStatus::Resting);
    EXPECT_EQ(r.reject_reason, RejectReason::None);
    EXPECT_EQ(engine.book().best_bid_price(), 9500);
}

}  // namespace quantengine::test

#include <gtest/gtest.h>

#include "quantengine/optimized/optimized_order_book.hpp"

namespace quantengine::test {

using namespace quantengine::core;
using namespace quantengine::optimized;

class OptimizedOrderBookTest : public ::testing::Test {
protected:
    OptimizedOrderBook book{8};  // Small initial capacity to stress pool expansion
};

TEST_F(OptimizedOrderBookTest, EmptyBookProperties) {
    EXPECT_TRUE(book.is_empty());
    EXPECT_EQ(book.total_orders(), 0);
    EXPECT_EQ(book.bid_level_count(), 0);
    EXPECT_EQ(book.ask_level_count(), 0);
    EXPECT_EQ(book.total_bid_volume(), 0);
    EXPECT_EQ(book.total_ask_volume(), 0);
    EXPECT_FALSE(book.best_bid_price().has_value());
    EXPECT_FALSE(book.best_ask_price().has_value());
    EXPECT_TRUE(book.check_invariants());
}

TEST_F(OptimizedOrderBookTest, AddSingleOrder) {
    Order buy_order(1, Side::Buy, 10000, 100, 1);
    EXPECT_TRUE(book.add_order(buy_order));

    EXPECT_FALSE(book.is_empty());
    EXPECT_EQ(book.total_orders(), 1);
    EXPECT_EQ(book.bid_level_count(), 1);
    EXPECT_EQ(book.ask_level_count(), 0);
    EXPECT_EQ(book.total_bid_volume(), 100);
    EXPECT_EQ(book.best_bid_price(), 10000);
    EXPECT_EQ(book.best_bid_quantity(), 100);
    EXPECT_TRUE(book.has_order(1));
    EXPECT_TRUE(book.check_invariants());
}

TEST_F(OptimizedOrderBookTest, PricePriorityOrdering) {
    EXPECT_TRUE(book.add_order(Order(1, Side::Buy, 10000, 10, 1)));
    EXPECT_TRUE(book.add_order(Order(2, Side::Buy, 10200, 20, 2)));
    EXPECT_TRUE(book.add_order(Order(3, Side::Buy, 10100, 30, 3)));

    EXPECT_TRUE(book.add_order(Order(4, Side::Sell, 10500, 40, 4)));
    EXPECT_TRUE(book.add_order(Order(5, Side::Sell, 10300, 50, 5)));
    EXPECT_TRUE(book.add_order(Order(6, Side::Sell, 10400, 60, 6)));

    EXPECT_EQ(book.best_bid_price(), 10200);
    EXPECT_EQ(book.best_ask_price(), 10300);

    const auto bids = book.get_bids();
    ASSERT_EQ(bids.size(), 3);
    EXPECT_EQ(bids[0].price, 10200);
    EXPECT_EQ(bids[1].price, 10100);
    EXPECT_EQ(bids[2].price, 10000);

    const auto asks = book.get_asks();
    ASSERT_EQ(asks.size(), 3);
    EXPECT_EQ(asks[0].price, 10300);
    EXPECT_EQ(asks[1].price, 10400);
    EXPECT_EQ(asks[2].price, 10500);

    EXPECT_TRUE(book.check_invariants());
}

TEST_F(OptimizedOrderBookTest, IntrusiveFIFOPriorityAtSamePrice) {
    EXPECT_TRUE(book.add_order(Order(10, Side::Buy, 10000, 100, 1)));
    EXPECT_TRUE(book.add_order(Order(20, Side::Buy, 10000, 200, 2)));
    EXPECT_TRUE(book.add_order(Order(30, Side::Buy, 10000, 300, 3)));

    EXPECT_EQ(book.bid_level_count(), 1);
    EXPECT_EQ(book.total_orders(), 3);
    EXPECT_EQ(book.best_bid_quantity(), 600);

    // Front should be Order 10
    auto* top = book.get_best_bid_order();
    ASSERT_NE(top, nullptr);
    EXPECT_EQ(top->order_id(), 10);

    // Pop front
    book.pop_best_bid_order();
    EXPECT_EQ(book.total_orders(), 2);
    EXPECT_EQ(book.total_bid_volume(), 500);

    // Front should now be Order 20
    top = book.get_best_bid_order();
    ASSERT_NE(top, nullptr);
    EXPECT_EQ(top->order_id(), 20);

    // Pop front
    book.pop_best_bid_order();
    top = book.get_best_bid_order();
    ASSERT_NE(top, nullptr);
    EXPECT_EQ(top->order_id(), 30);

    // Pop last order -> level cleaned up
    book.pop_best_bid_order();
    EXPECT_TRUE(book.is_empty());
    EXPECT_EQ(book.bid_level_count(), 0);
    EXPECT_EQ(book.get_best_bid_order(), nullptr);
    EXPECT_TRUE(book.check_invariants());
}

TEST_F(OptimizedOrderBookTest, CancellationRemovesOrderAndLevel) {
    EXPECT_TRUE(book.add_order(Order(1, Side::Buy, 10000, 100, 1)));
    EXPECT_TRUE(book.add_order(Order(2, Side::Buy, 10000, 200, 2)));
    EXPECT_TRUE(book.add_order(Order(3, Side::Buy, 10100, 300, 3)));

    // Cancel middle order at 10000
    auto cancelled = book.cancel_order(1);
    ASSERT_TRUE(cancelled.has_value());
    EXPECT_EQ(cancelled->order_id(), 1);
    EXPECT_EQ(cancelled->status(), OrderStatus::Cancelled);
    EXPECT_EQ(book.total_orders(), 2);
    EXPECT_EQ(book.total_bid_volume(), 500);
    EXPECT_FALSE(book.has_order(1));

    // Cancel sole order at 10100 -> price level erased
    cancelled = book.cancel_order(3);
    ASSERT_TRUE(cancelled.has_value());
    EXPECT_EQ(cancelled->order_id(), 3);
    EXPECT_EQ(book.bid_level_count(), 1);
    EXPECT_EQ(book.best_bid_price(), 10000);

    EXPECT_TRUE(book.check_invariants());
}

TEST_F(OptimizedOrderBookTest, ModificationResetsPriorityQueue) {
    EXPECT_TRUE(book.add_order(Order(1, Side::Buy, 10000, 100, 1)));
    EXPECT_TRUE(book.add_order(Order(2, Side::Buy, 10000, 200, 2)));

    // Modify Order 1 with new seq 3
    EXPECT_TRUE(book.modify_order(1, 10000, 150, 3));
    EXPECT_EQ(book.total_orders(), 2);
    EXPECT_EQ(book.total_bid_volume(), 350);

    // Order 2 is now at front
    auto* top = book.get_best_bid_order();
    ASSERT_NE(top, nullptr);
    EXPECT_EQ(top->order_id(), 2);

    // Modify Order 1 to higher price level
    EXPECT_TRUE(book.modify_order(1, 10500, 150, 4));
    EXPECT_EQ(book.bid_level_count(), 2);
    EXPECT_EQ(book.best_bid_price(), 10500);

    top = book.get_best_bid_order();
    ASSERT_NE(top, nullptr);
    EXPECT_EQ(top->order_id(), 1);

    EXPECT_TRUE(book.check_invariants());
}

TEST_F(OptimizedOrderBookTest, PoolCapacityExpansionAndSlotRecycling) {
    // Initial capacity was 8. Insert 20 orders to trigger vector expansion
    for (std::uint64_t i = 1; i <= 20; ++i) {
        EXPECT_TRUE(book.add_order(Order(i, Side::Buy, 10000 + static_cast<PriceTicks>(i), 10, i)));
    }

    EXPECT_EQ(book.total_orders(), 20);
    EXPECT_GE(book.pool_capacity(), 20);
    EXPECT_TRUE(book.check_invariants());

    // Cancel 10 orders
    for (std::uint64_t i = 1; i <= 10; ++i) {
        EXPECT_TRUE(book.cancel_order(i).has_value());
    }

    EXPECT_EQ(book.total_orders(), 10);
    EXPECT_TRUE(book.check_invariants());

    // Re-insert 10 new orders (should recycle deallocated pool slots)
    for (std::uint64_t i = 21; i <= 30; ++i) {
        EXPECT_TRUE(book.add_order(Order(i, Side::Buy, 10000 + static_cast<PriceTicks>(i), 10, i)));
    }

    EXPECT_EQ(book.total_orders(), 20);
    EXPECT_TRUE(book.check_invariants());
}

}  // namespace quantengine::test

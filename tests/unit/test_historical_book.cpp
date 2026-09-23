// tests/unit/test_historical_book.cpp
//
// Phase 3 TDD: Historical L3 Book, Queue-Position Model, and Differential Equivalence.

#include <gtest/gtest.h>

#include <cstdint>
#include <random>
#include <vector>

#include "quantengine/core/order.hpp"
#include "quantengine/core/types.hpp"
#include "quantengine/optimized/optimized_order_book.hpp"
#include "quantengine/reference/reference_order_book.hpp"
#include "quantengine/replay/FifoQueueModel.hpp"
#include "quantengine/replay/HistoricalL3Book.hpp"
#include "quantengine/replay/L3Message.hpp"
#include "quantengine/replay/QueuePositionTracker.hpp"

namespace quantengine::replay::test {

using namespace quantengine::core;
using namespace quantengine::reference;
using namespace quantengine::optimized;

// ===========================================================================
// HistoricalL3Book: Basic Operations & Lifecycle
// ===========================================================================

TEST(HistoricalL3BookTest, AddOrdersBBOAndDepth) {
    HistoricalL3Book book;
    EXPECT_TRUE(book.is_empty());
    EXPECT_EQ(book.total_orders(), 0u);

    // Add Bids: 15000 (qty 100), 15000 (qty 50), 14950 (qty 200)
    EXPECT_TRUE(book.apply_add(OrderAdded{
        .exchange_ts = 1'000'000'000LL,
        .venue_order_id = 101,
        .symbol = market::make_symbol("AAPL"),
        .price = 15000,
        .quantity = 100,
        .side = Side::Buy,
    }));
    EXPECT_TRUE(book.apply_add(OrderAdded{
        .exchange_ts = 1'000'000'100LL,
        .venue_order_id = 102,
        .symbol = market::make_symbol("AAPL"),
        .price = 15000,
        .quantity = 50,
        .side = Side::Buy,
    }));
    EXPECT_TRUE(book.apply_add(OrderAdded{
        .exchange_ts = 1'000'000'200LL,
        .venue_order_id = 103,
        .symbol = market::make_symbol("AAPL"),
        .price = 14950,
        .quantity = 200,
        .side = Side::Buy,
    }));

    // Add Asks: 15050 (qty 75), 15100 (qty 300)
    EXPECT_TRUE(book.apply_add(OrderAdded{
        .exchange_ts = 1'000'000'300LL,
        .venue_order_id = 201,
        .symbol = market::make_symbol("AAPL"),
        .price = 15050,
        .quantity = 75,
        .side = Side::Sell,
    }));
    EXPECT_TRUE(book.apply_add(OrderAdded{
        .exchange_ts = 1'000'000'400LL,
        .venue_order_id = 202,
        .symbol = market::make_symbol("AAPL"),
        .price = 15100,
        .quantity = 300,
        .side = Side::Sell,
    }));

    EXPECT_EQ(book.total_orders(), 5u);
    EXPECT_EQ(book.total_bid_volume(), 350u);
    EXPECT_EQ(book.total_ask_volume(), 375u);

    EXPECT_EQ(book.best_bid_price(), 15000);
    EXPECT_EQ(book.best_bid_quantity(), 150u);
    EXPECT_EQ(book.best_ask_price(), 15050);
    EXPECT_EQ(book.best_ask_quantity(), 75u);

    EXPECT_TRUE(book.check_invariants());
}

TEST(HistoricalL3BookTest, RejectsDuplicatesAndInvalidFields) {
    HistoricalL3Book book;

    // Zero venue ID
    EXPECT_FALSE(book.apply_add(OrderAdded{
        .venue_order_id = 0,
        .price = 15000,
        .quantity = 100,
        .side = Side::Buy,
    }));

    // Zero quantity
    EXPECT_FALSE(book.apply_add(OrderAdded{
        .venue_order_id = 1,
        .price = 15000,
        .quantity = 0,
        .side = Side::Buy,
    }));

    // Zero price
    EXPECT_FALSE(book.apply_add(OrderAdded{
        .venue_order_id = 1,
        .price = 0,
        .quantity = 100,
        .side = Side::Buy,
    }));

    // Valid add
    EXPECT_TRUE(book.apply_add(OrderAdded{
        .venue_order_id = 101,
        .price = 15000,
        .quantity = 100,
        .side = Side::Buy,
    }));

    // Duplicate venue order ID
    EXPECT_FALSE(book.apply_add(OrderAdded{
        .venue_order_id = 101,
        .price = 15050,
        .quantity = 50,
        .side = Side::Buy,
    }));
}

TEST(HistoricalL3BookTest, ExecutionPartialAndFull) {
    HistoricalL3Book book;
    book.apply_add(OrderAdded{
        .venue_order_id = 101,
        .price = 15000,
        .quantity = 100,
        .side = Side::Buy,
    });
    book.apply_add(OrderAdded{
        .venue_order_id = 102,
        .price = 15000,
        .quantity = 50,
        .side = Side::Buy,
    });

    // Partial execute 40 of order 101
    EXPECT_TRUE(book.apply_execute(OrderExecuted{
        .exchange_ts = 1'000'000'500LL,
        .venue_order_id = 101,
        .executed_qty = 40,
        .match_number = 1,
    }));

    EXPECT_EQ(book.total_bid_volume(), 110u);
    EXPECT_EQ(book.get_order(101)->remaining_quantity, 60u);
    EXPECT_EQ(book.best_bid_quantity(), 110u);
    EXPECT_TRUE(book.check_invariants());

    // Full execute remainder of order 101
    EXPECT_TRUE(book.apply_execute(OrderExecuted{
        .exchange_ts = 1'000'000'600LL,
        .venue_order_id = 101,
        .executed_qty = 60,
        .match_number = 2,
    }));

    EXPECT_FALSE(book.has_order(101));
    EXPECT_EQ(book.total_orders(), 1u);
    EXPECT_EQ(book.best_bid_quantity(), 50u);
    EXPECT_TRUE(book.check_invariants());
}

TEST(HistoricalL3BookTest, CancelPartialAndFull) {
    HistoricalL3Book book;
    book.apply_add(OrderAdded{
        .venue_order_id = 201,
        .price = 15050,
        .quantity = 100,
        .side = Side::Sell,
    });

    // Partial cancel 30
    EXPECT_TRUE(book.apply_cancel(OrderCancelled{
        .venue_order_id = 201,
        .cancelled_qty = 30,
    }));
    EXPECT_EQ(book.total_ask_volume(), 70u);
    EXPECT_EQ(book.get_order(201)->remaining_quantity, 70u);
    EXPECT_TRUE(book.check_invariants());

    // Full cancel remaining 70
    EXPECT_TRUE(book.apply_cancel(OrderCancelled{
        .venue_order_id = 201,
        .cancelled_qty = 70,
    }));
    EXPECT_FALSE(book.has_order(201));
    EXPECT_EQ(book.total_orders(), 0u);
    EXPECT_EQ(book.total_ask_volume(), 0u);
    EXPECT_FALSE(book.best_ask_price().has_value());
    EXPECT_TRUE(book.check_invariants());
}

TEST(HistoricalL3BookTest, ReplaceResetsPriority) {
    HistoricalL3Book book;
    book.apply_add(OrderAdded{
        .venue_order_id = 101,
        .price = 15000,
        .quantity = 100,
        .side = Side::Buy,
    });
    book.apply_add(OrderAdded{
        .venue_order_id = 102,
        .price = 15000,
        .quantity = 50,
        .side = Side::Buy,
    });

    // Replace order 101 with new order 103 at same price: it must move BEHIND 102!
    EXPECT_TRUE(book.apply_replace(OrderReplaced{
        .exchange_ts = 1'000'000'700LL,
        .old_venue_order_id = 101,
        .new_venue_order_id = 103,
        .new_price = 15000,
        .new_quantity = 80,
        .side = Side::Buy,
    }));

    EXPECT_FALSE(book.has_order(101));
    EXPECT_TRUE(book.has_order(103));
    EXPECT_EQ(book.total_bid_volume(), 130u);

    // Queue ahead of 102 should now be 0 (it is at the front)
    EXPECT_EQ(book.queue_ahead_of(102), 0u);
    // Queue ahead of 103 should be 50 (order 102 is ahead of it)
    EXPECT_EQ(book.queue_ahead_of(103), 50u);
    EXPECT_TRUE(book.check_invariants());
}

TEST(HistoricalL3BookTest, CanonicalHashDeterminism) {
    auto build_book = []() {
        HistoricalL3Book book;
        book.apply_add(OrderAdded{
            .venue_order_id = 1,
            .price = 10000,
            .quantity = 100,
            .side = Side::Buy,
        });
        book.apply_add(OrderAdded{
            .venue_order_id = 2,
            .price = 10010,
            .quantity = 200,
            .side = Side::Sell,
        });
        book.apply_add(OrderAdded{
            .venue_order_id = 3,
            .price = 10000,
            .quantity = 50,
            .side = Side::Buy,
        });
        book.apply_execute(OrderExecuted{
            .venue_order_id = 1,
            .executed_qty = 30,
            .match_number = 1,
        });
        book.apply_cancel(OrderCancelled{
            .venue_order_id = 2,
            .cancelled_qty = 50,
        });
        return book;
    };

    auto book1 = build_book();
    auto book2 = build_book();

    EXPECT_EQ(book1.compute_canonical_hash(), book2.compute_canonical_hash());
    EXPECT_NE(book1.compute_canonical_hash(), 0u);
}

// ===========================================================================
// Differential Tests: HistoricalL3Book vs ReferenceOrderBook vs OptimizedOrderBook
// ===========================================================================

TEST(DifferentialBookTest, EquivalenceAcrossEngines) {
    ReferenceOrderBook ref_book;
    OptimizedOrderBook opt_book;
    HistoricalL3Book hist_book;

    std::mt19937_64 rng(424242);
    std::uniform_int_distribution<std::int64_t> price_dist(100, 150);
    std::uniform_int_distribution<std::uint64_t> qty_dist(10, 100);
    std::uniform_int_distribution<int> side_dist(0, 1);

    std::vector<OrderId> active_order_ids;

    OrderId next_id = 1;

    for (std::size_t op = 0; op < 500; ++op) {
        if (active_order_ids.empty() || (rng() % 3 != 0)) {
            // Add Order
            OrderId id = next_id++;
            Side side = (side_dist(rng) == 0) ? Side::Buy : Side::Sell;
            PriceTicks price = price_dist(rng) * 100;
            Quantity qty = qty_dist(rng);

            Order core_order(id, side, price, qty);

            EXPECT_TRUE(ref_book.add_order(core_order));
            EXPECT_TRUE(opt_book.add_order(core_order));
            EXPECT_TRUE(hist_book.apply_add(OrderAdded{
                .venue_order_id = id,
                .price = price,
                .quantity = qty,
                .side = side,
            }));

            active_order_ids.push_back(id);
        } else {
            // Cancel Order
            std::uniform_int_distribution<std::size_t> idx_dist(0, active_order_ids.size() - 1);
            std::size_t idx = idx_dist(rng);
            OrderId id = active_order_ids[idx];
            active_order_ids.erase(active_order_ids.begin() + static_cast<std::ptrdiff_t>(idx));

            const auto* existing = hist_book.get_order(id);
            ASSERT_NE(existing, nullptr);
            Quantity rem_qty = existing->remaining_quantity;

            auto ref_res = ref_book.cancel_order(id);
            auto opt_res = opt_book.cancel_order(id);
            bool hist_res = hist_book.apply_cancel(OrderCancelled{
                .venue_order_id = id,
                .cancelled_qty = rem_qty,
            });

            EXPECT_TRUE(ref_res.has_value());
            EXPECT_TRUE(opt_res.has_value());
            EXPECT_TRUE(hist_res);
        }

        // Verify differential equivalence at every step
        ASSERT_EQ(hist_book.total_orders(), ref_book.total_orders());
        ASSERT_EQ(hist_book.total_orders(), opt_book.total_orders());
        ASSERT_EQ(hist_book.total_bid_volume(), ref_book.total_bid_volume());
        ASSERT_EQ(hist_book.total_bid_volume(), opt_book.total_bid_volume());
        ASSERT_EQ(hist_book.total_ask_volume(), ref_book.total_ask_volume());
        ASSERT_EQ(hist_book.total_ask_volume(), opt_book.total_ask_volume());

        ASSERT_EQ(hist_book.best_bid_price(), ref_book.best_bid_price());
        ASSERT_EQ(hist_book.best_bid_price(), opt_book.best_bid_price());
        ASSERT_EQ(hist_book.best_ask_price(), ref_book.best_ask_price());
        ASSERT_EQ(hist_book.best_ask_price(), opt_book.best_ask_price());

        ASSERT_EQ(hist_book.best_bid_quantity(), ref_book.best_bid_quantity());
        ASSERT_EQ(hist_book.best_bid_quantity(), opt_book.best_bid_quantity());
        ASSERT_EQ(hist_book.best_ask_quantity(), ref_book.best_ask_quantity());
        ASSERT_EQ(hist_book.best_ask_quantity(), opt_book.best_ask_quantity());

        ASSERT_EQ(hist_book.get_bids(), ref_book.get_bids());
        ASSERT_EQ(hist_book.get_bids(), opt_book.get_bids());
        ASSERT_EQ(hist_book.get_asks(), ref_book.get_asks());
        ASSERT_EQ(hist_book.get_asks(), opt_book.get_asks());
    }
}

// ===========================================================================
// QueuePositionTracker & Conservative FIFO Model Tests
// ===========================================================================

TEST(QueueTrackerTest, InitialQueueAheadTracksLevelQuantity) {
    HistoricalL3Book book;
    book.apply_add(OrderAdded{
        .venue_order_id = 1,
        .price = 15000,
        .quantity = 100,
        .side = Side::Buy,
    });
    book.apply_add(OrderAdded{
        .venue_order_id = 2,
        .price = 15000,
        .quantity = 75,
        .side = Side::Buy,
    });

    QueuePositionTracker tracker;
    EXPECT_TRUE(tracker.track_order(501, Side::Buy, 15000, 50, book, 1000));

    EXPECT_EQ(tracker.queue_ahead_of(501), 175u);
    EXPECT_EQ(tracker.active_order_count(), 1u);
}

TEST(QueueTrackerTest, HistoricalExecutionsDepleteQueueAheadThenFill) {
    HistoricalL3Book book;
    book.apply_add(OrderAdded{
        .venue_order_id = 1,
        .price = 15000,
        .quantity = 100,
        .side = Side::Buy,
    });

    QueuePositionTracker tracker;
    tracker.track_order(501, Side::Buy, 15000, 50, book, 1000);
    EXPECT_EQ(tracker.queue_ahead_of(501), 100u);

    // Snapshot order before exec
    HistoricalOrder ord1 = *book.get_order(1);
    book.apply_execute(OrderExecuted{
        .venue_order_id = 1,
        .executed_qty = 60,
        .match_number = 1,
    });
    auto fills = tracker.on_historical_execute(
        OrderExecuted{.venue_order_id = 1, .executed_qty = 60, .match_number = 1}, &ord1);

    EXPECT_TRUE(fills.empty());  // Queue ahead absorbed execution
    EXPECT_EQ(tracker.queue_ahead_of(501), 40u);

    // Another execution of 50 against the level: 40 consumes remainder of queue ahead, 10 fills our
    // order!
    ord1.remaining_quantity = 40;
    book.apply_execute(OrderExecuted{
        .venue_order_id = 1,
        .executed_qty = 40,
        .match_number = 2,
    });
    fills = tracker.on_historical_execute(
        OrderExecuted{
            .exchange_ts = 2000,
            .venue_order_id = 1,
            .executed_qty = 50,  // 40 ahead + 10 our order
            .match_number = 2,
        },
        &ord1);

    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].client_order_id, 501u);
    EXPECT_EQ(fills[0].fill_quantity, 10u);
    EXPECT_EQ(fills[0].fill_price, 15000);
    EXPECT_EQ(fills[0].fill_ts, 2000);

    const auto* sim_ord = tracker.get_order(501);
    EXPECT_EQ(sim_ord->queue_ahead, 0u);
    EXPECT_EQ(sim_ord->remaining_quantity, 40u);
    EXPECT_EQ(sim_ord->filled_quantity, 10u);
    EXPECT_TRUE(sim_ord->active);
}

TEST(QueueTrackerTest, CancellationAheadReducesQueueAhead) {
    HistoricalL3Book book;
    book.apply_add(OrderAdded{
        .venue_order_id = 1,
        .price = 15000,
        .quantity = 100,
        .side = Side::Buy,
    });

    QueuePositionTracker tracker;
    tracker.track_order(501, Side::Buy, 15000, 50, book, 1000);
    EXPECT_EQ(tracker.queue_ahead_of(501), 100u);

    // Cancel 40 of order 1 (which was placed before simulated order 501)
    HistoricalOrder ord1 = *book.get_order(1);
    book.apply_cancel(OrderCancelled{
        .venue_order_id = 1,
        .cancelled_qty = 40,
    });
    tracker.on_historical_cancel(OrderCancelled{.venue_order_id = 1, .cancelled_qty = 40}, &ord1);

    EXPECT_EQ(tracker.queue_ahead_of(501), 60u);
}

TEST(QueueTrackerTest, CancellationBehindDoesNotReduceQueueAhead) {
    HistoricalL3Book book;
    book.apply_add(OrderAdded{
        .venue_order_id = 1,
        .price = 15000,
        .quantity = 100,
        .side = Side::Buy,
    });

    QueuePositionTracker tracker;
    tracker.track_order(501, Side::Buy, 15000, 50, book, 1000);
    EXPECT_EQ(tracker.queue_ahead_of(501), 100u);

    // Add order 2 AFTER our order joined
    book.apply_add(OrderAdded{
        .venue_order_id = 2,
        .price = 15000,
        .quantity = 80,
        .side = Side::Buy,
    });

    // Now order 2 is cancelled. It was behind us, so our queue ahead must remain 100!
    HistoricalOrder ord2 = *book.get_order(2);
    book.apply_cancel(OrderCancelled{
        .venue_order_id = 2,
        .cancelled_qty = 80,
    });
    tracker.on_historical_cancel(OrderCancelled{.venue_order_id = 2, .cancelled_qty = 80}, &ord2);

    EXPECT_EQ(tracker.queue_ahead_of(501), 100u);
}

TEST(QueueTrackerTest, TradeThroughOurPriceFillsImmediately) {
    HistoricalL3Book book;
    book.apply_add(OrderAdded{
        .venue_order_id = 1,
        .price = 15000,
        .quantity = 100,
        .side = Side::Buy,
    });

    QueuePositionTracker tracker;
    tracker.track_order(501, Side::Buy, 15000, 50, book, 1000);

    // Suppose an aggressor executes at 14900 (trades through our 15000 bid)
    HistoricalOrder lower_bid{
        .venue_order_id = 999,
        .price = 14900,
        .initial_quantity = 50,
        .remaining_quantity = 50,
        .side = Side::Buy,
    };

    auto fills = tracker.on_historical_execute(
        OrderExecuted{
            .exchange_ts = 3000,
            .venue_order_id = 999,
            .executed_qty = 50,
            .match_number = 10,
        },
        &lower_bid);

    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].client_order_id, 501u);
    EXPECT_EQ(fills[0].fill_quantity, 50u);
    EXPECT_EQ(fills[0].fill_price, 15000);
    EXPECT_EQ(tracker.active_order_count(), 0u);  // Fully filled
}

TEST(QueueTrackerTest, MultipleOrdersAtSamePriceLevel) {
    HistoricalL3Book book;
    book.apply_add(OrderAdded{
        .venue_order_id = 1,
        .price = 15000,
        .quantity = 50,
        .side = Side::Buy,
    });

    QueuePositionTracker tracker;
    // Order 501 joins when book has 50 ahead
    tracker.track_order(501, Side::Buy, 15000, 20, book, 1000);
    // Order 502 joins when book has 50 ahead (plus logically 501)
    tracker.track_order(502, Side::Buy, 15000, 30, book, 1010);

    EXPECT_EQ(tracker.queue_ahead_of(501), 50u);
    EXPECT_EQ(tracker.queue_ahead_of(502), 70u);

    HistoricalOrder ord1 = *book.get_order(1);
    // Historical execution of 60 shares
    // 50 drains queue ahead of 501 and 502
    // 10 fills 501
    auto fills = tracker.on_historical_execute(
        OrderExecuted{
            .exchange_ts = 2000,
            .venue_order_id = 1,
            .executed_qty = 60,
            .match_number = 1,
        },
        &ord1);

    EXPECT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].client_order_id, 501u);
    EXPECT_EQ(fills[0].fill_quantity, 10u);

    EXPECT_EQ(tracker.get_order(501)->remaining_quantity, 10u);
    EXPECT_EQ(tracker.get_order(502)->remaining_quantity, 30u);
}

}  // namespace quantengine::replay::test

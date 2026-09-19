#include <gtest/gtest.h>

#include <sstream>

#include "quantengine/core/order.hpp"

namespace quantengine::test {

using namespace quantengine::core;

TEST(OrderTest, ConstructionAndAccessors) {
    Order order(101, Side::Buy, 10050, 500, 1);

    EXPECT_EQ(order.order_id(), 101);
    EXPECT_EQ(order.side(), Side::Buy);
    EXPECT_EQ(order.price(), 10050);
    EXPECT_EQ(order.initial_quantity(), 500);
    EXPECT_EQ(order.remaining_quantity(), 500);
    EXPECT_EQ(order.filled_quantity(), 0);
    EXPECT_EQ(order.sequence_number(), 1);
    EXPECT_EQ(order.status(), OrderStatus::New);
    EXPECT_FALSE(order.is_active());
    EXPECT_FALSE(order.is_terminal());
    EXPECT_TRUE(order.check_invariants());
}

TEST(OrderTest, TransitionToResting) {
    Order order(101, Side::Buy, 10050, 500, 1);
    EXPECT_TRUE(order.mark_resting());
    EXPECT_EQ(order.status(), OrderStatus::Resting);
    EXPECT_TRUE(order.is_active());
    EXPECT_FALSE(order.is_terminal());
    EXPECT_TRUE(order.check_invariants());
}

TEST(OrderTest, PartialFillAndCompleteFill) {
    Order order(101, Side::Buy, 10050, 500, 1);
    ASSERT_TRUE(order.mark_resting());

    // Partial fill of 200
    EXPECT_TRUE(order.apply_fill(200));
    EXPECT_EQ(order.status(), OrderStatus::PartiallyFilled);
    EXPECT_EQ(order.remaining_quantity(), 300);
    EXPECT_EQ(order.filled_quantity(), 200);
    EXPECT_TRUE(order.is_active());
    EXPECT_FALSE(order.is_terminal());
    EXPECT_TRUE(order.check_invariants());

    // Remaining fill of 300
    EXPECT_TRUE(order.apply_fill(300));
    EXPECT_EQ(order.status(), OrderStatus::Filled);
    EXPECT_EQ(order.remaining_quantity(), 0);
    EXPECT_EQ(order.filled_quantity(), 500);
    EXPECT_FALSE(order.is_active());
    EXPECT_TRUE(order.is_terminal());
    EXPECT_TRUE(order.check_invariants());
}

TEST(OrderTest, RejectsOverfillAndZeroFill) {
    Order order(101, Side::Buy, 10050, 500, 1);
    ASSERT_TRUE(order.mark_resting());

    EXPECT_FALSE(order.apply_fill(0));
    EXPECT_FALSE(order.apply_fill(501));
    EXPECT_EQ(order.remaining_quantity(), 500);
    EXPECT_EQ(order.status(), OrderStatus::Resting);
}

TEST(OrderTest, Cancellation) {
    Order order(101, Side::Sell, 10050, 500, 1);
    ASSERT_TRUE(order.mark_resting());

    EXPECT_TRUE(order.mark_cancelled());
    EXPECT_EQ(order.status(), OrderStatus::Cancelled);
    EXPECT_EQ(order.remaining_quantity(), 0);
    EXPECT_TRUE(order.is_terminal());
    EXPECT_FALSE(order.is_active());
    EXPECT_TRUE(order.check_invariants());

    // Cannot cancel again or fill
    EXPECT_FALSE(order.mark_cancelled());
    EXPECT_FALSE(order.apply_fill(10));
}

TEST(OrderTest, Rejection) {
    Order order(101, Side::Sell, 10050, 500, 1);
    EXPECT_TRUE(order.mark_rejected());
    EXPECT_EQ(order.status(), OrderStatus::Rejected);
    EXPECT_TRUE(order.is_terminal());
    EXPECT_FALSE(order.mark_resting());
}

TEST(OrderTest, ModificationHelpers) {
    Order order(101, Side::Buy, 10050, 500, 1);
    order.update_priority_sequence(42);
    EXPECT_EQ(order.sequence_number(), 42);

    order.update_remaining_quantity(200);
    EXPECT_EQ(order.remaining_quantity(), 200);
    EXPECT_EQ(order.initial_quantity(), 500);

    order.update_remaining_quantity(600);
    EXPECT_EQ(order.remaining_quantity(), 600);
    EXPECT_EQ(order.initial_quantity(), 600);
}

TEST(OrderTest, StreamOutput) {
    Order order(101, Side::Buy, 10050, 500, 1);
    std::ostringstream oss;
    oss << order;
    EXPECT_NE(oss.str().find("id=101"), std::string::npos);
    EXPECT_NE(oss.str().find("side=BUY"), std::string::npos);
}

}  // namespace quantengine::test

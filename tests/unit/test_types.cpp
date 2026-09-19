#include <gtest/gtest.h>

#include <sstream>

#include "quantengine/core/types.hpp"

namespace quantengine::test {

using namespace quantengine::core;

TEST(TypesTest, OppositeSide) {
    EXPECT_EQ(opposite_side(Side::Buy), Side::Sell);
    EXPECT_EQ(opposite_side(Side::Sell), Side::Buy);
}

TEST(TypesTest, SideToStringAndStream) {
    EXPECT_EQ(to_string(Side::Buy), "BUY");
    EXPECT_EQ(to_string(Side::Sell), "SELL");

    std::ostringstream oss;
    oss << Side::Buy << " " << Side::Sell;
    EXPECT_EQ(oss.str(), "BUY SELL");
}

TEST(TypesTest, OrderStatusToStringAndStream) {
    EXPECT_EQ(to_string(OrderStatus::New), "NEW");
    EXPECT_EQ(to_string(OrderStatus::Resting), "RESTING");
    EXPECT_EQ(to_string(OrderStatus::PartiallyFilled), "PARTIALLY_FILLED");
    EXPECT_EQ(to_string(OrderStatus::Filled), "FILLED");
    EXPECT_EQ(to_string(OrderStatus::Cancelled), "CANCELLED");
    EXPECT_EQ(to_string(OrderStatus::Rejected), "REJECTED");

    std::ostringstream oss;
    oss << OrderStatus::Resting;
    EXPECT_EQ(oss.str(), "RESTING");
}

TEST(TypesTest, OrderTypeToStringAndStream) {
    EXPECT_EQ(to_string(OrderType::Limit), "LIMIT");

    std::ostringstream oss;
    oss << OrderType::Limit;
    EXPECT_EQ(oss.str(), "LIMIT");
}

TEST(TypesTest, RejectReasonToStringAndStream) {
    EXPECT_EQ(to_string(RejectReason::None), "NONE");
    EXPECT_EQ(to_string(RejectReason::DuplicateOrderId), "DUPLICATE_ORDER_ID");
    EXPECT_EQ(to_string(RejectReason::UnknownOrder), "UNKNOWN_ORDER");
    EXPECT_EQ(to_string(RejectReason::InvalidPrice), "INVALID_PRICE");
    EXPECT_EQ(to_string(RejectReason::InvalidQuantity), "INVALID_QUANTITY");
    EXPECT_EQ(to_string(RejectReason::OrderAlreadyFilled), "ORDER_ALREADY_FILLED");
    EXPECT_EQ(to_string(RejectReason::OrderAlreadyCancelled), "ORDER_ALREADY_CANCELLED");
    EXPECT_EQ(to_string(RejectReason::InvalidStateTransition), "INVALID_STATE_TRANSITION");

    std::ostringstream oss;
    oss << RejectReason::DuplicateOrderId;
    EXPECT_EQ(oss.str(), "DUPLICATE_ORDER_ID");
}

}  // namespace quantengine::test

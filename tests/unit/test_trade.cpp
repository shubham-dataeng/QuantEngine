#include <gtest/gtest.h>

#include <sstream>

#include "quantengine/core/events.hpp"
#include "quantengine/core/trade.hpp"

namespace quantengine::test {

using namespace quantengine::core;

TEST(TradeTest, FieldsAndEquality) {
    Trade t1{1, 101, 102, Side::Sell, 15000, 250, 10};
    Trade t2{1, 101, 102, Side::Sell, 15000, 250, 10};
    Trade t3{2, 101, 103, Side::Buy, 15000, 100, 11};

    EXPECT_EQ(t1, t2);
    EXPECT_NE(t1, t3);

    std::ostringstream oss;
    oss << t1;
    EXPECT_NE(oss.str().find("Trade{id=1"), std::string::npos);
    EXPECT_NE(oss.str().find("maker=101"), std::string::npos);
}

TEST(EventsTest, CommandVariantsAndExecutionReport) {
    CreateOrderCommand create_cmd{101, Side::Buy, 10000, 50};
    CancelOrderCommand cancel_cmd{101};
    ModifyOrderCommand modify_cmd{101, 10100, 75};

    OrderCommand cmd1{1, create_cmd};
    OrderCommand cmd2{2, cancel_cmd};
    OrderCommand cmd3{3, modify_cmd};

    EXPECT_TRUE(std::holds_alternative<CreateOrderCommand>(cmd1.payload));
    EXPECT_TRUE(std::holds_alternative<CancelOrderCommand>(cmd2.payload));
    EXPECT_TRUE(std::holds_alternative<ModifyOrderCommand>(cmd3.payload));

    ExecutionReport report{.order_id = 101,
                           .status = OrderStatus::Resting,
                           .reject_reason = RejectReason::None,
                           .remaining_quantity = 50,
                           .filled_quantity = 0,
                           .price = 10000,
                           .side = Side::Buy,
                           .sequence_number = 1,
                           .trades = {}};

    EXPECT_EQ(report.order_id, 101);
    EXPECT_EQ(report.status, OrderStatus::Resting);
    EXPECT_EQ(report.trades.size(), 0);
}

}  // namespace quantengine::test

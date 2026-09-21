#include <gtest/gtest.h>

#include "quantengine/execution/OrderStateMachine.hpp"

namespace quantengine::test {

using namespace quantengine::core;
using namespace quantengine::execution;

class OrderStateMachineTest : public ::testing::Test {
protected:
    OrderStateMachine osm_;

    [[nodiscard]] static auto make_req(OrderId id, Quantity qty = 100) -> OrderRequest {
        OrderRequest req{};
        req.client_order_id = id;
        req.side = Side::Buy;
        req.type = OrderType::Limit;
        req.price = 15000;
        req.quantity = qty;
        return req;
    }
};

TEST_F(OrderStateMachineTest, ValidStateTransitions) {
    EXPECT_TRUE(
        OrderStateMachine::is_valid_transition(InFlightState::New, InFlightState::PendingAck));
    EXPECT_TRUE(
        OrderStateMachine::is_valid_transition(InFlightState::PendingAck, InFlightState::Resting));
    EXPECT_TRUE(OrderStateMachine::is_valid_transition(InFlightState::Resting,
                                                       InFlightState::PartiallyFilled));
    EXPECT_TRUE(OrderStateMachine::is_valid_transition(InFlightState::PartiallyFilled,
                                                       InFlightState::Filled));
    EXPECT_TRUE(OrderStateMachine::is_valid_transition(InFlightState::Resting,
                                                       InFlightState::PendingCancel));
    EXPECT_TRUE(OrderStateMachine::is_valid_transition(InFlightState::PendingCancel,
                                                       InFlightState::Cancelled));
}

TEST_F(OrderStateMachineTest, InvalidStateTransitionsRejected) {
    // Cannot transition from terminal states
    EXPECT_FALSE(
        OrderStateMachine::is_valid_transition(InFlightState::Filled, InFlightState::Resting));
    EXPECT_FALSE(OrderStateMachine::is_valid_transition(InFlightState::Cancelled,
                                                        InFlightState::PendingAck));
    EXPECT_FALSE(
        OrderStateMachine::is_valid_transition(InFlightState::Rejected, InFlightState::Resting));

    // Cannot transition directly from New to Filled without PendingAck
    EXPECT_FALSE(OrderStateMachine::is_valid_transition(InFlightState::New, InFlightState::Filled));
}

TEST_F(OrderStateMachineTest, RegisterOrderAndTrackCounts) {
    EXPECT_EQ(osm_.active_count(), 0u);

    EXPECT_TRUE(osm_.register_order(make_req(1, 100), InFlightState::PendingAck));
    EXPECT_EQ(osm_.active_count(), 1u);
    EXPECT_EQ(osm_.pending_ack_count(), 1u);
    EXPECT_EQ(osm_.resting_count(), 0u);

    // Duplicate client_order_id fails
    EXPECT_FALSE(osm_.register_order(make_req(1, 200)));

    // Transition to Resting
    EXPECT_TRUE(osm_.transition(1, InFlightState::Resting));
    EXPECT_EQ(osm_.pending_ack_count(), 0u);
    EXPECT_EQ(osm_.resting_count(), 1u);
}

TEST_F(OrderStateMachineTest, VenueIdAssociationAndLookup) {
    osm_.register_order(make_req(42, 50));
    EXPECT_TRUE(osm_.associate_venue_id(42, "alpaca-uuid-999"));

    const auto client_id = osm_.find_by_venue_id("alpaca-uuid-999");
    ASSERT_TRUE(client_id.has_value());
    EXPECT_EQ(*client_id, 42u);

    EXPECT_FALSE(osm_.find_by_venue_id("non-existent").has_value());
}

TEST_F(OrderStateMachineTest, ApplyFillUpdatesState) {
    osm_.register_order(make_req(10, 100));
    osm_.transition(10, InFlightState::Resting);

    // Partial fill 40
    EXPECT_TRUE(osm_.apply_fill(10, 40));
    auto ord = osm_.get_order(10);
    ASSERT_TRUE(ord.has_value());
    EXPECT_EQ(ord->filled_quantity, 40u);
    EXPECT_EQ(ord->remaining_quantity, 60u);
    EXPECT_EQ(ord->state, InFlightState::PartiallyFilled);

    // Remaining fill 60
    EXPECT_TRUE(osm_.apply_fill(10, 60));
    ord = osm_.get_order(10);
    ASSERT_TRUE(ord.has_value());
    EXPECT_EQ(ord->filled_quantity, 100u);
    EXPECT_EQ(ord->remaining_quantity, 0u);
    EXPECT_EQ(ord->state, InFlightState::Filled);

    // Order is now terminal
    EXPECT_EQ(osm_.active_count(), 0u);

    // Purge terminal orders
    EXPECT_EQ(osm_.purge_terminal_orders(), 1u);
    EXPECT_FALSE(osm_.get_order(10).has_value());
}

}  // namespace quantengine::test

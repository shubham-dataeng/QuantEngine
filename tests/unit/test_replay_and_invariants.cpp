#include <gtest/gtest.h>

#include "quantengine/engine/canonical_state.hpp"
#include "quantengine/engine/invariants.hpp"
#include "quantengine/engine/matching_engine.hpp"
#include "quantengine/engine/replay.hpp"

namespace quantengine::test {

using namespace quantengine::core;
using namespace quantengine::engine;

class DeterminismAndReplayTest : public ::testing::Test {};

TEST_F(DeterminismAndReplayTest, CanonicalHashDeterminism) {
    MatchingEngine e1;
    MatchingEngine e2;

    EXPECT_EQ(CanonicalState::compute_hash(e1), CanonicalState::compute_hash(e2));

    [[maybe_unused]] auto r1 = e1.submit_order(1, Side::Buy, 10000, 50);
    [[maybe_unused]] auto r2 = e2.submit_order(1, Side::Buy, 10000, 50);

    EXPECT_EQ(CanonicalState::compute_hash(e1), CanonicalState::compute_hash(e2));
}

TEST_F(DeterminismAndReplayTest, CanonicalHashStateSensitivity) {
    MatchingEngine base;
    [[maybe_unused]] auto r0 = base.submit_order(1, Side::Buy, 10000, 50);
    const auto base_hash = CanonicalState::compute_hash(base);

    // Different price
    MatchingEngine e_diff_price;
    [[maybe_unused]] auto r1 = e_diff_price.submit_order(1, Side::Buy, 10050, 50);
    EXPECT_NE(CanonicalState::compute_hash(e_diff_price), base_hash);

    // Different quantity
    MatchingEngine e_diff_qty;
    [[maybe_unused]] auto r2 = e_diff_qty.submit_order(1, Side::Buy, 10000, 51);
    EXPECT_NE(CanonicalState::compute_hash(e_diff_qty), base_hash);

    // Different side
    MatchingEngine e_diff_side;
    [[maybe_unused]] auto r3 = e_diff_side.submit_order(1, Side::Sell, 10000, 50);
    EXPECT_NE(CanonicalState::compute_hash(e_diff_side), base_hash);
}

TEST_F(DeterminismAndReplayTest, InvariantAuditorHealthyEngine) {
    MatchingEngine engine;
    EXPECT_TRUE(InvariantAuditor::audit(engine));

    [[maybe_unused]] auto r1 = engine.submit_order(1, Side::Buy, 10000, 100);
    [[maybe_unused]] auto r2 = engine.submit_order(2, Side::Sell, 10500, 200);

    auto result = InvariantAuditor::audit(engine);
    EXPECT_TRUE(result.ok);
    EXPECT_TRUE(result.error_message.empty());
}

TEST_F(DeterminismAndReplayTest, InvariantAuditorDetectsCrossedBook) {
    MatchingEngine engine;
    // Add resting sell at 10000
    [[maybe_unused]] auto r1 = engine.submit_order(1, Side::Sell, 10000, 100);

    // Artificially bypass matching to create crossed resting book
    // Buy at 10500 directly added to book
    Order crossed_buy(2, Side::Buy, 10500, 50, 2);
    ASSERT_TRUE(engine.book().add_order(crossed_buy));

    auto result = InvariantAuditor::audit(engine);
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error_message.find("Book crossed invariant violated"), std::string::npos);
}

TEST_F(DeterminismAndReplayTest, ComplexReplayBitForBitEquivalence) {
    std::vector<OrderCommand> journal = {
        {1, CreateOrderCommand{1, Side::Buy, 10000, 100}},
        {2, CreateOrderCommand{2, Side::Buy, 9900, 200}},
        {3, CreateOrderCommand{3, Side::Buy, 10000, 150}},
        {4, CreateOrderCommand{4, Side::Sell, 10200, 300}},
        {5, CreateOrderCommand{5, Side::Sell, 10300, 400}},
        {6, CreateOrderCommand{6, Side::Sell, 10000, 50}},  // Matches against Order 1 (50 of 100)
        {7, CancelOrderCommand{2}},                         // Cancel buy @ 9900
        {8, ModifyOrderCommand{4, 10000, 100}},  // Modify Sell to 10000, qty 100 (matches remaining
                                                 // 50 of Order 1 + 50 of Order 3)
        {9, CreateOrderCommand{7, Side::Buy, 10500, 500}},  // Sweeps all remaining resting asks
        {10, CancelOrderCommand{7}},                        // Cancel remainder of Order 7
    };

    EXPECT_TRUE(ReplayEngine::verify_determinism(journal));

    const auto res1 = ReplayEngine::replay(journal);
    const auto res2 = ReplayEngine::replay(journal);

    EXPECT_TRUE(res1.invariants_satisfied);
    EXPECT_TRUE(res2.invariants_satisfied);
    EXPECT_EQ(res1.final_hash, res2.final_hash);
    EXPECT_EQ(res1.all_trades.size(), res2.all_trades.size());
    EXPECT_GT(res1.all_trades.size(), 0);

    for (std::size_t i = 0; i < res1.all_trades.size(); ++i) {
        EXPECT_EQ(res1.all_trades[i], res2.all_trades[i]);
    }
}

}  // namespace quantengine::test

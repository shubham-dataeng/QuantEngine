#include <gtest/gtest.h>

#include "fuzz_generator.hpp"
#include "quantengine/engine/canonical_state.hpp"
#include "quantengine/engine/generic_matching_engine.hpp"
#include "quantengine/engine/invariants.hpp"
#include "quantengine/engine/replay.hpp"
#include "quantengine/optimized/optimized_order_book.hpp"
#include "quantengine/reference/reference_order_book.hpp"

namespace quantengine::test {

using namespace quantengine::core;
using namespace quantengine::engine;
using namespace quantengine::reference;
using namespace quantengine::optimized;

class DifferentialFuzzTest : public ::testing::TestWithParam<std::uint64_t> {};

TEST_P(DifferentialFuzzTest, DifferentialEquivalenceReferenceVsOptimized) {
    const std::uint64_t seed = GetParam();
    FuzzGenerator generator(seed);

    constexpr std::size_t EVENT_COUNT = 2500;
    const auto commands = generator.generate_commands(EVENT_COUNT);

    ReferenceMatchingEngine ref_engine;
    OptimizedMatchingEngine opt_engine;

    for (std::size_t i = 0; i < commands.size(); ++i) {
        const auto& cmd = commands[i];

        auto ref_report = ref_engine.process_command(cmd);
        auto opt_report = opt_engine.process_command(cmd);

        // 1. Exact ExecutionReport matching
        ASSERT_EQ(ref_report.order_id, opt_report.order_id)
            << "Mismatch at step " << i << ", seq " << cmd.sequence_number;
        ASSERT_EQ(ref_report.status, opt_report.status)
            << "Mismatch at step " << i << ", seq " << cmd.sequence_number;
        ASSERT_EQ(ref_report.reject_reason, opt_report.reject_reason)
            << "Mismatch at step " << i << ", seq " << cmd.sequence_number;
        ASSERT_EQ(ref_report.remaining_quantity, opt_report.remaining_quantity)
            << "Mismatch at step " << i << ", seq " << cmd.sequence_number;
        ASSERT_EQ(ref_report.filled_quantity, opt_report.filled_quantity)
            << "Mismatch at step " << i << ", seq " << cmd.sequence_number;
        ASSERT_EQ(ref_report.trades.size(), opt_report.trades.size())
            << "Mismatch in trade count at step " << i << ", seq " << cmd.sequence_number;

        for (std::size_t t = 0; t < ref_report.trades.size(); ++t) {
            ASSERT_EQ(ref_report.trades[t], opt_report.trades[t])
                << "Mismatch in trade " << t << " at step " << i;
        }

        // 2. Continuous invariant audits
        auto ref_audit = InvariantAuditor::audit(ref_engine);
        ASSERT_TRUE(ref_audit.ok) << "Reference invariant failed: " << ref_audit.error_message;

        auto opt_audit = InvariantAuditor::audit(opt_engine);
        ASSERT_TRUE(opt_audit.ok) << "Optimized invariant failed: " << opt_audit.error_message;
    }

    // 3. Final Book State Equivalence
    EXPECT_EQ(ref_engine.book().total_orders(), opt_engine.book().total_orders());
    EXPECT_EQ(ref_engine.book().total_bid_volume(), opt_engine.book().total_bid_volume());
    EXPECT_EQ(ref_engine.book().total_ask_volume(), opt_engine.book().total_ask_volume());
    EXPECT_EQ(ref_engine.book().best_bid_price(), opt_engine.book().best_bid_price());
    EXPECT_EQ(ref_engine.book().best_ask_price(), opt_engine.book().best_ask_price());
    EXPECT_EQ(ref_engine.book().get_bids(), opt_engine.book().get_bids());
    EXPECT_EQ(ref_engine.book().get_asks(), opt_engine.book().get_asks());

    // 4. Bit-for-bit Canonical State Hash Equivalence
    const auto ref_hash = CanonicalState::compute_hash(ref_engine);
    const auto opt_hash = CanonicalState::compute_hash(opt_engine);
    EXPECT_EQ(ref_hash, opt_hash) << "Canonical state hashes differ after 2,500 operations!";

    // 5. Replay Determinism across different book architectures
    EXPECT_TRUE(
        (ReplayEngine::verify_determinism<ReferenceOrderBook, OptimizedOrderBook>(commands)));
}

// Instantiate test suite across 4 distinct random seeds (10,000 total operations)
INSTANTIATE_TEST_SUITE_P(FuzzSeeds, DifferentialFuzzTest,
                         ::testing::Values(1337ULL, 424242ULL, 999999ULL, 123456789ULL));

}  // namespace quantengine::test

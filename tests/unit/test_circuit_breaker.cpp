#include <gtest/gtest.h>

#include "quantengine/risk/CircuitBreaker.hpp"
#include "quantengine/risk/StandardRiskManager.hpp"

namespace quantengine::test {

using namespace quantengine::core;
using namespace quantengine::risk;

class CircuitBreakerTest : public ::testing::Test {
protected:
    CircuitBreakerConfig cfg_{
        .warning_drawdown = 500, .trip_drawdown = 1000, .auto_halt_risk_manager = true};
    CircuitBreaker cb_{cfg_};
    StandardRiskManager risk_mgr_{RiskLimits::no_limit()};

    [[nodiscard]] static auto make_pv(PriceTicks realized, PriceTicks unrealized) -> PortfolioView {
        PortfolioView pv{};
        pv.session_realized_pnl = realized;
        pv.session_unrealized_pnl = unrealized;
        return pv;
    }
};

TEST_F(CircuitBreakerTest, InitialStateNormal) {
    EXPECT_FALSE(cb_.is_tripped());
    EXPECT_EQ(cb_.state(), CircuitBreakerState::Normal);
    EXPECT_EQ(cb_.peak_pnl(), 0);
    EXPECT_EQ(cb_.max_drawdown_observed(), 0);
}

TEST_F(CircuitBreakerTest, NormalGainTracksPeak) {
    auto state = cb_.update(make_pv(1000, 200), &risk_mgr_);
    EXPECT_EQ(state, CircuitBreakerState::Normal);
    EXPECT_FALSE(cb_.is_tripped());
    EXPECT_EQ(cb_.peak_pnl(), 1200);
    EXPECT_EQ(cb_.max_drawdown_observed(), 0);
}

TEST_F(CircuitBreakerTest, WarningThresholdCrossed) {
    // Peak at 2000
    cb_.update(make_pv(2000, 0), &risk_mgr_);

    // Drawdown of 600 (2000 - 1400), exceeds warning (500) but below trip (1000)
    auto state = cb_.update(make_pv(1400, 0), &risk_mgr_);
    EXPECT_EQ(state, CircuitBreakerState::Warning);
    EXPECT_FALSE(cb_.is_tripped());
    EXPECT_EQ(cb_.max_drawdown_observed(), 600);
}

TEST_F(CircuitBreakerTest, TripThresholdBreachedAndStickyHalt) {
    // Peak at 2000
    cb_.update(make_pv(2000, 0), &risk_mgr_);

    // Drawdown of 1200 (2000 - 800), breaches trip threshold 1000!
    auto state = cb_.update(make_pv(800, 0), &risk_mgr_);
    EXPECT_EQ(state, CircuitBreakerState::Tripped);
    EXPECT_TRUE(cb_.is_tripped());
    EXPECT_EQ(cb_.max_drawdown_observed(), 1200);

    // Sticky check: even if market recovers to 1800, breaker stays tripped!
    state = cb_.update(make_pv(1800, 0), &risk_mgr_);
    EXPECT_EQ(state, CircuitBreakerState::Tripped);
    EXPECT_TRUE(cb_.is_tripped());

    // Reset allows normal trading again
    cb_.reset(&risk_mgr_);
    EXPECT_FALSE(cb_.is_tripped());
    EXPECT_EQ(cb_.state(), CircuitBreakerState::Normal);
}

}  // namespace quantengine::test

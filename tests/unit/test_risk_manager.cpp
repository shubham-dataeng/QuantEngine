#include <gtest/gtest.h>

#include "quantengine/execution/IExecutionGateway.hpp"
#include "quantengine/risk/IRiskManager.hpp"
#ifdef QUANTENGINE_RISK_IMPL
#include "quantengine/risk/StandardRiskManager.hpp"
#endif

// =============================================================================
// RiskManagerTest — TDD skeleton for the StandardRiskManager implementation.
//
// STATUS: These tests define the required behaviour. They currently pass only
// against NullRiskManager (which approves everything). When the junior agent
// implements StandardRiskManager, they MUST make all tests in
// StandardRiskManagerTest pass without modifying a single test assertion.
//
// STRUCTURE:
//   NullRiskManagerTest       — sanity checks on the no-op impl (pass today)
//   RiskVerdictTest           — constexpr factory and layout checks (pass today)
//   RiskLimitsTest            — limit struct construction (pass today)
//   StandardRiskManagerTest   — the real contract (WILL FAIL until M7 impl)
//
// IMPLEMENTATION GUIDE FOR JUNIOR AGENTS:
//   - Create include/quantengine/risk/StandardRiskManager.hpp
//   - Create src/risk/StandardRiskManager.cpp
//   - StandardRiskManager must inherit IRiskManager publicly
//   - validate() must be noexcept, O(1), and heap-free
//   - Limits are evaluated in order: qty -> notional -> position -> exposure -> drawdown
//   - The halt state is sticky: once halted, validate() always rejects
//     with DrawdownHaltActive until reset_halt() is explicitly called
// =============================================================================

namespace quantengine::test {

using namespace quantengine::core;
using namespace quantengine::execution;
using namespace quantengine::risk;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Build a minimal OrderRequest for testing without worrying about all fields.
[[nodiscard]] static auto make_request(OrderId id, Side side, PriceTicks price, Quantity qty,
                                       std::string_view sym = "AAPL") noexcept -> OrderRequest {
    OrderRequest req{};
    req.client_order_id = id;
    req.side = side;
    req.price = price;
    req.quantity = qty;
    req.type = OrderType::Limit;
    req.time_in_force = TimeInForce::Day;
    req.symbol = market::make_symbol(sym);
    return req;
}

[[nodiscard]] static auto make_portfolio(PriceTicks realized_pnl = 0, Quantity net_position = 0,
                                         PriceTicks gross_exp = 0) noexcept -> PortfolioView {
    PortfolioView pv{};
    pv.session_realized_pnl = realized_pnl;
    pv.net_position = net_position;
    pv.gross_notional_exposure = gross_exp;
    return pv;
}

// ---------------------------------------------------------------------------
// NullRiskManagerTest: verifies the no-op impl satisfies the interface.
// These tests MUST pass before and after M7 implementation.
// ---------------------------------------------------------------------------
class NullRiskManagerTest : public ::testing::Test {
protected:
    NullRiskManager rm_;
};

TEST_F(NullRiskManagerTest, ApprovesEveryOrder) {
    const auto req = make_request(1, Side::Buy, 15000, 100);
    const auto pv = make_portfolio();
    const auto v = rm_.validate(req, pv);

    EXPECT_TRUE(v.approved);
    EXPECT_EQ(v.reject_reason, RiskRejectReason::None);
}

TEST_F(NullRiskManagerTest, NeverHalts) {
    EXPECT_FALSE(rm_.is_halted());
    rm_.reset_halt();  // no-op; must not throw or crash
    EXPECT_FALSE(rm_.is_halted());
}

TEST_F(NullRiskManagerTest, ReturnsNoLimitFromCurrentLimits) {
    const auto lim = rm_.current_limits();
    EXPECT_EQ(lim.max_order_quantity, std::numeric_limits<core::Quantity>::max());
    EXPECT_EQ(lim.max_gross_exposure, std::numeric_limits<core::PriceTicks>::max());
}

TEST_F(NullRiskManagerTest, UpdateLimitsIsNoOp) {
    RiskLimits new_limits{};
    new_limits.max_order_quantity = 50;
    rm_.update_limits(new_limits);
    // NullRiskManager ignores the update — still no-limit
    const auto lim = rm_.current_limits();
    EXPECT_EQ(lim.max_order_quantity, std::numeric_limits<core::Quantity>::max());
}

TEST_F(NullRiskManagerTest, NameIsNonEmpty) {
    EXPECT_FALSE(rm_.name().empty());
}

// ---------------------------------------------------------------------------
// RiskVerdictTest: constexpr factory and layout.
// ---------------------------------------------------------------------------
TEST(RiskVerdictTest, AcceptFactory) {
    constexpr auto v = RiskVerdict::accept();
    EXPECT_TRUE(v.approved);
    EXPECT_EQ(v.reject_reason, RiskRejectReason::None);
}

TEST(RiskVerdictTest, RejectFactory) {
    constexpr auto v = RiskVerdict::reject(RiskRejectReason::OrderTooLarge);
    EXPECT_FALSE(v.approved);
    EXPECT_EQ(v.reject_reason, RiskRejectReason::OrderTooLarge);
}

TEST(RiskVerdictTest, SizeIsEightBytes) {
    static_assert(sizeof(RiskVerdict) == 8);
    SUCCEED();
}

TEST(RiskVerdictTest, EqualityOperator) {
    EXPECT_EQ(RiskVerdict::accept(), RiskVerdict::accept());
    EXPECT_NE(RiskVerdict::accept(), RiskVerdict::reject(RiskRejectReason::DrawdownHaltActive));
}

// ---------------------------------------------------------------------------
// RiskLimitsTest: limit struct construction.
// ---------------------------------------------------------------------------
TEST(RiskLimitsTest, NoLimitHasMaxValues) {
    const auto lim = RiskLimits::no_limit();
    EXPECT_EQ(lim.max_order_quantity, std::numeric_limits<core::Quantity>::max());
    EXPECT_EQ(lim.daily_drawdown_limit, std::numeric_limits<core::PriceTicks>::max());
}

TEST(RiskLimitsTest, DefaultConstructedHasZeroLimits) {
    // Zero means "not configured" — the implementation must treat 0 as disabled,
    // NOT as "reject everything". Implementations must document their zero-limit
    // semantics clearly.
    const RiskLimits lim{};
    EXPECT_EQ(lim.max_order_quantity, 0u);
}

TEST(RiskLimitsTest, EqualityOperator) {
    EXPECT_EQ(RiskLimits::no_limit(), RiskLimits::no_limit());
    EXPECT_NE(RiskLimits{}, RiskLimits::no_limit());
}

// ---------------------------------------------------------------------------
// PortfolioViewTest: snapshot struct.
// ---------------------------------------------------------------------------
TEST(PortfolioViewTest, SessionPnlIsSumOfRealizedAndUnrealized) {
    PortfolioView pv{};
    pv.session_realized_pnl = 500;
    pv.session_unrealized_pnl = -200;
    EXPECT_EQ(pv.session_pnl(), 300);
}

TEST(PortfolioViewTest, SizeIsFortyBytes) {
    static_assert(sizeof(PortfolioView) == 40);
    SUCCEED();
}

// ===========================================================================
// StandardRiskManagerTest
//
// THESE TESTS DEFINE THE CONTRACT. They will FAIL until a junior agent
// implements StandardRiskManager. Do NOT modify assertions.
//
// The fixture uses a forward-declaration trick: it includes the header only
// if QUANTENGINE_PLATFORM_IMPL is defined, which the build system sets when
// the implementation sources are added. Until then, this test suite is
// compiled but skipped via GTEST_SKIP().
// ===========================================================================

#ifdef QUANTENGINE_RISK_IMPL
using TestedRiskManager = quantengine::risk::StandardRiskManager;
#else
// Stub: until implementation exists, redirect to NullRiskManager so the
// test binary links. Tests detect the stub and skip with a clear message.
using TestedRiskManager = quantengine::risk::NullRiskManager;
static constexpr bool kRiskImplMissing = true;
#endif

class StandardRiskManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
#ifndef QUANTENGINE_RISK_IMPL
        GTEST_SKIP() << "StandardRiskManager not yet implemented. "
                        "Define QUANTENGINE_RISK_IMPL and provide "
                        "include/quantengine/risk/StandardRiskManager.hpp "
                        "to run these tests.";
#endif
    }

    TestedRiskManager rm_;
};

// ----- Group 1: Max Order Quantity ------------------------------------------

TEST_F(StandardRiskManagerTest, ApprovesBelowMaxOrderQuantity) {
    RiskLimits lim{};
    lim.max_order_quantity = 1000;
    lim.max_position_quantity = std::numeric_limits<Quantity>::max();
    lim.max_order_notional = std::numeric_limits<PriceTicks>::max();
    lim.max_gross_exposure = std::numeric_limits<PriceTicks>::max();
    lim.daily_drawdown_limit = std::numeric_limits<PriceTicks>::max();
    rm_.update_limits(lim);

    const auto req = make_request(1, Side::Buy, 10000, 999);
    const auto v = rm_.validate(req, make_portfolio());
    EXPECT_TRUE(v.approved);
    EXPECT_EQ(v.reject_reason, RiskRejectReason::None);
}

TEST_F(StandardRiskManagerTest, RejectsAtMaxOrderQuantity) {
    RiskLimits lim{};
    lim.max_order_quantity = 1000;
    lim.max_position_quantity = std::numeric_limits<Quantity>::max();
    lim.max_order_notional = std::numeric_limits<PriceTicks>::max();
    lim.max_gross_exposure = std::numeric_limits<PriceTicks>::max();
    lim.daily_drawdown_limit = std::numeric_limits<PriceTicks>::max();
    rm_.update_limits(lim);

    // Exactly at the limit: REJECTED (limit is exclusive upper bound)
    const auto at = make_request(2, Side::Buy, 10000, 1000);
    const auto v1 = rm_.validate(at, make_portfolio());
    EXPECT_FALSE(v1.approved);
    EXPECT_EQ(v1.reject_reason, RiskRejectReason::OrderTooLarge);

    // Above the limit
    const auto over = make_request(3, Side::Buy, 10000, 1001);
    const auto v2 = rm_.validate(over, make_portfolio());
    EXPECT_FALSE(v2.approved);
    EXPECT_EQ(v2.reject_reason, RiskRejectReason::OrderTooLarge);
}

TEST_F(StandardRiskManagerTest, MaxOrderQuantityCheckedBeforeNotional) {
    // qty breach must be reported even when notional would also breach
    RiskLimits lim{};
    lim.max_order_quantity = 100;
    lim.max_order_notional = 50000;  // would also breach at qty=100, price=1000
    lim.max_position_quantity = std::numeric_limits<Quantity>::max();
    lim.max_gross_exposure = std::numeric_limits<PriceTicks>::max();
    lim.daily_drawdown_limit = std::numeric_limits<PriceTicks>::max();
    rm_.update_limits(lim);

    const auto req = make_request(1, Side::Buy, 1000, 200);  // qty=200, notional=200000
    const auto v = rm_.validate(req, make_portfolio());
    EXPECT_FALSE(v.approved);
    // qty check must fire first (limit hierarchy order)
    EXPECT_EQ(v.reject_reason, RiskRejectReason::OrderTooLarge);
}

// ----- Group 2: Max Order Notional ------------------------------------------

TEST_F(StandardRiskManagerTest, ApprovesBelowMaxNotional) {
    RiskLimits lim{};
    lim.max_order_quantity = std::numeric_limits<Quantity>::max();
    lim.max_order_notional = 1000000;  // 1M ticks
    lim.max_position_quantity = std::numeric_limits<Quantity>::max();
    lim.max_gross_exposure = std::numeric_limits<PriceTicks>::max();
    lim.daily_drawdown_limit = std::numeric_limits<PriceTicks>::max();
    rm_.update_limits(lim);

    // price=10000 * qty=99 = 990000 < 1000000: approve
    const auto req = make_request(1, Side::Buy, 10000, 99);
    const auto v = rm_.validate(req, make_portfolio());
    EXPECT_TRUE(v.approved);
}

TEST_F(StandardRiskManagerTest, RejectsAtMaxNotional) {
    RiskLimits lim{};
    lim.max_order_quantity = std::numeric_limits<Quantity>::max();
    lim.max_order_notional = 1000000;
    lim.max_position_quantity = std::numeric_limits<Quantity>::max();
    lim.max_gross_exposure = std::numeric_limits<PriceTicks>::max();
    lim.daily_drawdown_limit = std::numeric_limits<PriceTicks>::max();
    rm_.update_limits(lim);

    // price=10000 * qty=100 = 1000000: at or above limit -> reject
    const auto req = make_request(1, Side::Buy, 10000, 100);
    const auto v = rm_.validate(req, make_portfolio());
    EXPECT_FALSE(v.approved);
    EXPECT_EQ(v.reject_reason, RiskRejectReason::NotionalTooLarge);
}

// ----- Group 3: Max Position Quantity ---------------------------------------

TEST_F(StandardRiskManagerTest, ApprovesWhenPositionBelowLimit) {
    RiskLimits lim{};
    lim.max_order_quantity = std::numeric_limits<Quantity>::max();
    lim.max_order_notional = std::numeric_limits<PriceTicks>::max();
    lim.max_position_quantity = 500;
    lim.max_gross_exposure = std::numeric_limits<PriceTicks>::max();
    lim.daily_drawdown_limit = std::numeric_limits<PriceTicks>::max();
    rm_.update_limits(lim);

    // Current position 400, order qty 50: result 450 < 500 -> approve
    const auto pv = make_portfolio(0, 400);
    const auto req = make_request(1, Side::Buy, 10000, 50);
    const auto v = rm_.validate(req, pv);
    EXPECT_TRUE(v.approved);
}

TEST_F(StandardRiskManagerTest, RejectsWhenOrderWouldBreachPositionLimit) {
    RiskLimits lim{};
    lim.max_order_quantity = std::numeric_limits<Quantity>::max();
    lim.max_order_notional = std::numeric_limits<PriceTicks>::max();
    lim.max_position_quantity = 500;
    lim.max_gross_exposure = std::numeric_limits<PriceTicks>::max();
    lim.daily_drawdown_limit = std::numeric_limits<PriceTicks>::max();
    rm_.update_limits(lim);

    // Current position 400, order qty 150: result 550 >= 500 -> reject
    const auto pv = make_portfolio(0, 400);
    const auto req = make_request(1, Side::Buy, 10000, 150);
    const auto v = rm_.validate(req, pv);
    EXPECT_FALSE(v.approved);
    EXPECT_EQ(v.reject_reason, RiskRejectReason::PositionLimitBreached);
}

TEST_F(StandardRiskManagerTest, PositionCheckAppliesSymmetricallyToSells) {
    // Short position (represented as a large uint64 via two's complement wrap,
    // or the impl may use a separate signed field — implementation-defined).
    // The test verifies that a sell order which would deepen a short position
    // beyond max_position_quantity is also rejected.
    RiskLimits lim{};
    lim.max_order_quantity = std::numeric_limits<Quantity>::max();
    lim.max_order_notional = std::numeric_limits<PriceTicks>::max();
    lim.max_position_quantity = 500;
    lim.max_gross_exposure = std::numeric_limits<PriceTicks>::max();
    lim.daily_drawdown_limit = std::numeric_limits<PriceTicks>::max();
    rm_.update_limits(lim);

    // Short 400, adding sell 150 -> short 550 -> breach
    // Implementation must interpret net_position sign for sells.
    const auto pv = make_portfolio(0, static_cast<Quantity>(-400LL));
    const auto req = make_request(1, Side::Sell, 10000, 150);
    const auto v = rm_.validate(req, pv);
    EXPECT_FALSE(v.approved);
    EXPECT_EQ(v.reject_reason, RiskRejectReason::PositionLimitBreached);
}

// ----- Group 4: Daily Drawdown Halt -----------------------------------------

TEST_F(StandardRiskManagerTest, HaltsWhenDrawdownLimitBreached) {
    // The drawdown halt is triggered by the Portfolio, not by a single order.
    // The implementation must check session_pnl() against -daily_drawdown_limit
    // and engage the halt inside validate() or via a separate trigger method.
    RiskLimits lim{};
    lim.max_order_quantity = std::numeric_limits<Quantity>::max();
    lim.max_order_notional = std::numeric_limits<PriceTicks>::max();
    lim.max_position_quantity = std::numeric_limits<Quantity>::max();
    lim.max_gross_exposure = std::numeric_limits<PriceTicks>::max();
    lim.daily_drawdown_limit = 10000;  // halt when session_pnl <= -10000
    rm_.update_limits(lim);

    // Portfolio is in drawdown
    const auto pv = make_portfolio(-10000);  // session_realized_pnl = -10000
    const auto req = make_request(1, Side::Buy, 10000, 10);
    const auto v = rm_.validate(req, pv);

    EXPECT_FALSE(v.approved);
    EXPECT_EQ(v.reject_reason, RiskRejectReason::DrawdownHaltActive);
    // is_halted() must reflect the breach
    EXPECT_TRUE(rm_.is_halted());
}

TEST_F(StandardRiskManagerTest, HaltIsStickyAfterDrawdown) {
    RiskLimits lim{};
    lim.max_order_quantity = std::numeric_limits<Quantity>::max();
    lim.max_order_notional = std::numeric_limits<PriceTicks>::max();
    lim.max_position_quantity = std::numeric_limits<Quantity>::max();
    lim.max_gross_exposure = std::numeric_limits<PriceTicks>::max();
    lim.daily_drawdown_limit = 10000;
    rm_.update_limits(lim);

    // Trigger halt
    {
        const auto pv = make_portfolio(-10001);
        (void)rm_.validate(make_request(1, Side::Buy, 10000, 1), pv);
    }
    EXPECT_TRUE(rm_.is_halted());

    // Even if P&L recovers, halt must remain until explicitly reset
    {
        const auto pv = make_portfolio(5000);  // now profitable
        const auto v = rm_.validate(make_request(2, Side::Buy, 10000, 1), pv);
        EXPECT_FALSE(v.approved);
        EXPECT_EQ(v.reject_reason, RiskRejectReason::DrawdownHaltActive);
    }
}

TEST_F(StandardRiskManagerTest, ResetHaltAllowsTrading) {
    RiskLimits lim{};
    lim.max_order_quantity = std::numeric_limits<Quantity>::max();
    lim.max_order_notional = std::numeric_limits<PriceTicks>::max();
    lim.max_position_quantity = std::numeric_limits<Quantity>::max();
    lim.max_gross_exposure = std::numeric_limits<PriceTicks>::max();
    lim.daily_drawdown_limit = 10000;
    rm_.update_limits(lim);

    // Trigger halt
    (void)rm_.validate(make_request(1, Side::Buy, 10000, 1), make_portfolio(-15000));
    EXPECT_TRUE(rm_.is_halted());

    // Explicit reset
    rm_.reset_halt();
    EXPECT_FALSE(rm_.is_halted());

    // Now a valid order at a healthy P&L is approved
    const auto v = rm_.validate(make_request(2, Side::Buy, 10000, 1), make_portfolio(0));
    EXPECT_TRUE(v.approved);
}

TEST_F(StandardRiskManagerTest, ApprovesWhenDrawdownBelowLimit) {
    RiskLimits lim{};
    lim.max_order_quantity = std::numeric_limits<Quantity>::max();
    lim.max_order_notional = std::numeric_limits<PriceTicks>::max();
    lim.max_position_quantity = std::numeric_limits<Quantity>::max();
    lim.max_gross_exposure = std::numeric_limits<PriceTicks>::max();
    lim.daily_drawdown_limit = 10000;
    rm_.update_limits(lim);

    const auto pv = make_portfolio(-9999);  // one tick below threshold
    const auto v = rm_.validate(make_request(1, Side::Buy, 10000, 1), pv);
    EXPECT_TRUE(v.approved);
    EXPECT_FALSE(rm_.is_halted());
}

// ----- Group 5: Limit Hierarchy Order (first breach wins) -------------------

TEST_F(StandardRiskManagerTest, LimitHierarchyQtyBeforeNotionalBeforePosition) {
    RiskLimits lim{};
    // Set all limits tight so multiple would fire
    lim.max_order_quantity = 10;
    lim.max_order_notional = 100;   // 10 * 20 = 200 > 100
    lim.max_position_quantity = 5;  // 0 + 10 > 5
    lim.max_gross_exposure = std::numeric_limits<PriceTicks>::max();
    lim.daily_drawdown_limit = std::numeric_limits<PriceTicks>::max();
    rm_.update_limits(lim);

    // qty=10 breaches max_order_quantity (10 is not < 10); qty fires first
    const auto req = make_request(1, Side::Buy, 20, 10);
    const auto v = rm_.validate(req, make_portfolio());
    EXPECT_FALSE(v.approved);
    EXPECT_EQ(v.reject_reason, RiskRejectReason::OrderTooLarge);
}

// ----- Group 6: Duplicate client_order_id detection -------------------------

TEST_F(StandardRiskManagerTest, RejectsDuplicateClientOrderId) {
    // The risk manager must track submitted client_order_ids within a session
    // and reject re-use. This prevents double-submission after reconnect.
    const auto req = make_request(42, Side::Buy, 10000, 10);
    rm_.update_limits(RiskLimits::no_limit());

    const auto v1 = rm_.validate(req, make_portfolio());
    EXPECT_TRUE(v1.approved);  // first submission: ok

    // The risk manager must be notified of confirmed submissions so it can
    // track the ID. Call on_order_accepted() if the interface exposes it,
    // or validate() itself may do the tracking on first approval.
    const auto v2 = rm_.validate(req, make_portfolio());  // same id
    EXPECT_FALSE(v2.approved);
    EXPECT_EQ(v2.reject_reason, RiskRejectReason::DuplicateClientId);
}

}  // namespace quantengine::test

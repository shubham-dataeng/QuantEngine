#include <gtest/gtest.h>

#include "quantengine/execution/SimGateway.hpp"
#include "quantengine/market/IMarketDataFeed.hpp"
#include "quantengine/portfolio/Portfolio.hpp"
#include "quantengine/risk/StandardRiskManager.hpp"
#include "quantengine/strategy/StrategyRunner.hpp"

namespace quantengine::test {

using namespace quantengine::core;
using namespace quantengine::execution;
using namespace quantengine::market;
using namespace quantengine::portfolio;
using namespace quantengine::risk;
using namespace quantengine::strategy;

// ---------------------------------------------------------------------------
// Mock Feed for testing StrategyRunner
// ---------------------------------------------------------------------------
class MockFeed final : public IMarketDataFeed {
public:
    auto subscribe(EventHandlerBase& handler, std::string_view,
                   SubscriptionMask) noexcept -> FeedStatus override {
        handler_ = &handler;
        return FeedStatus::Ok;
    }

    auto unsubscribe(EventHandlerBase&, std::string_view) noexcept -> FeedStatus override {
        handler_ = nullptr;
        return FeedStatus::Ok;
    }

    auto start() noexcept -> FeedStatus override {
        running_ = true;
        if (handler_ != nullptr) {
            handler_->on_connected();
        }
        return FeedStatus::Ok;
    }

    auto stop() noexcept -> FeedStatus override {
        running_ = false;
        if (handler_ != nullptr) {
            handler_->on_disconnected();
        }
        return FeedStatus::Ok;
    }

    [[nodiscard]] auto is_running() const noexcept -> bool override { return running_; }
    [[nodiscard]] auto feed_name() const noexcept -> std::string_view override {
        return "MockFeed";
    }

    void publish(const MarketEvent& event) noexcept {
        if (handler_ != nullptr && running_) {
            handler_->on_event(event);
        }
    }

private:
    EventHandlerBase* handler_{nullptr};
    bool running_{false};
};

// ---------------------------------------------------------------------------
// Test Strategy: Simple market maker / taker for testing
// ---------------------------------------------------------------------------
class SimpleTestStrategy final : public IStrategy {
public:
    void on_start(IOrderRouter& router) noexcept override {
        router_ = &router;
        started_ = true;
    }

    void on_market_event(const MarketEvent& event) noexcept override {
        ++event_count_;
        if (auto_order_on_quote_) {
            if (const auto* q = std::get_if<Quote>(&event)) {
                OrderRequest req{};
                req.side = Side::Buy;
                req.type = OrderType::Limit;
                req.price = q->bid_price;
                req.quantity = 10;
                req.symbol = q->symbol;
                last_result_ = router_->submit(req);
            }
        }
    }

    void on_fill(const ExecutionReport& report) noexcept override {
        last_fill_ = report;
        ++fill_count_;
    }

    void on_stop() noexcept override { stopped_ = true; }

    [[nodiscard]] auto name() const noexcept -> std::string_view override {
        return "SimpleTestStrategy";
    }

    IOrderRouter* router_{nullptr};
    bool started_{false};
    bool stopped_{false};
    bool auto_order_on_quote_{false};
    std::size_t event_count_{0};
    std::size_t fill_count_{0};
    OrderResult last_result_{};
    ExecutionReport last_fill_{};
};

// ---------------------------------------------------------------------------
// StrategyRunnerTest Fixture
// ---------------------------------------------------------------------------
class StrategyRunnerTest : public ::testing::Test {
protected:
    MockFeed feed_;
    StandardRiskManager risk_{RiskLimits::no_limit()};
    SimGateway gateway_;
    Portfolio portfolio_;
    SimpleTestStrategy strategy_;
    SymbolArray aapl_ = make_symbol("AAPL");

    void SetUp() override { portfolio_.reset(); }
};

TEST_F(StrategyRunnerTest, LifecycleStartAndStop) {
    StrategyRunner runner(feed_, risk_, gateway_, portfolio_, strategy_);

    EXPECT_FALSE(runner.is_running());
    EXPECT_FALSE(strategy_.started_);

    EXPECT_TRUE(runner.run());
    EXPECT_TRUE(runner.is_running());
    EXPECT_TRUE(gateway_.is_connected());
    EXPECT_TRUE(feed_.is_running());
    EXPECT_TRUE(strategy_.started_);

    runner.stop();
    EXPECT_FALSE(runner.is_running());
    EXPECT_FALSE(gateway_.is_connected());
    EXPECT_FALSE(feed_.is_running());
    EXPECT_TRUE(strategy_.stopped_);
}

TEST_F(StrategyRunnerTest, MarketEventForwardedAndMarksPortfolio) {
    StrategyRunner runner(feed_, risk_, gateway_, portfolio_, strategy_);
    ASSERT_TRUE(runner.run());

    Quote q{};
    q.symbol = aapl_;
    q.bid_price = 100;
    q.ask_price = 102;
    feed_.publish(MarketEvent{q});

    EXPECT_EQ(strategy_.event_count_, 1u);

    runner.stop();
}

TEST_F(StrategyRunnerTest, ManualOrderSubmissionThroughRouter) {
    StrategyRunner runner(feed_, risk_, gateway_, portfolio_, strategy_);
    ASSERT_TRUE(runner.run());

    ASSERT_NE(strategy_.router_, nullptr);

    OrderRequest req{};
    req.side = Side::Buy;
    req.type = OrderType::Limit;
    req.price = 10000;
    req.quantity = 50;
    req.symbol = aapl_;

    const auto res = strategy_.router_->submit(req);
    EXPECT_TRUE(res.submitted);
    EXPECT_TRUE(res.accepted());
    EXPECT_EQ(res.gateway_ack.status, GatewayStatus::Accepted);
    EXPECT_EQ(runner.orders_submitted(), 1u);
    EXPECT_EQ(runner.orders_rejected_by_risk(), 0u);

    // SimGateway should have registered the resting order
    EXPECT_EQ(portfolio_.open_order_count(), 1u);
    EXPECT_EQ(strategy_.fill_count_, 1u);
    EXPECT_EQ(strategy_.last_fill_.status, OrderStatus::Resting);

    runner.stop();
}

TEST_F(StrategyRunnerTest, OrderRejectedByRiskManager) {
    // Set max order quantity = 25
    RiskLimits limits = RiskLimits::no_limit();
    limits.max_order_quantity = 25;
    risk_.update_limits(limits);

    StrategyRunner runner(feed_, risk_, gateway_, portfolio_, strategy_);
    ASSERT_TRUE(runner.run());

    OrderRequest req{};
    req.side = Side::Buy;
    req.type = OrderType::Limit;
    req.price = 10000;
    req.quantity = 50;  // Breaches limit (50 >= 25)
    req.symbol = aapl_;

    const auto res = strategy_.router_->submit(req);
    EXPECT_FALSE(res.submitted);
    EXPECT_FALSE(res.accepted());
    EXPECT_FALSE(res.risk_verdict.approved);
    EXPECT_EQ(res.risk_verdict.reject_reason, RiskRejectReason::OrderTooLarge);
    EXPECT_EQ(runner.orders_submitted(), 0u);
    EXPECT_EQ(runner.orders_rejected_by_risk(), 1u);
    EXPECT_EQ(strategy_.fill_count_, 0u);

    runner.stop();
}

TEST_F(StrategyRunnerTest, FullLoopExecutionAndFill) {
    StrategyRunner runner(feed_, risk_, gateway_, portfolio_, strategy_);
    ASSERT_TRUE(runner.run());

    // 1. Seed the book with a resting sell order at 100, qty 20
    OrderRequest sell_req{};
    sell_req.side = Side::Sell;
    sell_req.type = OrderType::Limit;
    sell_req.price = 100;
    sell_req.quantity = 20;
    sell_req.symbol = aapl_;
    const auto sell_res = strategy_.router_->submit(sell_req);
    ASSERT_TRUE(sell_res.accepted());

    // 2. Submit aggressive buy at 100, qty 20
    OrderRequest buy_req{};
    buy_req.side = Side::Buy;
    buy_req.type = OrderType::Limit;
    buy_req.price = 100;
    buy_req.quantity = 20;
    buy_req.symbol = aapl_;
    const auto buy_res = strategy_.router_->submit(buy_req);
    ASSERT_TRUE(buy_res.accepted());

    // Check portfolio received the fill and updated position
    const auto* pos = portfolio_.find_position(aapl_);
    ASSERT_NE(pos, nullptr);
    EXPECT_EQ(pos->net_quantity, 20);
    EXPECT_EQ(pos->avg_cost_ticks, 100);

    // Strategy on_fill should have received the Filled report
    EXPECT_EQ(strategy_.last_fill_.status, OrderStatus::Filled);
    EXPECT_EQ(strategy_.last_fill_.filled_quantity, 20u);

    runner.stop();
}

TEST_F(StrategyRunnerTest, AutoOrderOnQuoteEvent) {
    strategy_.auto_order_on_quote_ = true;

    StrategyRunner runner(feed_, risk_, gateway_, portfolio_, strategy_);
    ASSERT_TRUE(runner.run());

    // Publish quote
    Quote q{};
    q.symbol = aapl_;
    q.bid_price = 15000;
    q.ask_price = 15010;
    feed_.publish(MarketEvent{q});

    EXPECT_EQ(strategy_.event_count_, 1u);
    EXPECT_EQ(runner.orders_submitted(), 1u);
    EXPECT_TRUE(strategy_.last_result_.accepted());
    EXPECT_EQ(portfolio_.open_order_count(), 1u);

    runner.stop();
}

}  // namespace quantengine::test

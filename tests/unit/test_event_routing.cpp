#include <cstddef>
#include <cstdlib>
#include <new>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "quantengine/execution/IExecutionGateway.hpp"
#include "quantengine/market/IMarketDataFeed.hpp"
#include "quantengine/market/MarketEvent.hpp"
#include "quantengine/risk/IRiskManager.hpp"

// =============================================================================
// EventRoutingTest — TDD skeleton for the event dispatch pipeline.
//
// GOALS:
//   1. Verify that a mock MarketEvent passes through a dummy feed and reaches
//      a registered EventHandlerBase without dynamic heap allocation.
//   2. Verify that OrderRequest passes through IRiskManager::validate() and
//      IExecutionGateway::submit_order() without dynamic heap allocation.
//   3. Provide a template for correct mock implementations that junior agents
//      can extend when building real feed and gateway adapters.
//
// ALLOCATION TRACKING:
//   We override global operator new/delete to count allocations.
//   The counter is reset before each hot-path assertion.
//   Any allocation during the hot-path call is a test failure.
//
//   IMPORTANT CAVEAT: The allocation tracker catches *heap* allocations
//   (operator new). Stack allocations and static storage are not tracked
//   and are fine. std::variant, std::array, and plain structs used here
//   are stack-allocated by construction.
//
// FUTURE IMPL HOOKS (marked TODO):
//   These tests define the wiring contract. When a junior agent implements
//   a real CsvReplayFeed or AlpacaFeed, they must satisfy these tests
//   using the same EventHandlerBase and IMarketDataFeed interface — no
//   modifications to test assertions are permitted.
// =============================================================================

namespace quantengine::test {

// ---------------------------------------------------------------------------
// Allocation counter — thread-local so parallel test runs don't interfere.
// ---------------------------------------------------------------------------
namespace alloc_tracker {

thread_local std::size_t g_alloc_count   = 0;
thread_local std::size_t g_dealloc_count = 0;
thread_local bool        g_tracking      = false;

struct Guard {
    Guard()  { g_alloc_count = 0; g_dealloc_count = 0; g_tracking = true; }
    ~Guard() { g_tracking = false; }

    [[nodiscard]] auto allocations() const noexcept -> std::size_t {
        return g_alloc_count;
    }
};

}  // namespace alloc_tracker

}  // namespace quantengine::test

// Override global operator new/delete — must be at namespace scope.
void* operator new(std::size_t size) {
    if (quantengine::test::alloc_tracker::g_tracking) {
        ++quantengine::test::alloc_tracker::g_alloc_count;
    }
    void* ptr = std::malloc(size);
    if (ptr == nullptr) { throw std::bad_alloc{}; }
    return ptr;
}

void operator delete(void* ptr) noexcept {
    if (quantengine::test::alloc_tracker::g_tracking) {
        ++quantengine::test::alloc_tracker::g_dealloc_count;
    }
    std::free(ptr);
}

void operator delete(void* ptr, std::size_t /*size*/) noexcept {
    if (quantengine::test::alloc_tracker::g_tracking) {
        ++quantengine::test::alloc_tracker::g_dealloc_count;
    }
    std::free(ptr);
}

namespace quantengine::test {

using namespace quantengine::market;
using namespace quantengine::execution;
using namespace quantengine::risk;

// ---------------------------------------------------------------------------
// MockEventHandler: minimal EventHandlerBase that records received events.
// Does NOT allocate on the hot path — uses a pre-allocated fixed-size array.
// ---------------------------------------------------------------------------
class MockEventHandler final : public EventHandlerBase {
public:
    static constexpr std::size_t kMaxRecorded = 64;

    void on_event(const MarketEvent& event) noexcept override {
        if (received_count_ < kMaxRecorded) {
            received_[received_count_++] = event;
        }
    }
    void on_connected()                   noexcept override { connected_ = true; }
    void on_disconnected()                noexcept override { connected_ = false; }
    void on_error(std::string_view /*r*/) noexcept override { ++error_count_; }

    [[nodiscard]] auto received_count() const noexcept -> std::size_t {
        return received_count_;
    }
    [[nodiscard]] auto last_event() const noexcept -> const MarketEvent& {
        return received_[received_count_ - 1];
    }
    [[nodiscard]] auto is_connected() const noexcept -> bool { return connected_; }
    [[nodiscard]] auto error_count()  const noexcept -> std::size_t { return error_count_; }

    void reset() noexcept {
        received_count_ = 0;
        error_count_    = 0;
        connected_      = false;
    }

private:
    MarketEvent received_[kMaxRecorded]{};  // fixed array, stack/BSS allocated
    std::size_t received_count_{0};
    std::size_t error_count_{0};
    bool        connected_{false};
};

// Verify MockEventHandler satisfies the IEventHandler concept.
static_assert(IEventHandler<MockEventHandler>);

// ---------------------------------------------------------------------------
// MockFeed: a synchronous in-process feed that delivers events by calling
// dispatch() directly — no threads, no network, no heap.
//
// This is the reference implementation for testing; the junior agent's real
// feed implementations must produce the same observable behaviour.
// ---------------------------------------------------------------------------
class MockFeed final : public IMarketDataFeed {
public:
    [[nodiscard]] auto subscribe(EventHandlerBase& handler,
                                  std::string_view /*symbol*/,
                                  SubscriptionMask mask) noexcept -> FeedStatus override {
        handler_  = &handler;
        mask_     = mask;
        return FeedStatus::Ok;
    }

    [[nodiscard]] auto unsubscribe(EventHandlerBase& /*handler*/,
                                    std::string_view /*symbol*/) noexcept -> FeedStatus override {
        handler_ = nullptr;
        return FeedStatus::Ok;
    }

    [[nodiscard]] auto start() noexcept -> FeedStatus override {
        if (running_) { return FeedStatus::AlreadyRunning; }
        running_ = true;
        if (handler_ != nullptr) { handler_->on_connected(); }
        return FeedStatus::Ok;
    }

    [[nodiscard]] auto stop() noexcept -> FeedStatus override {
        if (!running_) { return FeedStatus::NotRunning; }
        running_ = false;
        if (handler_ != nullptr) { handler_->on_disconnected(); }
        return FeedStatus::Ok;
    }

    [[nodiscard]] auto is_running()  const noexcept -> bool            override { return running_; }
    [[nodiscard]] auto feed_name()   const noexcept -> std::string_view override { return "mock"; }

    // Synchronously push one event to the registered handler.
    // Filters by SubscriptionMask before calling on_event().
    // TODO: real implementations will call this from their socket/parse loop.
    void dispatch(const MarketEvent& ev) noexcept {
        if (handler_ == nullptr || !running_) { return; }

        // Filter check — zero allocation, pure bit-ops
        const auto kind = event_kind(ev);
        bool pass = false;
        switch (kind) {
            case MarketEventKind::Quote:
                pass = has_flag(mask_, SubscriptionMask::Quotes);   break;
            case MarketEventKind::Tick:
                pass = has_flag(mask_, SubscriptionMask::Ticks);    break;
            case MarketEventKind::MarketTrade:
                pass = has_flag(mask_, SubscriptionMask::Trades);   break;
            case MarketEventKind::OrderBookSnapshot:
                pass = has_flag(mask_, SubscriptionMask::Snapshots); break;
        }

        if (pass) { handler_->on_event(ev); }
    }

private:
    EventHandlerBase* handler_{nullptr};
    SubscriptionMask  mask_{SubscriptionMask::All};
    bool              running_{false};
};

// ---------------------------------------------------------------------------
// MockGateway: synchronous SimGateway stub.
// Approves every order, calls on_fill() synchronously (sim semantics).
// TODO: replace with real SimGateway backed by OptimizedMatchingEngine (M4).
// ---------------------------------------------------------------------------
class MockGateway final : public IExecutionGateway {
public:
    [[nodiscard]] auto connect(IFillHandler& handler) noexcept -> bool override {
        fill_handler_ = &handler;
        connected_    = true;
        handler.on_gateway_connected();
        return true;
    }

    void disconnect() noexcept override {
        connected_ = false;
        if (fill_handler_ != nullptr) {
            fill_handler_->on_gateway_disconnected("GRACEFUL");
        }
    }

    [[nodiscard]] auto is_connected() const noexcept -> bool override { return connected_; }

    [[nodiscard]] auto submit_order(const OrderRequest& req) noexcept -> OrderAck override {
        ++submitted_count_;
        if (!connected_) {
            return OrderAck{.client_order_id = req.client_order_id,
                            .status = GatewayStatus::NotConnected};
        }
        // Simulate synchronous fill (SimGateway semantics)
        if (fill_handler_ != nullptr) {
            core::ExecutionReport rpt{};
            rpt.order_id         = req.client_order_id;
            rpt.status           = core::OrderStatus::Resting;
            rpt.reject_reason    = core::RejectReason::None;
            rpt.remaining_quantity = req.quantity;
            rpt.filled_quantity  = 0;
            rpt.price            = req.price;
            rpt.side             = req.side;
            fill_handler_->on_fill(rpt);
        }
        return OrderAck{.client_order_id = req.client_order_id,
                        .status = GatewayStatus::Accepted};
    }

    [[nodiscard]] auto cancel_order(const CancelRequest& req) noexcept -> OrderAck override {
        return OrderAck{.client_order_id = req.client_order_id,
                        .status = GatewayStatus::Accepted};
    }

    [[nodiscard]] auto modify_order(const ModifyRequest& req) noexcept -> OrderAck override {
        return OrderAck{.client_order_id = req.client_order_id,
                        .status = GatewayStatus::Accepted};
    }

    [[nodiscard]] auto gateway_name() const noexcept -> std::string_view override {
        return "mock-gateway";
    }

    [[nodiscard]] auto submitted_count() const noexcept -> std::size_t {
        return submitted_count_;
    }

private:
    IFillHandler* fill_handler_{nullptr};
    bool          connected_{false};
    std::size_t   submitted_count_{0};
};

// ---------------------------------------------------------------------------
// MockFillHandler: records fills for assertion.
// ---------------------------------------------------------------------------
class MockFillHandler final : public IFillHandler {
public:
    static constexpr std::size_t kMaxFills = 64;

    void on_fill(const core::ExecutionReport& rpt) noexcept override {
        if (fill_count_ < kMaxFills) { fills_[fill_count_++] = rpt; }
    }
    void on_gateway_connected()                  noexcept override { connected_ = true; }
    void on_gateway_disconnected(std::string_view) noexcept override { connected_ = false; }

    [[nodiscard]] auto fill_count()    const noexcept -> std::size_t { return fill_count_; }
    [[nodiscard]] auto last_fill()     const noexcept -> const core::ExecutionReport& {
        return fills_[fill_count_ - 1];
    }
    [[nodiscard]] auto is_connected()  const noexcept -> bool { return connected_; }
    void reset() noexcept { fill_count_ = 0; connected_ = false; }

private:
    core::ExecutionReport fills_[kMaxFills]{};
    std::size_t           fill_count_{0};
    bool                  connected_{false};
};

// ===========================================================================
// Test Fixtures
// ===========================================================================

class EventRoutingTest : public ::testing::Test {
protected:
    MockFeed          feed_;
    MockEventHandler  handler_;
    MockGateway       gateway_;
    MockFillHandler   fill_handler_;
    NullRiskManager   risk_;

    void SetUp() override {
        // Wire everything up before each test
        ASSERT_EQ(feed_.subscribe(handler_, "AAPL", SubscriptionMask::All), FeedStatus::Ok);
        ASSERT_EQ(feed_.start(), FeedStatus::Ok);
        ASSERT_TRUE(gateway_.connect(fill_handler_));
    }

    void TearDown() override {
        gateway_.disconnect();
        (void)feed_.stop();
        (void)feed_.unsubscribe(handler_, "AAPL");
        handler_.reset();
        fill_handler_.reset();
    }
};

// ===========================================================================
// Tests
// ===========================================================================

// ----- Feed lifecycle -------------------------------------------------------

TEST_F(EventRoutingTest, StartNotifiesHandlerConnected) {
    EXPECT_TRUE(handler_.is_connected());
}

TEST_F(EventRoutingTest, StopNotifiesHandlerDisconnected) {
    (void)feed_.stop();
    EXPECT_FALSE(handler_.is_connected());
    // Restart for TearDown
    (void)feed_.start();
}

TEST_F(EventRoutingTest, DoubleStartReturnsAlreadyRunning) {
    EXPECT_EQ(feed_.start(), FeedStatus::AlreadyRunning);
}

TEST_F(EventRoutingTest, StopWhenNotRunningReturnsNotRunning) {
    (void)feed_.stop();
    EXPECT_EQ(feed_.stop(), FeedStatus::NotRunning);
    // Restart for TearDown
    (void)feed_.start();
}

// ----- Event dispatch — correctness -----------------------------------------

TEST_F(EventRoutingTest, QuoteEventReachesHandler) {
    Quote q{};
    q.bid_price = 15000;
    q.ask_price = 15005;
    q.symbol    = make_symbol("AAPL");

    feed_.dispatch(MarketEvent{q});

    ASSERT_EQ(handler_.received_count(), 1u);
    const auto& ev = handler_.last_event();
    ASSERT_TRUE(std::holds_alternative<Quote>(ev));
    EXPECT_EQ(std::get<Quote>(ev).bid_price, 15000);
}

TEST_F(EventRoutingTest, TickEventReachesHandler) {
    Tick t{};
    t.price    = 15002;
    t.quantity = 100;
    t.symbol   = make_symbol("AAPL");

    feed_.dispatch(MarketEvent{t});

    ASSERT_EQ(handler_.received_count(), 1u);
    EXPECT_TRUE(std::holds_alternative<Tick>(handler_.last_event()));
}

TEST_F(EventRoutingTest, SnapshotEventReachesHandler) {
    OrderBookSnapshot snap{};
    snap.symbol    = make_symbol("AAPL");
    snap.bid_count = 1;
    snap.bids[0]   = PriceLevel{15000, 200, 1, 0};
    snap.ask_count = 1;
    snap.asks[0]   = PriceLevel{15005, 150, 1, 0};

    feed_.dispatch(MarketEvent{snap});

    ASSERT_EQ(handler_.received_count(), 1u);
    ASSERT_TRUE(std::holds_alternative<OrderBookSnapshot>(handler_.last_event()));
    const auto& s = std::get<OrderBookSnapshot>(handler_.last_event());
    EXPECT_EQ(s.bids[0].price, 15000);
}

TEST_F(EventRoutingTest, MultipleEventsDeliveredInOrder) {
    for (int i = 0; i < 10; ++i) {
        Quote q{};
        q.bid_price = static_cast<core::PriceTicks>(15000 + i);
        feed_.dispatch(MarketEvent{q});
    }
    EXPECT_EQ(handler_.received_count(), 10u);
    EXPECT_EQ(std::get<Quote>(handler_.last_event()).bid_price, 15009);
}

// ----- SubscriptionMask filtering -------------------------------------------

TEST_F(EventRoutingTest, SnapshotsFilteredWhenMaskExcludesThem) {
    // Re-subscribe with Quotes only
    (void)feed_.unsubscribe(handler_, "AAPL");
    (void)feed_.subscribe(handler_, "AAPL", SubscriptionMask::Quotes);
    handler_.reset();

    // Dispatch a snapshot — should NOT reach the handler
    feed_.dispatch(MarketEvent{OrderBookSnapshot{}});
    EXPECT_EQ(handler_.received_count(), 0u);

    // Dispatch a quote — SHOULD reach the handler
    feed_.dispatch(MarketEvent{Quote{}});
    EXPECT_EQ(handler_.received_count(), 1u);
}

TEST_F(EventRoutingTest, AllMaskDeliversAllEventTypes) {
    feed_.dispatch(MarketEvent{Quote{}});
    feed_.dispatch(MarketEvent{Tick{}});
    feed_.dispatch(MarketEvent{MarketTrade{}});
    feed_.dispatch(MarketEvent{OrderBookSnapshot{}});
    EXPECT_EQ(handler_.received_count(), 4u);
}

// ----- Allocation checks — HOT PATH must be zero-allocation ----------------

TEST_F(EventRoutingTest, DispatchQuoteDoesNotAllocate) {
    Quote q{};
    q.bid_price = 15000;
    q.ask_price = 15005;
    q.symbol    = make_symbol("AAPL");
    const MarketEvent ev{q};

    // Warm up: first dispatch may prime internal state
    feed_.dispatch(ev);
    handler_.reset();

    alloc_tracker::Guard g;
    feed_.dispatch(ev);   // HOT PATH
    EXPECT_EQ(g.allocations(), 0u)
        << "dispatch() must be zero-allocation on the hot path";
}

TEST_F(EventRoutingTest, DispatchSnapshotDoesNotAllocate) {
    OrderBookSnapshot snap{};
    snap.bid_count = 5;
    snap.ask_count = 5;
    const MarketEvent ev{snap};

    feed_.dispatch(ev);  // warm up
    handler_.reset();

    alloc_tracker::Guard g;
    feed_.dispatch(ev);  // HOT PATH
    EXPECT_EQ(g.allocations(), 0u)
        << "OrderBookSnapshot dispatch must be zero-allocation";
}

TEST_F(EventRoutingTest, BurstOf1000EventsDoesNotAllocate) {
    const MarketEvent ev{Quote{}};

    // Warm up: deliver one event to stabilise any lazy initialisation
    feed_.dispatch(ev);
    handler_.reset();

    alloc_tracker::Guard g;
    for (int i = 0; i < 1000; ++i) {
        feed_.dispatch(ev);
    }
    EXPECT_EQ(g.allocations(), 0u)
        << "1000 dispatches must produce zero heap allocations";
    EXPECT_EQ(handler_.received_count(), MockEventHandler::kMaxRecorded);
}

// ----- Order submission through risk -> gateway ----------------------------

TEST_F(EventRoutingTest, ValidOrderPassesRiskAndReachesGateway) {
    auto req = OrderRequest{};
    req.client_order_id = 1;
    req.side            = core::Side::Buy;
    req.price           = 15000;
    req.quantity        = 100;
    req.symbol          = make_symbol("AAPL");

    const auto pv      = PortfolioView{};
    const auto verdict = risk_.validate(req, pv);
    ASSERT_TRUE(verdict.approved);

    const auto ack = gateway_.submit_order(req);
    EXPECT_EQ(ack.status, GatewayStatus::Accepted);
    EXPECT_EQ(gateway_.submitted_count(), 1u);
    EXPECT_EQ(fill_handler_.fill_count(), 1u);
}

TEST_F(EventRoutingTest, GatewayRejectsOrderWhenNotConnected) {
    MockGateway disconnected_gw;
    // No connect() called
    const auto req = OrderRequest{.client_order_id = 1};
    const auto ack = disconnected_gw.submit_order(req);
    EXPECT_EQ(ack.status, GatewayStatus::NotConnected);
}

TEST_F(EventRoutingTest, SubmitOrderDoesNotAllocate) {
    auto req = OrderRequest{};
    req.client_order_id = 99;
    req.side            = core::Side::Sell;
    req.price           = 15000;
    req.quantity        = 50;

    // Warm up
    (void)gateway_.submit_order(req);
    fill_handler_.reset();

    alloc_tracker::Guard g;
    const auto ack = gateway_.submit_order(req);   // HOT PATH
    EXPECT_EQ(g.allocations(), 0u)
        << "submit_order() must be zero-allocation on the hot path";
    EXPECT_EQ(ack.status, GatewayStatus::Accepted);
}

TEST_F(EventRoutingTest, RiskValidateDoesNotAllocate) {
    const auto req = OrderRequest{.client_order_id = 1, .quantity = 100};
    const auto pv  = PortfolioView{};

    // Warm up
    (void)risk_.validate(req, pv);

    alloc_tracker::Guard g;
    const auto verdict = risk_.validate(req, pv);   // HOT PATH
    EXPECT_EQ(g.allocations(), 0u)
        << "validate() must be zero-allocation on the hot path";
    EXPECT_TRUE(verdict.approved);
}

// ----- Symbol helpers -------------------------------------------------------

TEST_F(EventRoutingTest, EventSymbolHelperExtractsSymbol) {
    Quote q{};
    q.symbol = make_symbol("TSLA");
    const MarketEvent ev{q};
    EXPECT_EQ(event_symbol(ev), "TSLA");
}

TEST_F(EventRoutingTest, MakeSymbolTruncatesAtMaxLen) {
    // 16-char symbol (15 chars + null): "ABCDEFGHIJKLMNO"
    const auto sym = make_symbol("ABCDEFGHIJKLMNOPQ");  // 17 chars, truncated to 15
    const auto sv  = symbol_view(sym);
    EXPECT_EQ(sv.size(), kMaxSymbolLen - 1);
}

TEST_F(EventRoutingTest, QuoteSpreadIsCorrect) {
    Quote q{};
    q.bid_price = 10000;
    q.ask_price = 10003;
    EXPECT_EQ(q.spread_ticks(), 3);
    EXPECT_FALSE(q.is_crossed());
}

TEST_F(EventRoutingTest, SnapshotMidPriceIsCorrect) {
    OrderBookSnapshot snap{};
    snap.bid_count = 1;
    snap.ask_count = 1;
    snap.bids[0] = PriceLevel{10000, 100, 1, 0};
    snap.asks[0] = PriceLevel{10010, 100, 1, 0};
    EXPECT_EQ(snap.mid_price(), 10005);
}

// ----- Concept conformance --------------------------------------------------

TEST(ConceptConformanceTest, MockEventHandlerSatisfiesIEventHandler) {
    static_assert(IEventHandler<MockEventHandler>);
    SUCCEED();
}

TEST(ConceptConformanceTest, EventHandlerBaseSatisfiesIEventHandler) {
    static_assert(IEventHandler<EventHandlerBase>);
    SUCCEED();
}

}  // namespace quantengine::test

#pragma once

// quantengine/broker/AlpacaWsFeed.hpp
//
// M9 & M12: Alpaca WebSocket market data adapter with sequence tracking,
// gap detection, and reconnection state machine. Implements IMarketDataFeed.

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "quantengine/market/IMarketDataFeed.hpp"
#include "quantengine/market/MarketEvent.hpp"

namespace quantengine::broker {

struct AlpacaFeedConfig {
    std::string api_key{};
    std::string secret_key{};
    std::string feed_url{"wss://stream.data.alpaca.markets/v2/iex"};
    bool enable_gap_detection{true};
    std::uint32_t max_reconnect_attempts{5};
};

class AlpacaWsFeed final : public market::IMarketDataFeed {
public:
    explicit AlpacaWsFeed(const AlpacaFeedConfig& config = {}) noexcept;
    ~AlpacaWsFeed() override;

    AlpacaWsFeed(const AlpacaWsFeed&) = delete;
    auto operator=(const AlpacaWsFeed&) -> AlpacaWsFeed& = delete;
    AlpacaWsFeed(AlpacaWsFeed&&) = delete;
    auto operator=(AlpacaWsFeed&&) -> AlpacaWsFeed& = delete;

    // ---- IMarketDataFeed implementation -------------------------------------

    [[nodiscard]] auto subscribe(
        market::EventHandlerBase& handler, std::string_view symbol,
        market::SubscriptionMask mask = market::SubscriptionMask::All) noexcept
        -> market::FeedStatus override;

    [[nodiscard]] auto unsubscribe(market::EventHandlerBase& handler,
                                   std::string_view symbol) noexcept -> market::FeedStatus override;

    [[nodiscard]] auto start() noexcept -> market::FeedStatus override;
    [[nodiscard]] auto stop() noexcept -> market::FeedStatus override;

    [[nodiscard]] auto is_running() const noexcept -> bool override;
    [[nodiscard]] auto feed_name() const noexcept -> std::string_view override {
        return "AlpacaWsFeed";
    }

    // ---- Protocol Ingestion & Testing ---------------------------------------

    // Ingest raw JSON payload (e.g. from websocket frame) and dispatch to handlers
    bool process_raw_message(std::string_view json_text) noexcept;

    // Simulate network disconnect and reconnection for resilience testing (M12)
    void simulate_disconnect(std::string_view reason = "NETWORK_TIMEOUT") noexcept;
    bool reconnect() noexcept;

    // Diagnostics
    [[nodiscard]] std::uint64_t messages_received() const noexcept { return messages_received_; }
    [[nodiscard]] std::uint64_t quotes_dispatched() const noexcept { return quotes_dispatched_; }
    [[nodiscard]] std::uint64_t trades_dispatched() const noexcept { return trades_dispatched_; }
    [[nodiscard]] std::uint64_t gaps_detected() const noexcept { return gaps_detected_; }
    [[nodiscard]] std::uint32_t reconnect_count() const noexcept { return reconnect_count_; }

private:
    void dispatch_event(const market::MarketEvent& event,
                        const market::SymbolArray& symbol) noexcept;
    bool parse_and_dispatch_single(std::string_view json_obj) noexcept;

    struct Subscription {
        market::EventHandlerBase* handler{nullptr};
        std::string symbol{};
        market::SubscriptionMask mask{market::SubscriptionMask::All};
    };

    AlpacaFeedConfig config_{};
    mutable std::mutex mutex_;
    std::vector<Subscription> subscriptions_;
    std::atomic<bool> running_{false};

    // Sequence tracking for gap detection (M12)
    std::unordered_map<std::string, std::uint64_t> last_sequence_per_symbol_;
    std::uint64_t messages_received_{0};
    std::uint64_t quotes_dispatched_{0};
    std::uint64_t trades_dispatched_{0};
    std::uint64_t gaps_detected_{0};
    std::uint32_t reconnect_count_{0};
};

}  // namespace quantengine::broker

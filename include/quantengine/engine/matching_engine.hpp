#pragma once

#include <vector>

#include "quantengine/core/events.hpp"
#include "quantengine/core/order.hpp"
#include "quantengine/core/trade.hpp"
#include "quantengine/core/types.hpp"
#include "quantengine/reference/reference_order_book.hpp"

namespace quantengine::engine {

class MatchingEngine {
public:
    MatchingEngine() = default;
    explicit MatchingEngine(reference::ReferenceOrderBook book) : book_(std::move(book)) {}

    // Event Command Processor
    [[nodiscard]] core::ExecutionReport process_command(const core::OrderCommand& command);

    // Direct API (assigns monotonically increasing sequence numbers automatically)
    [[nodiscard]] core::ExecutionReport submit_order(core::OrderId order_id, core::Side side,
                                                     core::PriceTicks price,
                                                     core::Quantity quantity);
    [[nodiscard]] core::ExecutionReport cancel_order(core::OrderId order_id);
    [[nodiscard]] core::ExecutionReport modify_order(core::OrderId order_id,
                                                     core::PriceTicks new_price,
                                                     core::Quantity new_quantity);

    // Sequence & Trade Counters
    [[nodiscard]] core::SeqNum current_sequence() const noexcept {
        return (next_sequence_ > 1) ? next_sequence_ - 1 : 0;
    }
    [[nodiscard]] core::TradeId current_trade_id() const noexcept {
        return (next_trade_id_ > 1) ? next_trade_id_ - 1 : 0;
    }

    // Book Inspection
    [[nodiscard]] const reference::ReferenceOrderBook& book() const noexcept { return book_; }
    [[nodiscard]] reference::ReferenceOrderBook& book() noexcept { return book_; }

    // State Reset
    void reset() noexcept;

private:
    core::ExecutionReport handle_create(core::SeqNum seq, const core::CreateOrderCommand& cmd);
    core::ExecutionReport handle_cancel(core::SeqNum seq, const core::CancelOrderCommand& cmd);
    core::ExecutionReport handle_modify(core::SeqNum seq, const core::ModifyOrderCommand& cmd);

    reference::ReferenceOrderBook book_;
    core::SeqNum next_sequence_{1};
    core::TradeId next_trade_id_{1};
};

}  // namespace quantengine::engine

#include "quantengine/engine/matching_engine.hpp"

#include <algorithm>

namespace quantengine::engine {

using namespace quantengine::core;

ExecutionReport MatchingEngine::process_command(const OrderCommand& command) {
    SeqNum seq = command.sequence_number;
    if (seq == 0) {
        seq = next_sequence_++;
    } else if (seq >= next_sequence_) {
        next_sequence_ = seq + 1;
    }

    return std::visit(
        [this, seq](const auto& payload) -> ExecutionReport {
            using T = std::decay_t<decltype(payload)>;
            if constexpr (std::is_same_v<T, CreateOrderCommand>) {
                return handle_create(seq, payload);
            } else if constexpr (std::is_same_v<T, CancelOrderCommand>) {
                return handle_cancel(seq, payload);
            } else if constexpr (std::is_same_v<T, ModifyOrderCommand>) {
                return handle_modify(seq, payload);
            }
        },
        command.payload);
}

ExecutionReport MatchingEngine::submit_order(OrderId order_id, Side side, PriceTicks price,
                                             Quantity quantity) {
    const SeqNum seq = next_sequence_++;
    return handle_create(seq, CreateOrderCommand{order_id, side, price, quantity});
}

ExecutionReport MatchingEngine::cancel_order(OrderId order_id) {
    const SeqNum seq = next_sequence_++;
    return handle_cancel(seq, CancelOrderCommand{order_id});
}

ExecutionReport MatchingEngine::modify_order(OrderId order_id, PriceTicks new_price,
                                             Quantity new_quantity) {
    const SeqNum seq = next_sequence_++;
    return handle_modify(seq, ModifyOrderCommand{order_id, new_price, new_quantity});
}

ExecutionReport MatchingEngine::handle_create(SeqNum seq, const CreateOrderCommand& cmd) {
    if (cmd.order_id == 0) {
        return ExecutionReport{.order_id = cmd.order_id,
                               .status = OrderStatus::Rejected,
                               .reject_reason = RejectReason::UnknownOrder,
                               .remaining_quantity = 0,
                               .filled_quantity = 0,
                               .price = cmd.price,
                               .side = cmd.side,
                               .sequence_number = seq,
                               .trades = {}};
    }

    if (cmd.price <= 0) {
        return ExecutionReport{.order_id = cmd.order_id,
                               .status = OrderStatus::Rejected,
                               .reject_reason = RejectReason::InvalidPrice,
                               .remaining_quantity = 0,
                               .filled_quantity = 0,
                               .price = cmd.price,
                               .side = cmd.side,
                               .sequence_number = seq,
                               .trades = {}};
    }

    if (cmd.quantity == 0) {
        return ExecutionReport{.order_id = cmd.order_id,
                               .status = OrderStatus::Rejected,
                               .reject_reason = RejectReason::InvalidQuantity,
                               .remaining_quantity = 0,
                               .filled_quantity = 0,
                               .price = cmd.price,
                               .side = cmd.side,
                               .sequence_number = seq,
                               .trades = {}};
    }

    if (book_.has_order(cmd.order_id)) {
        return ExecutionReport{.order_id = cmd.order_id,
                               .status = OrderStatus::Rejected,
                               .reject_reason = RejectReason::DuplicateOrderId,
                               .remaining_quantity = 0,
                               .filled_quantity = 0,
                               .price = cmd.price,
                               .side = cmd.side,
                               .sequence_number = seq,
                               .trades = {}};
    }

    Order incoming(cmd.order_id, cmd.side, cmd.price, cmd.quantity, seq);
    std::vector<Trade> trades;

    if (cmd.side == Side::Buy) {
        while (incoming.remaining_quantity() > 0) {
            const auto best_ask = book_.best_ask_price();
            if (!best_ask || incoming.price() < *best_ask) {
                break;
            }

            Order* maker = book_.get_best_ask_order();
            const Quantity match_qty =
                std::min(incoming.remaining_quantity(), maker->remaining_quantity());
            const PriceTicks match_price = maker->price();

            [[maybe_unused]] const bool mf = maker->apply_fill(match_qty);
            [[maybe_unused]] const bool tf = incoming.apply_fill(match_qty);

            trades.push_back(Trade{.trade_id = next_trade_id_++,
                                   .maker_order_id = maker->order_id(),
                                   .taker_order_id = incoming.order_id(),
                                   .maker_side = maker->side(),
                                   .price = match_price,
                                   .quantity = match_qty,
                                   .sequence_number = seq});

            if (maker->remaining_quantity() == 0) {
                book_.pop_best_ask_order();
            }
        }
    } else {
        while (incoming.remaining_quantity() > 0) {
            const auto best_bid = book_.best_bid_price();
            if (!best_bid || incoming.price() > *best_bid) {
                break;
            }

            Order* maker = book_.get_best_bid_order();
            const Quantity match_qty =
                std::min(incoming.remaining_quantity(), maker->remaining_quantity());
            const PriceTicks match_price = maker->price();

            [[maybe_unused]] const bool mf = maker->apply_fill(match_qty);
            [[maybe_unused]] const bool tf = incoming.apply_fill(match_qty);

            trades.push_back(Trade{.trade_id = next_trade_id_++,
                                   .maker_order_id = maker->order_id(),
                                   .taker_order_id = incoming.order_id(),
                                   .maker_side = maker->side(),
                                   .price = match_price,
                                   .quantity = match_qty,
                                   .sequence_number = seq});

            if (maker->remaining_quantity() == 0) {
                book_.pop_best_bid_order();
            }
        }
    }

    OrderStatus final_status = OrderStatus::New;
    if (incoming.remaining_quantity() == 0) {
        final_status = OrderStatus::Filled;
    } else {
        if (trades.empty()) {
            final_status = OrderStatus::Resting;
        } else {
            final_status = OrderStatus::PartiallyFilled;
        }
        [[maybe_unused]] const bool added = book_.add_order(incoming);
    }

    return ExecutionReport{.order_id = incoming.order_id(),
                           .status = final_status,
                           .reject_reason = RejectReason::None,
                           .remaining_quantity = incoming.remaining_quantity(),
                           .filled_quantity = incoming.filled_quantity(),
                           .price = incoming.price(),
                           .side = incoming.side(),
                           .sequence_number = seq,
                           .trades = std::move(trades)};
}

ExecutionReport MatchingEngine::handle_cancel(SeqNum seq, const CancelOrderCommand& cmd) {
    if (!book_.has_order(cmd.order_id)) {
        return ExecutionReport{.order_id = cmd.order_id,
                               .status = OrderStatus::Rejected,
                               .reject_reason = RejectReason::UnknownOrder,
                               .remaining_quantity = 0,
                               .filled_quantity = 0,
                               .price = 0,
                               .side = Side::Buy,
                               .sequence_number = seq,
                               .trades = {}};
    }

    auto cancelled_opt = book_.cancel_order(cmd.order_id);
    return ExecutionReport{.order_id = cancelled_opt->order_id(),
                           .status = OrderStatus::Cancelled,
                           .reject_reason = RejectReason::None,
                           .remaining_quantity = 0,
                           .filled_quantity = cancelled_opt->filled_quantity(),
                           .price = cancelled_opt->price(),
                           .side = cancelled_opt->side(),
                           .sequence_number = seq,
                           .trades = {}};
}

ExecutionReport MatchingEngine::handle_modify(SeqNum seq, const ModifyOrderCommand& cmd) {
    if (cmd.new_price <= 0) {
        return ExecutionReport{.order_id = cmd.order_id,
                               .status = OrderStatus::Rejected,
                               .reject_reason = RejectReason::InvalidPrice,
                               .remaining_quantity = 0,
                               .filled_quantity = 0,
                               .price = cmd.new_price,
                               .side = Side::Buy,
                               .sequence_number = seq,
                               .trades = {}};
    }

    if (cmd.new_quantity == 0) {
        return ExecutionReport{.order_id = cmd.order_id,
                               .status = OrderStatus::Rejected,
                               .reject_reason = RejectReason::InvalidQuantity,
                               .remaining_quantity = 0,
                               .filled_quantity = 0,
                               .price = cmd.new_price,
                               .side = Side::Buy,
                               .sequence_number = seq,
                               .trades = {}};
    }

    const Order* existing = book_.get_order(cmd.order_id);
    if (existing == nullptr) {
        return ExecutionReport{.order_id = cmd.order_id,
                               .status = OrderStatus::Rejected,
                               .reject_reason = RejectReason::UnknownOrder,
                               .remaining_quantity = 0,
                               .filled_quantity = 0,
                               .price = cmd.new_price,
                               .side = Side::Buy,
                               .sequence_number = seq,
                               .trades = {}};
    }

    const Side side = existing->side();
    // Cancel from book atomically
    [[maybe_unused]] auto cancelled_opt = book_.cancel_order(cmd.order_id);

    // Reinsert as incoming taker order
    return handle_create(seq,
                         CreateOrderCommand{cmd.order_id, side, cmd.new_price, cmd.new_quantity});
}

void MatchingEngine::reset() noexcept {
    book_ = reference::ReferenceOrderBook{};
    next_sequence_ = 1;
    next_trade_id_ = 1;
}

}  // namespace quantengine::engine

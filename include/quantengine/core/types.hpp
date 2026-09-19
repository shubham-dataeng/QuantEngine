#pragma once

#include <cstdint>
#include <iosfwd>
#include <string_view>

namespace quantengine::core {

using PriceTicks = std::int64_t;
using Quantity = std::uint64_t;
using OrderId = std::uint64_t;
using SeqNum = std::uint64_t;
using TradeId = std::uint64_t;

enum class Side : std::uint8_t { Buy = 0, Sell = 1 };

[[nodiscard]] constexpr Side opposite_side(Side side) noexcept {
    return (side == Side::Buy) ? Side::Sell : Side::Buy;
}

[[nodiscard]] constexpr std::string_view to_string(Side side) noexcept {
    switch (side) {
        case Side::Buy:
            return "BUY";
        case Side::Sell:
            return "SELL";
    }
    return "UNKNOWN";
}

std::ostream& operator<<(std::ostream& os, Side side);

enum class OrderStatus : std::uint8_t {
    New = 0,
    Resting,
    PartiallyFilled,
    Filled,
    Cancelled,
    Rejected
};

[[nodiscard]] constexpr std::string_view to_string(OrderStatus status) noexcept {
    switch (status) {
        case OrderStatus::New:
            return "NEW";
        case OrderStatus::Resting:
            return "RESTING";
        case OrderStatus::PartiallyFilled:
            return "PARTIALLY_FILLED";
        case OrderStatus::Filled:
            return "FILLED";
        case OrderStatus::Cancelled:
            return "CANCELLED";
        case OrderStatus::Rejected:
            return "REJECTED";
    }
    return "UNKNOWN";
}

std::ostream& operator<<(std::ostream& os, OrderStatus status);

enum class OrderType : std::uint8_t { Limit = 0 };

[[nodiscard]] constexpr std::string_view to_string(OrderType type) noexcept {
    switch (type) {
        case OrderType::Limit:
            return "LIMIT";
    }
    return "UNKNOWN";
}

std::ostream& operator<<(std::ostream& os, OrderType type);

enum class RejectReason : std::uint8_t {
    None = 0,
    DuplicateOrderId,
    UnknownOrder,
    InvalidPrice,
    InvalidQuantity,
    OrderAlreadyFilled,
    OrderAlreadyCancelled,
    InvalidStateTransition
};

[[nodiscard]] constexpr std::string_view to_string(RejectReason reason) noexcept {
    switch (reason) {
        case RejectReason::None:
            return "NONE";
        case RejectReason::DuplicateOrderId:
            return "DUPLICATE_ORDER_ID";
        case RejectReason::UnknownOrder:
            return "UNKNOWN_ORDER";
        case RejectReason::InvalidPrice:
            return "INVALID_PRICE";
        case RejectReason::InvalidQuantity:
            return "INVALID_QUANTITY";
        case RejectReason::OrderAlreadyFilled:
            return "ORDER_ALREADY_FILLED";
        case RejectReason::OrderAlreadyCancelled:
            return "ORDER_ALREADY_CANCELLED";
        case RejectReason::InvalidStateTransition:
            return "INVALID_STATE_TRANSITION";
    }
    return "UNKNOWN";
}

std::ostream& operator<<(std::ostream& os, RejectReason reason);

}  // namespace quantengine::core

#pragma once

#include <variant>
#include <vector>

#include "quantengine/core/order.hpp"
#include "quantengine/core/trade.hpp"
#include "quantengine/core/types.hpp"

namespace quantengine::core {

struct CreateOrderCommand {
    OrderId order_id{0};
    Side side{Side::Buy};
    PriceTicks price{0};
    Quantity quantity{0};

    [[nodiscard]] constexpr bool operator==(const CreateOrderCommand&) const noexcept = default;
};

struct CancelOrderCommand {
    OrderId order_id{0};

    [[nodiscard]] constexpr bool operator==(const CancelOrderCommand&) const noexcept = default;
};

struct ModifyOrderCommand {
    OrderId order_id{0};
    PriceTicks new_price{0};
    Quantity new_quantity{0};

    [[nodiscard]] constexpr bool operator==(const ModifyOrderCommand&) const noexcept = default;
};

using OrderCommandPayload =
    std::variant<CreateOrderCommand, CancelOrderCommand, ModifyOrderCommand>;

struct OrderCommand {
    SeqNum sequence_number{0};
    OrderCommandPayload payload;

    [[nodiscard]] bool operator==(const OrderCommand&) const noexcept = default;
};

struct ExecutionReport {
    OrderId order_id{0};
    OrderStatus status{OrderStatus::Rejected};
    RejectReason reject_reason{RejectReason::None};
    Quantity remaining_quantity{0};
    Quantity filled_quantity{0};
    PriceTicks price{0};
    Side side{Side::Buy};
    SeqNum sequence_number{0};
    std::vector<Trade> trades;

    [[nodiscard]] bool operator==(const ExecutionReport&) const noexcept = default;
};

}  // namespace quantengine::core
